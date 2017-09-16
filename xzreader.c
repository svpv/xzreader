// Copyright (c) 2017 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdio.h> // sys_errlist
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <lzma.h>
#include "reada.h"
#include "xzreader.h"

// A thread-safe strerror(3) replacement.
static const char *xstrerror(int errnum)
{
    // Some of the great minds say that sys_errlist is deprecated.
    // Well, at least it's thread-safe, and it does not deadlock.
    if (errnum > 0 && errnum < sys_nerr)
	return sys_errlist[errnum];
    return "Unknown error";
}

// The list of possible errors during decoding is based on
// egrep 'case.*ERROR:|msg =' doc/examples/02_decompress.c
static const char *lzma_strerror(lzma_ret ret)
{
    switch (ret) {
    case LZMA_MEM_ERROR:
	return "Memory allocation failed";
    case LZMA_OPTIONS_ERROR:
	return "Unsupported decompressor flags";
    case LZMA_FORMAT_ERROR:
	return "The input is not in the .xz format";
    case LZMA_DATA_ERROR:
	return "Compressed file is corrupt";
    case LZMA_BUF_ERROR:
	return "Compressed file is truncated or otherwise corrupt";
    default:
	break;
    }
    return "Unknown error, possibly a bug";
}

// Helpers to fill err[2] arg.
#define ERRNO(func) err[0] = func, err[1] = xstrerror(errno)
#define ERRXZ(func, ret) err[0] = func, err[1] = lzma_strerror(ret)
#define ERRSTR(str) err[0] = __func__, err[1] = str

// Start decoding at the beginning of a stream.
static int xzreader_begin(lzma_stream *lzma, struct fda *fda, const char *err[2])
{
    unsigned char buf[LZMA_STREAM_HEADER_SIZE];
    ssize_t n = reada(fda, &buf, sizeof buf);
    if (n < 0)
	return ERRNO("read"), -1;
    if (n == 0)
	return 0;
    if (n < sizeof buf)
	return ERRSTR("input too small"), -1;

    lzma->next_in = buf, lzma->avail_in = sizeof buf;
    lzma->next_out = NULL, lzma->avail_out = 0;

    lzma_ret zret = lzma_code(lzma, LZMA_RUN);
    if (zret != LZMA_OK)
	return ERRXZ("lzma_code", zret), -1;

    assert(lzma->avail_in == 0);

    return 1;
}

struct xzreader {
    struct fda *fda;
    lzma_stream lzma;
    bool eof;
};

// 80M should be enough to decode 'xz -9' output.  The limit once was
// in use and was specified in the pre-5.0 xz(1) manpage.  The limit
// exemplifies the Pareto principle.
#define MEMLIMIT (80<<20)

int xzreader_open(struct xzreader **zp, struct fda *fda, const char *err[2])
{
    lzma_stream lzma = LZMA_STREAM_INIT;
    lzma_ret zret = lzma_stream_decoder(&lzma, MEMLIMIT, 0);
    if (zret != LZMA_OK)
	return ERRXZ("lzma_stream_decoder", zret), -1;

    int rc = xzreader_begin(&lzma, fda, err);
    if (rc <= 0)
	return lzma_end(&lzma), rc;

    struct xzreader *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), lzma_end(&lzma), -1;

    z->fda = fda;
    z->lzma = lzma;
    z->eof = false;

    *zp = z;
    return 1;
}

int xzreader_reopen(struct xzreader *z, struct fda *fda, const char *err[2])
{
    if (fda)
	z->fda = fda;

    // The stream can be reused only upon successful decoding.
    if (!z->eof) {
	lzma_end(&z->lzma);
	z->lzma = (lzma_stream) LZMA_STREAM_INIT;

	lzma_ret zret = lzma_stream_decoder(&z->lzma, MEMLIMIT, 0);
	if (zret != LZMA_OK)
	    return ERRXZ("lzma_stream_decoder", zret), -1;
    }

    z->eof = false;

    return xzreader_begin(&z->lzma, z->fda, err);
}

void xzreader_free(struct xzreader *z)
{
    if (!z)
	return;
    lzma_end(&z->lzma);
    free(z);
}

ssize_t xzreader_read(struct xzreader *z, void *buf, size_t size, const char *err[2])
{
    if (z->eof)
	return 0;
    assert(size > 0);

    size_t total = 0;

    do {
	// Prefill the internal buffer.
	unsigned w;
	ssize_t ret = peeka(z->fda, &w, 4);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret == 0)
	    return ERRSTR("unexpected EOF"), -1;

	// We must not read past the end of the current frame, and the library
	// doesn't give us any clue as to where that end might be.  Therefore,
	// I employ this special technique of "tentative reads": the internal
	// buffer is fed into the decoder, and how many bytes are actually
	// read becomes known only after the call.
	z->lzma.next_in = (void *) z->fda->cur;
	z->lzma.avail_in = z->fda->end - z->fda->cur;
	z->lzma.next_out = buf;
	z->lzma.avail_out = size;

	lzma_ret zret = lzma_code(&z->lzma, LZMA_RUN);
	if (zret == LZMA_STREAM_END)
	    z->eof = true;
	else if (zret != LZMA_OK)
	    return ERRXZ("lzma_code", zret), -1;

	// See how many bytes have been read.
	if (z->lzma.avail_in)
	    z->fda->cur = z->fda->end - z->lzma.avail_in;
	else
	    z->fda->cur = z->fda->end = NULL;

	// See how many bytes have been recovered.
	size_t n = size - z->lzma.avail_out;
	size = z->lzma.avail_out, buf = (char *) buf + n;
	total += n;
    } while (size && !z->eof);

    return total;
}

// ex:set ts=8 sts=4 sw=4 noet:

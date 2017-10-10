#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "xzreader.h"
#include "reada.h"

#define PROG "xzreader"

static void error(const char *func, const char *err[2])
{
    if (strcmp(func, err[0]) == 0)
	fprintf(stderr, PROG ": %s: %s\n", err[0], err[1]);
    else
	fprintf(stderr, PROG ": %s: %s: %s\n", func, err[0], err[1]);
}

static void my_rewind(struct xzreader *z, struct fda *fda)
{
    off_t pos = lseek(fda->fd, 0, 0);
    assert(pos == 0);
    *fda = (struct fda) { fda->fd, fda->buf };
    const char *err[2];
    int zret = xzreader_reopen(z, fda, err);
    assert(zret == 1);
}

int main()
{
    char fdabuf[NREADA];
    struct fda fda = { 0, fdabuf };

    const char *err[2];
    struct xzreader *z;
    int zret = xzreader_open(&z, &fda, err);
    if (zret < 0)
	return error("xzreader_fdopen", err), 1;
    if (zret == 0)
	return fprintf(stderr, PROG ": empty input\n"), 0;

    char buf[256<<10];
    ssize_t ret;

    while (1) {
	while ((ret = xzreader_read(z, buf, sizeof buf, err)) > 0)
	    continue;
	if (ret < 0)
	    return error("xzreader_read", err), 1;
	zret = xzreader_reopen(z, &fda, err);
	if (zret < 0)
	    return error("xzreader_reopen", err), 1;
	if (zret == 0)
	    break;
    }

    my_rewind(z, &fda);

    while (1) {
	while ((ret = xzreader_read(z, buf, sizeof buf, err)) > 0)
	    fwrite(buf, 1, ret, stdout);
	if (ret < 0)
	    return error("xzreader_read", err), 1;
	zret = xzreader_reopen(z, &fda, err);
	if (zret < 0)
	    return error("xzreader_reopen", err), 1;
	if (zret == 0)
	    break;
    }

    xzreader_free(z);

    return 0;
}

// ex:set ts=8 sts=4 sw=4 noet:

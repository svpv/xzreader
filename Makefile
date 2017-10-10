RPM_OPT_FLAGS ?= -O2 -g -Wall
READA_DIR = ../reada
all: xzreader
xzreader: main.o xzreader.o reada.o
	$(CC) $(RPM_OPT_FLAGS) -o $@ $^ -llzma
main.o: main.c xzreader.h $(READA_DIR)/reada.h
	$(CC) $(RPM_OPT_FLAGS) -I$(READA_DIR) -c $<
xzreader.o: xzreader.c xzreader.h $(READA_DIR)/reada.h
	$(CC) $(RPM_OPT_FLAGS) -I$(READA_DIR) -c $<
reada.o: $(READA_DIR)/reada.c $(READA_DIR)/reada.h
	$(CC) $(RPM_OPT_FLAGS) -I$(READA_DIR) -c $<
clean:
	rm -f xzreader main.o xzreader.o reada.o
check: xzreader
	true        |xz  >test.xz
	echo -n foo |xz >>test.xz
	true        |xz --check=none >>test.xz
	echo -n bar |xz >>test.xz
	echo    baz |xz >>test.xz
	test `./xzreader <test.xz` = foobarbaz
	rm test.xz

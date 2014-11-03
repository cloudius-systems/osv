CC=gcc
CFLAGS=-O2 -g -Wall -std=gnu99
INCLUDES=-I../lua/src \
	-I$(lastword $(wildcard ../ncurses/build/*/include)) \
	-I$(lastword $(wildcard ../libedit/build/*/src))
LFLAGS=-L../lua/src \
	-L$(lastword $(wildcard ../ncurses/build/*/lib)) \
	-L$(lastword $(wildcard ../libedit/build/*/src/.libs))
LIBS=-ledit -ltinfo -llua
SRCS=cli.c
MAIN=cli.so

# For compiling executable running locally
LOC_INCLUDES=-I../lua/src
LOC_LFLAGS=-L../lua/src
LOC_LDFLAGS=-Wl,-rpath,$(abspath ../lua/src)
LOC_MAIN=cli

module: $(MAIN)

$(MAIN): $(SRCS)
	$(CC) -DOSV_CLI $(CFLAGS) $(INCLUDES) -fPIC -shared -o $@ $(SRCS) $(LFLAGS) $(LIBS)

$(LOC_MAIN): $(SRCS)
	$(CC) $(CFLAGS) $(LOC_INCLUDES) $^ -o $@ $(LOC_LDFLAGS) $(LOC_LFLAGS) $(LIBS)

clean:
	rm -f $(MAIN) $(LOC_MAIN)
	make -f rpmbuild/Makefile clean

rpm: $(LOC_MAIN)
	make -C rpmbuild

.PHONY: module clean rpm

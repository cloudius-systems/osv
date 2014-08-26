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
LOC_INCLUDES=-I/usr/include/lua5.2
LOC_LIBS=-ledit -ltinfo -llua5.2
LOC_MAIN=cli

module: $(MAIN)

$(MAIN): $(SRCS)
	$(CC) -DOSV_CLI $(CFLAGS) $(INCLUDES) -fPIC -shared -o $@ $(SRCS) $(LFLAGS) $(LIBS)

$(LOC_MAIN): $(SRCS)
	$(CC) $(CFLAGS) $(LOC_INCLUDES) $^ -o $@ $(LOC_LIBS)

clean:
	rm -f $(MAIN) $(LOC_MAIN)

.PHONY: module clean

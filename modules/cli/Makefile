LUA_LIB = $(shell pkg-config --libs lua53 2>/dev/null || pkg-config --libs lua || echo 'ERROR: Could not find lua, please run ./scripts/setup.py')
LUA_INCLUDES = $(shell pkg-config --cflags lua53 2>/dev/null || pkg-config --cflags lua || echo 'ERROR: Could not find lua, please run ./scripts/setup.py')

CC=gcc
CFLAGS=-O2 -g -Wall -std=gnu99
LIBS=-ledit -ltinfo $(LUA_LIB)
SRCS=cli.c
MAIN=cli

INCLUDES = $(LUA_INCLUDES)

SRC = $(shell readlink -f ../..)

module: $(MAIN)
	$(SRC)/scripts/manifest_from_host.sh $(MAIN) > usr.manifest

$(MAIN): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -fPIC -pie -o $@ $(LIBS)

rpm: $(MAIN)
	make -C rpmbuild

clean:
	rm -f $(MAIN)
	make -f rpmbuild/Makefile clean

.PHONY: module clean rpm

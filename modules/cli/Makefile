SRC = $(shell readlink -f ../..)

LUA_DIR = $(SRC)/modules/lua/upstream/lua5.3
export LD_LIBRARY_PATH = $(LUA_DIR)

CC=gcc
CFLAGS=-O2 -g -Wall -std=gnu99 -I $(LUA_DIR)/include
LIBS=-ledit -ltinfo -ldl -L$(LUA_DIR) -llua53
SRCS=cli.c
MAIN=cli

module: $(MAIN)
	$(SRC)/scripts/manifest_from_host.sh $(MAIN) > usr.manifest

$(MAIN): $(SRCS)
	$(CC) $(CFLAGS) $^ -fPIC -pie -o $@ $(LIBS)

rpm: $(MAIN)
	make -C rpmbuild

clean:
	rm -f $(MAIN)
	make -f rpmbuild/Makefile clean

.PHONY: module clean rpm

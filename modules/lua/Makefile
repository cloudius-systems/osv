# Some directories
OUT=out
BUILD=build
DOWNLOAD=download

# Official version
LUA_V=5.2

# Archive details
LUA_VERSION=5.2.3
LUA_FOLDER=lua-$(LUA_VERSION)
LUA_DOWNLOAD=http://www.lua.org/ftp/$(LUA_FOLDER).tar.gz
LUA_ARCHIVE=download/$(LUA_FOLDER).tar.gz

# Lua sources for dependencies
LUA_SRCS=lapi.c lapi.h lauxlib.c lauxlib.h lbaselib.c lbitlib.c lcode.c \
  lcode.h lcorolib.c lctype.c lctype.h ldblib.c ldebug.c ldebug.h ldo.c ldo.h \
  ldump.c lfunc.c lfunc.h lgc.c lgc.h linit.c liolib.c llex.c llex.h \
  llimits.h lmathlib.c lmem.c lmem.h loadlib.c lobject.c lobject.h lopcodes.c \
  lopcodes.h loslib.c lparser.c lparser.h lstate.c lstate.h lstring.c \
  lstring.h lstrlib.c ltable.c ltable.h ltablib.c ltm.c ltm.h lua.c luac.c \
  luaconf.h lua.h lualib.h lundump.c lundump.h lvm.c lvm.h lzio.c lzio.h

SRCS=$(addprefix src/, $(LUA_SRCS))

# Lua itself
MAIN=src/liblua.so

# Local executable used for LuaRocks
LUA_BIN=out/bin/lua

# List of Lua modules, each module has its own target
LUA_MODULES=LuaSocket LPeg LuaJSON Lua_stdlib
BDIR=$(OUT)/bin
LDIR=$(OUT)/lib/lua/$(LUA_V)
CDIR=$(OUT)/share/lua/$(LUA_V)

.PHONY: module clean LuaRocks $(LUA_MODULES)

module: $(MAIN) $(LUA_BIN) LuaRocks $(LUA_MODULES)

$(MAIN): $(SRCS)
	cd src && $(MAKE)

$(LUA_BIN): $(MAIN) | $(BDIR)
	cp src/lua $(LUA_BIN)

$(LUA_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
	 curl --remote-name --remote-time $(LUA_DOWNLOAD)

$(SRCS): $(LUA_ARCHIVE)
	tar -Oxzf $< $(LUA_FOLDER)/$@ > $@

$(DOWNLOAD) $(BUILD) $(LDIR) $(CDIR) $(BDIR):
	@mkdir -p $@

# == LuaRocks ==
LuaRocks_V=2.1.2
LuaRocks_F=luarocks-$(LuaRocks_V)
LuaRocks_A=$(LuaRocks_F).tar.gz
LuaRocks_D=http://luarocks.org/releases/$(LuaRocks_A)

LuaRocks: $(BDIR)/luarocks

$(BDIR)/luarocks: $(BUILD)/$(LuaRocks_F)/src/luarocks
	cd $(BUILD)/$(LuaRocks_F) && \
		make install

$(BUILD)/$(LuaRocks_F)/src/luarocks: $(BUILD)/$(LuaRocks_F)/Makefile
	cd $(BUILD)/$(LuaRocks_F) && \
		make build

$(BUILD)/$(LuaRocks_F)/Makefile: $(BUILD)/$(LuaRocks_F)/configure
	$(eval PREFIX=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST)))))
	cd $(BUILD)/$(LuaRocks_F) && \
		./configure --prefix=$(PREFIX)/$(OUT) --force-config \
		--with-lua-include=$(PREFIX)/src --lua-version=$(LUA_V)
	touch $@

$(BUILD)/$(LuaRocks_F)/configure: $(DOWNLOAD)/$(LuaRocks_A) | $(BUILD)
	tar --directory $(BUILD) -xzf $<
	touch $@

$(DOWNLOAD)/$(LuaRocks_A): | $(DOWNLOAD)
	curl --remote-time --location $(LuaRocks_D) > $@

# == LuaSocket ==

LuaSocket: $(LDIR)/socket/core.so

$(LDIR)/socket/core.so: | LuaRocks
	out/bin/luarocks install luasocket 3.0-rc1

# == LPeg ==
LPeg: $(LDIR)/lpeg.so

$(LDIR)/lpeg.so: | LuaRocks
	out/bin/luarocks install lpeg 0.12

# == LuaJSON ==
LuaJSON: $(CDIR)/json.lua

$(CDIR)/json.lua: | LuaRocks
	out/bin/luarocks install luajson 1.3.3

# == Lua_stdlib ==
Lua_stdlib: $(CDIR)/std.lua

$(CDIR)/std.lua: | LuaRocks
	out/bin/luarocks install stdlib 40

##
clean:
	rm -f $(SRCS)
	rm -rf $(DOWNLOAD) $(BUILD) $(OUT)
	cd src && $(MAKE) clean

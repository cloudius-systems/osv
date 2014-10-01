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

# Out sub-directories
BDIR=$(OUT)/bin
LDIR=$(OUT)/lib/lua/$(LUA_V)
CDIR=$(OUT)/share/lua/$(LUA_V)

# Lua itself
MAIN=src/liblua.so

# Local executable used for LuaRocks
LUA_BIN=$(BDIR)/lua
LUA_ROCKS_BIN=$(BDIR)/luarocks

# List of Lua modules, each module has its own target
LUA_MODULES=LuaSocket LuaJSON Lua_stdlib LuaFileSystem LuaPath LuaSec

module: $(MAIN) $(LUA_MODULES)

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

$(LUA_ROCKS_BIN): $(LUA_BIN) $(BUILD)/$(LuaRocks_F)/src/luarocks
	cd $(BUILD)/$(LuaRocks_F) && \
		make install

$(BUILD)/$(LuaRocks_F)/src/luarocks: $(BUILD)/$(LuaRocks_F)/Makefile
	cd $(BUILD)/$(LuaRocks_F) && \
		make build

$(BUILD)/$(LuaRocks_F)/Makefile: $(BUILD)/$(LuaRocks_F)/configure
	$(eval PREFIX=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST)))))
	cd $(BUILD)/$(LuaRocks_F) && \
		./configure --prefix=$(PREFIX)/$(OUT) --force-config \
		--with-lua-include=$(PREFIX)/src --lua-version=$(LUA_V) \
		--with-lua=$(PREFIX)/$(OUT)
	touch $@

$(BUILD)/$(LuaRocks_F)/configure: $(DOWNLOAD)/$(LuaRocks_A) | $(BUILD)
	tar --directory $(BUILD) -xzf $<
	touch $@

$(DOWNLOAD)/$(LuaRocks_A): | $(DOWNLOAD)
	curl --remote-time --location $(LuaRocks_D) > $@

# == LuaSocket ==
LuaSocket: $(LDIR)/socket/core.so

$(LDIR)/socket/core.so: $(LUA_ROCKS_BIN)
	out/bin/luarocks install luasocket 3.0-rc1

# == LuaJSON ==
LuaJSON: $(CDIR)/json.lua

$(CDIR)/json.lua: $(LUA_ROCKS_BIN)
	out/bin/luarocks install luajson 1.3.3

# == Lua_stdlib ==
Lua_stdlib: $(CDIR)/std.lua

$(CDIR)/std.lua: $(LUA_ROCKS_BIN)
	out/bin/luarocks install stdlib 40

# == LuaFileSystem ==
LuaFileSystem: $(LDIR)/lfs.so

$(LDIR)/lfs.so: $(LUA_ROCKS_BIN)
	out/bin/luarocks install LuaFileSystem 1.6.2

# == LuaPath ==
LuaPath: $(CDIR)/path.lua

$(CDIR)/path.lua: $(LUA_ROCKS_BIN)
	out/bin/luarocks install lua-path 0.2.1

# == LuaSec ==
LuaSec: $(CDIR)/ssl.lua

$(CDIR)/ssl.lua: $(LUA_ROCKS_BIN)
	out/bin/luarocks install LuaSec 0.5

##
clean:
	rm -f $(SRCS)
	rm -rf $(DOWNLOAD) $(BUILD) $(OUT)
	cd src && $(MAKE) clean

.PHONY: module clean $(LUA_MODULES)
.NOTPARALLEL: $(LUA_MODULES)

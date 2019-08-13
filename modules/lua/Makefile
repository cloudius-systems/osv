SRC = $(shell readlink -f ../..)
LUA_ROCKS=upstream/luarocks-3.1.1-linux-x86_64/luarocks
MODULES_DIR=install/lua_modules
LDIR=install/lua_modules/lib/lua/5.3
CDIR=install/lua_modules/share/lua/5.3

# List of Lua modules, each module has its own target
LUA_MODULES=LuaSocket LuaJSON Lua_stdlib LuaFileSystem LuaPath LuaSec

LUA_LIBRARY := $(shell ldconfig -p | grep -Po "liblua*.5\.3.so.0" | head -1)
ifndef LUA_LIBRARY
	LUA_LIBRARY := $(shell ldconfig -p | grep -Po "liblua*.5\.3.so" | head -1)
endif

module: $(LUA_MODULES)
	mkdir -p $(MODULES_DIR)
	$(SRC)/scripts/manifest_from_host.sh -l $(LUA_LIBRARY) > usr.manifest

$(LUA_ROCKS):
	mkdir -p upstream
	cd upstream && wget -c https://luarocks.github.io/luarocks/releases/luarocks-3.1.1-linux-x86_64.zip
	cd upstream && unzip luarocks-3.1.1-linux-x86_64.zip

# == LuaSocket ==
LuaSocket: $(LDIR)/socket/core.so

$(LDIR)/socket/core.so: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) luasocket 3.0rc1-2

# == LuaJSON ==
LuaJSON: $(CDIR)/json.lua

$(CDIR)/json.lua: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) luajson 1.3.4-1

# == Lua_stdlib ==
Lua_stdlib: $(CDIR)/std.lua

$(CDIR)/std.lua: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) stdlib 41.2.2-1

# == LuaFileSystem ==
LuaFileSystem: $(LDIR)/lfs.so

$(LDIR)/lfs.so: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) LuaFileSystem 1.7.0-2

# == LuaPath ==
LuaPath: $(CDIR)/path.lua

$(CDIR)/path.lua: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) lua-path 0.3.1-1

# == LuaSec ==
LuaSec: $(CDIR)/ssl.lua

$(CDIR)/ssl.lua: $(LUA_ROCKS)
	$(LUA_ROCKS) install --no-doc --tree $(MODULES_DIR) LuaSec 0.8-1

clean:
	rm -rf install
	rm -rf upstream

.PHONY: module clean $(LUA_MODULES)
.NOTPARALLEL: $(LUA_MODULES)

SRC = $(shell readlink -f ../..)

# This makefile orchestrates building some key lua modules used by the OSv cli
# module. Please note that both lua binaries, header files and luarocks are
# downloaded from internet and lua artifacts if installed on the host are not used.
# This should make maintenance of lua module much less painful as regardless
# of the Linux distribution and version it will use lua 5.3 and luarocks 3.1.1
# until we specifically upgrade them by modifying this makefile.

LUA=lua5.3
LUA_DIR=upstream/$(LUA)
LUA_ROCKS=upstream/luarocks-3.1.1-linux-x86_64/luarocks

MODULES_DIR=install/lua_modules
LUA_ROCKS_INSTALL_MODULE := $(LUA_ROCKS) --lua-dir=$(LUA_DIR) install --no-doc --tree $(MODULES_DIR)

LDIR=install/lua_modules/lib/lua/5.3
CDIR=install/lua_modules/share/lua/5.3

# Set LUAROCKS_CONFIG to make luarocks use lua binaries downloaded in upstream/lua5.3
export LUAROCKS_CONFIG=$(SRC)/modules/lua/upstream/config.lua

# List of Lua modules, each module has its own target
LUA_MODULES=LuaSocket LuaJSON Lua_stdlib LuaFileSystem LuaPath LuaSec

module: $(LUA_MODULES)
	mkdir -p $(MODULES_DIR)
	echo "/usr/lib/liblua53.so: $(SRC)/modules/lua/$(LUA_DIR)/liblua53.so" > usr.manifest

# Download lua interpreter from lua binaries
$(LUA_DIR)/lua53:
	mkdir -p $(LUA_DIR)
	cd upstream && wget -c "https://sourceforge.net/projects/luabinaries/files/5.3.6/Tools%20Executables/lua-5.3.6_Linux54_64_bin.tar.gz"
	cd $(LUA_DIR) && tar xf ../lua-5.3.6_Linux54_64_bin.tar.gz

# Download lua shared library and header files from lua binaries
$(LUA_DIR)/liblua53.so:
	mkdir -p $(LUA_DIR)
	cd upstream && wget -c "https://sourceforge.net/projects/luabinaries/files/5.3.6/Linux%20Libraries/lua-5.3.6_Linux54_64_lib.tar.gz"
	cd $(LUA_DIR) && tar xf ../lua-5.3.6_Linux54_64_lib.tar.gz

# In order for luarocks to use the downloaded version of lua in upstream, we need to create a config file below
upstream/config.lua:
	echo "variables = {" > upstream/config.lua
	echo "   LUA_INCDIR = \"$(SRC)/modules/lua/$(LUA_DIR)/include\"," >> upstream/config.lua
	echo "   LUA_LIBDIR = \"$(SRC)/modules/lua/$(LUA_DIR)\"," >> upstream/config.lua
	echo "}" >> upstream/config.lua

$(LUA_ROCKS): $(LUA_DIR)/lua53 $(LUA_DIR)/liblua53.so upstream/config.lua
	mkdir -p upstream
	cd upstream && wget -c "https://luarocks.github.io/luarocks/releases/luarocks-3.1.1-linux-x86_64.zip"
	cd upstream && unzip luarocks-3.1.1-linux-x86_64.zip
	touch $(LUA_ROCKS) #To prevent re-running the rule in case $(LUA_ROCKS) is older than $(LUA_DIR)/liblua53.so and/or $(LUA_DIR)/lua53

# == LuaSocket ==
LuaSocket: $(LDIR)/socket/core.so

$(LDIR)/socket/core.so: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) luasocket 3.0rc1-2

# == LuaJSON ==
LuaJSON: $(CDIR)/json.lua

$(CDIR)/json.lua: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) lpeg 1.0.2-1
	$(LUA_ROCKS_INSTALL_MODULE) luajson 1.3.4-1

# == Lua_stdlib ==
Lua_stdlib: $(CDIR)/std.lua

$(CDIR)/std.lua: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) stdlib 41.2.2-1

# == LuaFileSystem ==
LuaFileSystem: $(LDIR)/lfs.so

$(LDIR)/lfs.so: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) LuaFileSystem 1.7.0-2

# == LuaPath ==
LuaPath: $(CDIR)/path.lua

$(CDIR)/path.lua: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) lua-path 0.3.1-1

# == LuaSec ==
LuaSec: $(CDIR)/ssl.lua

$(CDIR)/ssl.lua: $(LUA_ROCKS)
	$(LUA_ROCKS_INSTALL_MODULE) LuaSec 0.8-1

clean:
	rm -rf install
	rm -rf upstream

.PHONY: module clean $(LUA_MODULES)
.NOTPARALLEL: $(LUA_MODULES)

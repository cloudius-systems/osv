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

# List of Lua modules, each LuaModule has its own target
LUA_MODULES=LuaSocket LPeg LuaJSON
LDIR=$(OUT)/lib/lua/$(LUA_V)
CDIR=$(OUT)/share/lua/$(LUA_V)

.PHONY: module clean $(LUA_MODULES)

module: $(MAIN) $(LUA_MODULES)

$(MAIN): $(SRCS)
	cd src && $(MAKE) liblua.so

$(DOWNLOAD) $(BUILD) $(LDIR) $(CDIR):
	@mkdir -p $@

$(LUA_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
	 curl --remote-name --remote-time $(LUA_DOWNLOAD)

$(SRCS): $(LUA_ARCHIVE)
	tar -Oxzf $< $(LUA_FOLDER)/$@ > $@

# == LuaSocket ==
LuaSocket_V=3.0-rc1
LuaSocket_F=luasocket-$(LuaSocket_V)
LuaSocket_A=$(LuaSocket_F).tar.gz
LuaSocket_D=https://github.com/diegonehab/luasocket/archive/v$(LuaSocket_V).tar.gz

LuaSocket: $(LDIR)/socket/core.so

$(LDIR)/socket/core.so: $(BUILD)/$(LuaSocket_F)/src/socket.so.$(LuaSocket_V)
	cd $(BUILD)/$(LuaSocket_F) && \
		$(MAKE) LUAV=$(LUA_V) INSTALL_TOP=../../../$(OUT) install

$(BUILD)/$(LuaSocket_F)/src/socket.so.$(LuaSocket_V): $(BUILD)/$(LuaSocket_F)/makefile $(SRCS)
	cd $(BUILD)/$(LuaSocket_F) && \
		$(MAKE) LUAINC_linux=../../../src
	touch $@

$(BUILD)/$(LuaSocket_F)/makefile: $(DOWNLOAD)/$(LuaSocket_A) | $(BUILD)
	tar --directory $(BUILD) -xzf $^
	touch $@

$(DOWNLOAD)/$(LuaSocket_A): | $(DOWNLOAD)
	curl --remote-time --location $(LuaSocket_D) > $@

# == LPeg ==
LPeg_V=0.12
LPeg_F=lpeg-$(LPeg_V)
LPeg_A=$(LPeg_F).tar.gz
LPeg_D=http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-0.12.tar.gz

LPeg: $(LDIR)/lpeg.so

$(LDIR)/lpeg.so: $(BUILD)/$(LPeg_F)/lpeg.so | $(LDIR)
	cp $< $@

$(BUILD)/$(LPeg_F)/lpeg.so: $(BUILD)/$(LPeg_F)/makefile $(SRCS)
	cd $(BUILD)/$(LPeg_F) && \
		$(MAKE) LUADIR=../../src COPT="-O2 -fPIC"

$(BUILD)/$(LPeg_F)/makefile: $(DOWNLOAD)/$(LPeg_A) | $(BUILD)
	tar --directory $(BUILD) -xzf $<
	touch $@

$(DOWNLOAD)/$(LPeg_A): | $(DOWNLOAD)
	curl --remote-time --location $(LPeg_D) > $@

# == LuaJSON ==
LuaJSON_V=1.3.3
LuaJSON_F=luajson-$(LuaJSON_V)
LuaJSON_A=$(LuaJSON_F).tar.gz
LuaJSON_D=https://github.com/harningt/luajson/archive/$(LuaJSON_V).tar.gz

LuaJSON: $(CDIR)/json.lua

$(CDIR)/json.lua: $(BUILD)/$(LuaJSON_F)/lua/json.lua
	cd $(BUILD)/$(LuaJSON_F) && \
		$(MAKE) INSTALL_LMOD=../../$(CDIR) install

$(BUILD)/$(LuaJSON_F)/lua/json.lua: $(DOWNLOAD)/$(LuaJSON_A) | $(BUILD)
	tar --directory $(BUILD) -xzf $<
	touch $@

$(DOWNLOAD)/$(LuaJSON_A): | $(DOWNLOAD)
	curl --remote-time --location $(LuaJSON_D) > $@

##
clean:
	rm -f $(SRCS)
	rm -rf $(DOWNLOAD) $(BUILD) $(OUT)
	cd src && $(MAKE) clean

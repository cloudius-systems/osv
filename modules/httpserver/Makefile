
INCLUDES = -isystem $(miscbase)/usr/include
INCLUDES += -I../../include -I. -I../../java -I../../arch/$(ARCH) -I../..

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend)
src = $(shell readlink -f ../..)

ifndef ARCH
	ARCH = x64
endif

ifndef mode
	mode = release
endif

ifndef OSV_BUILD_PATH
	OSV_BUILD_PATH = $(src)/build/$(mode).$(ARCH)
endif
miscbase = $(src)/external/$(ARCH)/misc.bin
libs-dir = $(miscbase)/usr/lib64
boost-libs := $(libs-dir)/libboost_system-mt.so \
              $(libs-dir)/libboost_filesystem-mt.so

# the build target executable:
TARGET = httpserver
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc) $(wildcard json/*.cc) $(wildcard api/*.cc)
STUB_CPP_FILES := $(wildcard stub/*.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
STUB_FILES := $(addprefix obj/,$(STUB_CPP_FILES:.cc=.o))

STATIC_LIBS = $(libs-dir)/libboost_program_options-mt.a
DYN_LIBS = -lpthread -ldl -L$(OSV_BUILD_PATH)/tools -ltools -L$(libs-dir) -lyaml-cpp $(boost-libs)

ifeq ($(use_host),y)
	DYN_LIBS += -lssl -lcrypto
else
	DYN_LIBS += $(libs-dir)/libssl.so.10 \
		$(libs-dir)/libcrypto.so.10
endif

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all gui swagger-ui-lib

all: init lib$(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)
	$(call very-quiet, mkdir -p obj/stub)
	$(call very-quiet, mkdir -p obj/json)
	$(call very-quiet, mkdir -p obj/api)
	$(call very-quiet, mkdir -p obj/autogen)
	$(call very-quiet, mkdir -p autogen)
.PHONY: init

stub-lib: $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET)-stub.so $^, LINK $@)

$(TARGET): $(OBJ_FILES) $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS), LINK $@)

lib$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% json2code.py
	$(call quiet,./json2code.py -f $< -ns json, GEN $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) $(TARGET), CLEAN)
	$(call very-quiet, $(RM) lib$(TARGET).so)
	$(call very-quiet, $(RM) -rf obj/*)
	$(call very-quiet, $(RM) -rf autogen/*)

check:
	# Test plain HTTP
	cd $(src) && \
	make image=httpserver.fg,mgmt.shell && \
	PYTHONPATH=$(src)/scripts modules/httpserver/tests/testhttpserver.py

	# Test SSL
	cd $(src) && \
	make image=httpserver.fg_ssl,mgmt.shell,certs && \
	PYTHONPATH=$(src)/scripts modules/httpserver/tests/testhttpserver.py \
		--cert modules/certs/build/client.pem \
		--key modules/certs/build/client.key \
		--cacert modules/certs/build/cacert.pem

swagger-ui-lib: swagger-ui/.git
.PHONY: swagger-ui-lib

swagger-ui/.git:
	cd $(src) && git submodule update --init -f modules/httpserver/swagger-ui

gui: osv-gui/.git
	$(call quiet,make -C osv-gui)
.PHONY: gui

osv-gui/.git:
	cd $(src) && git submodule update --init -f modules/httpserver/osv-gui

.SECONDARY:

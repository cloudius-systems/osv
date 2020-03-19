SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk
INCLUDES += -I. -I../httpserver-api -I $(SRC)/libc/internal

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend) -DMONITORING -fvisibility=hidden
#TODO: Figure out why lto breaks exception handling
#CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend) -DMONITORING -fvisibility=hidden -flto
#LDFLAGS = -flto
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
boost-libs := -lboost_system -lboost_filesystem

# the build target executable:
TARGET = httpserver-api
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(addprefix json/,$(notdir $(wildcard ../httpserver-api/json/*.cc))) \
	api/fs.cc api/os.cc api/network.cc api/hardware.cc api/env.cc api/file.cc api/api.cc \
	common.cc main.cc plain_server.cc server.cc connection.cc matcher.cc \
	reply.cc connection_manager.cc mime_types.cc request_handler.cc \
	transformers.cc global_server.cc request_parser.cc handlers.cc \
	path_holder.cc routes.cc $(JSON_CC_FILES)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))

DYN_LIBS = -lpthread -ldl -L../libtools -ltools $(boost-libs)

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all

all: lib$(TARGET).so
	$(call very-quiet, $(SRC)/scripts/manifest_from_host.sh lib$(TARGET).so > usr.manifest)

lib$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% ../httpserver-api/json2code.py
	$(call very-quiet, mkdir -p autogen)
	$(call quiet,../httpserver-api/json2code.py -outdir autogen -f $< -ns json, GEN $@)

$(OBJ_FILES): obj/%.o: ../httpserver-api/%.cc
	$(call very-quiet, mkdir -p obj/stub obj/json obj/api obj/autogen)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(TARGET), CLEAN)
	$(call very-quiet, $(RM) -f lib*.so)
	$(call very-quiet, $(RM) -rf obj)
	$(call very-quiet, $(RM) -rf autogen)
	$(call very-quiet, $(RM) -f *usr*.manifest)

check:
	# Test plain readonly HTTP
	cd $(src) && \
	make image=httpserver-monitoring-api.fg && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-monitoring-api.py

.PHONY: check

.SECONDARY:

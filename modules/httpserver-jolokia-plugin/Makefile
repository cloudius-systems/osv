SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk
INCLUDES += -I. -I../httpserver-api

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend)
src = $(shell readlink -f ../..)

CODE_GEN_FROM_JSON := ../httpserver-api/json2code.py
RM := /bin/rm

ifndef ARCH
	ARCH = x64
endif

ifndef mode
	mode = release
endif

ifndef OSV_BUILD_PATH
	OSV_BUILD_PATH = $(src)/build/$(mode).$(ARCH)
endif

# the build target executable:
TARGET = jolokia
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))

# link with -mt if present, else the base version (and hope it is multithreaded)
boost-mt := -mt
boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
ifeq ($(filter /%,$(boost-lib-dir)),)
    boost-mt :=
    boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
    ifeq ($(filter /%,$(boost-lib-dir)),)
        $(error Error: libboost_system.a needs to be installed.)
    endif
endif

STATIC_LIBS = $(boost-lib-dir)/libboost_program_options$(boost-mt).a
DYN_LIBS = -lpthread -ldl -L$(libs-dir) -lyaml-cpp $(boost-libs)

DYN_LIBS += -lssl -lcrypto

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all

all: $(TARGET).so
	cd jolokia-agent && mvn package -DskipTests=true

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)
	$(call very-quiet, mkdir -p obj/json)
	$(call very-quiet, mkdir -p obj/api)
	$(call very-quiet, mkdir -p obj/autogen)
	$(call very-quiet, mkdir -p autogen)
.PHONY: init

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% $(CODE_GEN_FROM_JSON) | init
	$(call quiet,./$(CODE_GEN_FROM_JSON) -f $< -ns json, GEN $@)

obj/%.o: %.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

# jolokia.cc depends on autogen/jolokia.json.hh, which needs to be
# auto-generated before jolokia.cc is compiled
obj/jolokia.o: autogen/jolokia.json.cc

clean:
	$(call quiet, $(RM) -f $(TARGET).so, CLEAN)
	$(call very-quiet, $(RM) -f usr.manifest)
	$(call very-quiet, $(RM) -rf obj)
	$(call very-quiet, $(RM) -rf autogen)
	cd jolokia-agent && mvn clean
	-rm -f dependency-reduced-pom.xml


INCLUDES = -I$(src)/build/$(mode)/gen/include
INCLUDES += -I../../include -I. -I../../java -I../../arch/$(ARCH) -I../..
INCLUDES += -I$(jdkbase)/include -I$(jdkbase)/include/linux
INCLUDES += -I../httpserver-api

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
TARGET = jvm
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all

all: init $(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)
	$(call very-quiet, mkdir -p obj/json)
	$(call very-quiet, mkdir -p obj/api)
	$(call very-quiet, mkdir -p obj/autogen)
	$(call very-quiet, mkdir -p autogen)
.PHONY: init

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% $(CODE_GEN_FROM_JSON)
	$(call quiet,./$(CODE_GEN_FROM_JSON) -f $< -ns json, GEN $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(TARGET).so, CLEAN)
	$(call very-quiet, $(RM) -f usr.manifest)
	$(call very-quiet, $(RM) -rf obj/*)
	$(call very-quiet, $(RM) -rf autogen/*)

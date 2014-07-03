
INCLUDES = -isystem $(miscbase)/usr/include
INCLUDES += -I../../include -I. -I../../java -I../../arch/$(arch) -I../..

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend)
src = ../..
arch = x64
miscbase = $(src)/external/$(arch)/misc.bin
boost-lib-dir = $(miscbase)/usr/lib64
boost-libs := $(boost-lib-dir)/libboost_system-mt.so \
              $(boost-lib-dir)/libboost_filesystem-mt.so

# the build target executable:
TARGET = httpserver
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc) $(wildcard json/*.cc) $(wildcard api/*.cc)
STUB_CPP_FILES := $(wildcard stub/*.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
STUB_FILES := $(addprefix obj/,$(STUB_CPP_FILES:.cc=.o))

LIBS = -lpthread -lboost_program_options-mt $(boost-libs)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all swagger-ui-lib

all: init lib$(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)
	$(call very-quiet, mkdir -p obj/stub)
	$(call very-quiet, mkdir -p obj/json)
	$(call very-quiet, mkdir -p obj/api)
	$(call very-quiet, mkdir -p obj/autogen)
	$(call very-quiet, mkdir -p autogen)

stub-lib: $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET)-stub.so $^, LINK $@)

$(TARGET): $(OBJ_FILES) $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $(TARGET) $^ $(LIBS), LINK $@)

lib$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o lib$(TARGET).so $^, LINK $@)

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
	cd ../.. && modules/httpserver/tests/testhttpserver.py

swagger-ui-lib: swagger-ui/.git
.PHONY: swagger-ui-lib

swagger-ui/.git:
	cd .. && git submodule update --init -f httpserver/swagger-ui

.SECONDARY:

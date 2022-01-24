include ../common.gmk

module_out := $(out)/modules/httpserver-jolokia-plugin
INCLUDES += -I. -I../httpserver-api

jdkbase = $(dir $(shell readlink -f $$(which javac)))/..
INCLUDES += -I$(jdkbase)/include -I$(jdkbase)/include/linux

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CXXFLAGS  = -g -Wall -std=c++11 -fPIC $(COMMON) -O2

CODE_GEN_FROM_JSON := ../httpserver-api/json2code.py

# the build target executable:
TARGET = jolokia
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc)
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))

DYN_LIBS = -lpthread -ldl -lyaml-cpp
DYN_LIBS += -lssl -lcrypto

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

DEPS := $(OBJ_FILES:.o=.d)

module: all
	echo '/usr/mgmt/plugins/jolokia.so: ./modules/httpserver-jolokia-plugin/jolokia.so' > usr.manifest

all: $(module_out)/$(TARGET).so
	cd jolokia-agent && mvn -q package -DskipTests=true

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p $(module_out)/json)
	$(call very-quiet, mkdir -p $(module_out)/api)
	$(call very-quiet, mkdir -p $(module_out)/autogen)
	$(call very-quiet, mkdir -p autogen)
.PHONY: init

$(module_out)/$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% $(CODE_GEN_FROM_JSON) | init
	$(call quiet,./$(CODE_GEN_FROM_JSON) -f $< -ns json, GEN $@)

$(module_out)/%.o: %.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

# jolokia.cc depends on autogen/jolokia.json.hh, which needs to be
# auto-generated before jolokia.cc is compiled
$(module_out)/jolokia.o: autogen/jolokia.json.cc

clean:
	$(call quiet, $(RM) -f $(module_out)/$(TARGET).so, CLEAN)
	$(call very-quiet, $(RM) -f usr.manifest)
	$(call very-quiet, $(RM) -rf $(module_out))
	$(call very-quiet, $(RM) -rf autogen)
	cd jolokia-agent && mvn -q clean
	-rm -f dependency-reduced-pom.xml

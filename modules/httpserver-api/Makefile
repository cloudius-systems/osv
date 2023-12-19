include ../common.gmk

module_out := $(out)/modules/httpserver-api

INCLUDES += -I. -I $(src)/libc/internal

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CXXFLAGS  = -g -Wall -std=$(conf_cxx_level) -fPIC $(COMMON) -O2

boost-libs := -lboost_system -lboost_filesystem

# the build target executable:
TARGET = httpserver-api
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc) $(wildcard json/*.cc)
STUB_CPP_FILES := $(wildcard stub/*.cc)
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))
STUB_FILES := $(addprefix $(module_out)/,$(STUB_CPP_FILES:.cc=.o))

DYN_LIBS = -lpthread -ldl -L$(out)/modules/libtools -ltools $(boost-libs) -lyaml-cpp

DYN_LIBS += -lssl -lcrypto

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

DEPS := $(OBJ_FILES:.o=.d)

module: all

all: $(module_out)/lib$(TARGET).so api_api api_app api_env api_file api_fs api_hardware api_network api_os api_trace
	$(call quiet, cat _usr_*.manifest | sort | uniq > usr.manifest, CREATE_MANIFEST)
	$(call very-quiet, $(src)/scripts/manifest_from_host.sh $(module_out)/lib$(TARGET).so >> usr.manifest)

add_api_to_manifest = \
	echo "/usr/mgmt/plugins/libhttpserver-$(1).so: $(module_out)/libhttpserver-$(1).so" > _usr_$(1).manifest

add_boost_dependencies_to_manifest = \
	ldd $(module_out)/libhttpserver-$(1).so | grep boost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' > _usr_$(1)_boost.manifest

api_%: $(module_out)/libhttpserver-api_%.so
	$(call quiet, $(call add_api_to_manifest,$@), APPEND_TO_MANIFEST $@)
	$(call very-quiet, $(call add_boost_dependencies_to_manifest,$@))

stub-lib: $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(module_out)/$(TARGET)-stub.so $^, LINK $@)

$(TARGET): $(OBJ_FILES) $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS), LINK $@)

$(module_out)/lib$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)
	$(call very-quiet, ldd $@ | grep boost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' > _usr_$(TARGET).manifest)

$(module_out)/libhttpserver-api_%.so: $(module_out)/autogen/%.json.o $(module_out)/api/%.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% json2code.py
	$(call very-quiet, mkdir -p autogen)
	$(call quiet,./json2code.py -f $< -ns json, GEN $@)

init_obj = \
	$(call very-quiet, mkdir -p $(module_out)/stub) \
	$(call very-quiet, mkdir -p $(module_out)/json) \
	$(call very-quiet, mkdir -p $(module_out)/api) \
	$(call very-quiet, mkdir -p $(module_out)/autogen)

$(module_out)/%.o: %.cc
	$(call init_obj)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(TARGET), CLEAN)
	$(call very-quiet, $(RM) -rf $(module_out))
	$(call very-quiet, $(RM) -rf autogen)
	$(call very-quiet, $(RM) -f *usr*.manifest)

check: check-http check-ssl

check-http:
	# Test plain HTTP
	cd $(src) && \
	make image=httpserver-api,openjdk8-from-host,jetty && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-api.py

check-ssl:
	# Test SSL
	cd $(src) && \
	make image=httpserver-api.daemon_ssl,certs,openjdk8-from-host,jetty && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-api.py \
		--cert modules/certs/build/client.pem \
		--key modules/certs/build/client.key \
		--cacert modules/certs/build/cacert.pem

.PHONY: check check-http check-ssl

.SECONDARY:

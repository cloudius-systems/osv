INCLUDES = -I. -I../../include -I../../arch/$(ARCH) -I../.. \
	-I../../build/$(mode)/gen/include

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
boost-libs := -lboost_system -lboost_filesystem -lboost_regex

# the build target executable:
TARGET = httpserver-api
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
CPP_FILES := $(JSON_CC_FILES) $(wildcard *.cc) $(wildcard json/*.cc)
STUB_CPP_FILES := $(wildcard stub/*.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
STUB_FILES := $(addprefix obj/,$(STUB_CPP_FILES:.cc=.o))

DYN_LIBS = -lpthread -ldl -L../libtools -ltools $(boost-libs) -lyaml-cpp

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

module: all

all: lib$(TARGET).so api_api api_app api_env api_file api_fs api_hardware api_network api_os api_trace
	$(call quiet, cat _usr_*.manifest | sort | uniq > usr.manifest, CREATE_MANIFEST)

add_api_to_manifest = \
	echo "/usr/mgmt/plugins/libhttpserver-$(1).so: `realpath libhttpserver-$(1).so`" > _usr_$(1).manifest

add_boost_dependencies_to_manifest = \
	ldd libhttpserver-$(1).so | grep boost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' > _usr_$(1)_boost.manifest

api_%: libhttpserver-api_%.so
	$(call quiet, $(call add_api_to_manifest,$@), APPEND_TO_MANIFEST $@)
	$(call very-quiet, $(call add_boost_dependencies_to_manifest,$@))

stub-lib: $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET)-stub.so $^, LINK $@)

$(TARGET): $(OBJ_FILES) $(STUB_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS), LINK $@)

lib$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)
	$(call very-quiet, ldd $@ | grep boost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' > _usr_$(TARGET).manifest)

libhttpserver-api_%.so: obj/autogen/%.json.o obj/api/%.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% json2code.py
	$(call very-quiet, mkdir -p autogen)
	$(call quiet,./json2code.py -f $< -ns json, GEN $@)

init_obj = \
	$(call very-quiet, mkdir -p obj/stub) \
	$(call very-quiet, mkdir -p obj/json) \
	$(call very-quiet, mkdir -p obj/api) \
	$(call very-quiet, mkdir -p obj/autogen)

obj/%.o: %.cc
	$(call init_obj)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) $(TARGET), CLEAN)
	$(call very-quiet, $(RM) lib*.so)
	$(call very-quiet, $(RM) -rf obj)
	$(call very-quiet, $(RM) -rf autogen)
	$(call very-quiet, $(RM) -f *usr*.manifest)

check: check-http check-ssl

check-http:
	# Test plain HTTP
	cd $(src) && \
	make image=httpserver-api,jetty && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-api.py

check-ssl:
	# Test SSL
	cd $(src) && \
	make image=httpserver-api.fg_ssl,certs,jetty && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-api.py \
		--cert modules/certs/build/client.pem \
		--key modules/certs/build/client.key \
		--cacert modules/certs/build/cacert.pem

.PHONY: check check-http check-ssl

.SECONDARY:

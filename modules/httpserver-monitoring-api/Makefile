include ../common.gmk

module_out := $(out)/modules/httpserver-monitoring-api

INCLUDES += -I. -I../httpserver-api -I $(src)/libc/internal

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CXXFLAGS  = -g -Wall -std=$(conf_cxx_level) -fPIC $(COMMON) -O2 -DMONITORING -fvisibility=hidden
#TODO: Figure out why lto breaks exception handling
#CXXFLAGS  = -g -Wall -std=$(conf_cxx_level) -fPIC $(COMMON) -O2 -DMONITORING -fvisibility=hidden -flto
#LDFLAGS = -flto

boost-libs := -lboost_system -lboost_filesystem

# the build target executable:
TARGET = httpserver-api
JSON_FILES := $(wildcard api-doc/listings/*.json)
JSON_CC_FILES := $(subst .json,.json.cc,$(subst api-doc/listings/,autogen/,$(JSON_FILES)))
JSON_OBJ_FILES := $(addprefix $(module_out)/,$(JSON_CC_FILES:.cc=.o))

API_CC_FILES := $(addprefix api/,fs.cc os.cc network.cc hardware.cc env.cc file.cc api.cc)
SERVER_CC_FILES := common.cc main.cc plain_server.cc server.cc connection.cc matcher.cc \
	reply.cc connection_manager.cc mime_types.cc request_handler.cc \
	transformers.cc global_server.cc request_parser.cc handlers.cc \
	path_holder.cc routes.cc
CPP_FILES := $(addprefix json/,$(notdir $(wildcard ../httpserver-api/json/*.cc))) \
	$(API_CC_FILES) $(SERVER_CC_FILES)
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))

DYN_LIBS = -lpthread -ldl -L$(out)/modules/libtools -ltools $(boost-libs)

LIBS = $(DYN_LIBS) $(STATIC_LIBS)

DEPS := $(OBJ_FILES:.o=.d)

module: all

all: $(module_out)/lib$(TARGET).so
	@if [ "$(CROSS_PREFIX)" = "aarch64-linux-gnu-" ]; then \
		echo "/usr/lib/lib$(TARGET).so: $(module_out)/lib$(TARGET).so" > usr.manifest; \
		$(src)/modules/httpserver-monitoring-api/add_aarch64_libraries.sh $(OSV_BASE) >> usr.manifest; \
	else \
		$(src)/scripts/manifest_from_host.sh $(module_out)/lib$(TARGET).so > usr.manifest; \
	fi

$(module_out)/lib$(TARGET).so: $(JSON_OBJ_FILES) $(OBJ_FILES) $(module_out)/options.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(STATIC_LIBS) -o $@ $^ $(DYN_LIBS), LINK $@)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

autogen/%.cc: api-doc/listings/% ../httpserver-api/json2code.py
	$(call very-quiet, mkdir -p autogen)
	$(call quiet,../httpserver-api/json2code.py -outdir autogen -f $< -ns json, GEN $@)

$(OBJ_FILES): $(module_out)/%.o: ../httpserver-api/%.cc
	$(call very-quiet, mkdir -p $(module_out)/json $(module_out)/api)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

$(JSON_OBJ_FILES): $(module_out)/autogen/%.o: autogen/%.cc
	$(call very-quiet, mkdir -p $(module_out)/autogen)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

$(module_out)/options.o: $(src)/core/options.cc
	$(call very-quiet, mkdir -p $(module_out))
	$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(TARGET), CLEAN)
	$(call very-quiet, $(RM) -rf $(module_out))
	$(call very-quiet, $(RM) -rf autogen)
	$(call very-quiet, $(RM) -f *usr*.manifest)

check:
	# Test plain readonly HTTP
	cd $(src) && \
	make image=httpserver-monitoring-api.fg && \
	PYTHONPATH=$(src)/scripts modules/httpserver-api/tests/testhttpserver-monitoring-api.py

.PHONY: check

.SECONDARY:

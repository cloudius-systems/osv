include ../common.gmk

module_out := $(out)/modules/cloud-init

CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(COMMON)

boost-libs := -lboost_system -lboost_filesystem

HTTPSERVER_API_DIR =  $(out)/modules/httpserver-api
INCLUDES += -I../httpserver-api

# the build target executable:
TARGET = cloud-init
CPP_FILES := client.cc cloud-init.cc data-source.cc main.cc template.cc cassandra-module.cc json.cc
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

STUB_HTTPSERVER_LIBS = $(HTTPSERVER_API_DIR)/httpserver-stub.so
LIBS = -lpthread $(boost-libs) $(DEPEDNDS_LIBS) -lyaml-cpp -L$(HTTPSERVER_API_DIR)/ -lhttpserver-api

module: all

all: init $(module_out)/$(TARGET).so $(module_out)/tst-template

init:
	$(call very-quiet, mkdir -p $(module_out))

$(module_out)/tst-template: template.cc tst-template.cc
	$(call quiet, $(CXX) -g -Wall -std=c++11 $(LDFLAGS) -o $@ $^ -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK, LINK $@)

$(module_out)/$(TARGET): $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) $(module_out)/-o $(TARGET) $^ $(LIBS) $(STUB_HTTPSERVER_LIBS), LINK $@)

$(module_out)/$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) $(STATIC_LIBS) -shared -o $(module_out)/$(TARGET).so $^ $(LIBS), LINK $@)
	echo '/usr/mgmt/cloud-init.so: ./modules/cloud-init/cloud-init.so' > usr.manifest

$(module_out)/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(module_out)/$(TARGET).so $(module_out)/tst-template, CLEAN)
	$(call very-quiet, $(RM) -rf $(module_out))

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

.SECONDARY:

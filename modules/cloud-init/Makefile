SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk

autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) $(autodepend)
src = $(SRC)
arch = x64

boost-libs := -lboost_system -lboost_filesystem

HTTPSERVER_API_DIR = ../../modules/httpserver-api
INCLUDES += -I$(HTTPSERVER_API_DIR)

# the build target executable:
TARGET = cloud-init
CPP_FILES := client.cc cloud-init.cc data-source.cc main.cc template.cc cassandra-module.cc json.cc
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

STUB_HTTPSERVER_LIBS = $(HTTPSERVER_API_DIR)/httpserver-stub.so
LIBS = -lpthread $(boost-libs) $(DEPEDNDS_LIBS) -lyaml-cpp -L$(HTTPSERVER_API_DIR)/ -lhttpserver-api

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)


module: all

all: init $(TARGET).so tst-template

init:
	$(call very-quiet, mkdir -p obj)

tst-template: template.cc tst-template.cc
	$(call quiet, $(CXX) -g -Wall -std=c++11 -o $@ $^ -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK, LINK $@)

$(TARGET): $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $(TARGET) $^ $(LIBS) $(STUB_HTTPSERVER_LIBS), LINK $@)

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(STATIC_LIBS) -shared -o $(TARGET).so $^ $(LIBS), LINK $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) $(TARGET), CLEAN)
	$(call very-quiet, $(RM) $(TARGET).so)
	$(call very-quiet, $(RM) -rf obj/*)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

.SECONDARY:

miscbase = $(src)/external/$(arch)/misc.bin
INCLUDES = -isystem $(miscbase)/usr/include
INCLUDES += -I../../include -I.

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) $(autodepend)
src = ../..
arch = x64

boost-lib-dir = $(miscbase)/usr/lib64
boost-libs := $(boost-lib-dir)/libboost_system.so \
              $(boost-lib-dir)/libboost_filesystem.so


HTTPSERVER_DIR = ../../modules/httpserver
INCLUDES += -I$(HTTPSERVER_DIR)

# the build target executable:
TARGET = cloud-init
CPP_FILES := $(wildcard *.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

STUB_HTTPSERVER_LIBS = $(HTTPSERVER_DIR)/httpserver-stub.so
LIBS = -lpthread $(boost-libs) $(DEPEDNDS_LIBS) -lyaml-cpp -L$(HTTPSERVER_DIR)/ -lhttpserver -L$(boost-lib-dir)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)


module: all

all: init $(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)

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

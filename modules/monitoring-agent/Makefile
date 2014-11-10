miscbase = $(src)/external/$(ARCH)/misc.bin

INCLUDES = -isystem $(miscbase)/usr/include -I$(src)/build/$(mode)/gen/include
INCLUDES += -I../../include -I. -I../../java -I../../arch/$(ARCH) -I../..
INCLUDES += -I$(jdkbase)/include -I$(jdkbase)/include/linux


# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) $(autodepend)
src = ../..


ifndef ARCH
	ARCH = x64
endif

ifndef mode
	mode = release
endif

boost-lib-dir = $(miscbase)/usr/lib64
boost-libs := $(boost-lib-dir)/libboost_system.so \
              $(boost-lib-dir)/libboost_filesystem.so


HTTPSERVER_DIR = ../../modules/httpserver
INCLUDES += -I$(HTTPSERVER_DIR)

# the build target executable:
TARGET = monitoring-agent
CPP_FILES := main.cc monitor-agent.cc client.cc
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

LIBS = -lpthread $(boost-libs) -lyaml-cpp $(DEPEDNDS_LIBS) -L$(boost-lib-dir)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)


module: all

all: init $(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)

$(TARGET): $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $(TARGET) $^ $(LIBS) $(boost-lib-dir)/libboost_program_options.a, LINK $@)

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

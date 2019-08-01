SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk
INCLUDES += -I.

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) $(autodepend)

ifndef ARCH
	ARCH = x64
endif

ifndef mode
	mode = release
endif

boost-libs := -lboost_system -lboost_filesystem

HTTPSERVER_DIR = $(src)/modules/httpserver
INCLUDES += -I$(HTTPSERVER_DIR)

# the build target executable:
TARGET = monitoring-agent
CPP_FILES := main.cc monitor-agent.cc client.cc
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

LIBS = -lpthread $(boost-libs) -lyaml-cpp $(DEPEDNDS_LIBS)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)


module: all

all: init $(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET).so $^ $(LIBS), LINK $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) -f $(TARGET).so, CLEAN)
	$(call very-quiet, $(RM) -rf obj/*)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

.SECONDARY:

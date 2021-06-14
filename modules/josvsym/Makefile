include ../common.gmk

module_out := $(out)/modules/josvsym

jdkbase = $(dir $(shell readlink -f $$(which javac)))/..
INCLUDES += -I$(jdkbase)/include -I$(jdkbase)/include/linux

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CXXFLAGS  	= -g -Wall -std=c++11 -fPIC $(COMMON) -O2

MODULE		= josvsym
CPP_FILES 	:= josvsym.cc
OBJ_FILES 	:= $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))
DEPS 		:= $(OBJ_FILES:.o=.d)
TARGET		:= $(module_out)/lib$(MODULE).so

module		: all

all		: $(TARGET)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

init	:
		@echo "  MKDIRS"
		$(call very-quiet, mkdir -p $(module_out))

$(TARGET)	: $(OBJ_FILES) $(STUB_FILES)
		$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LIBS), LINK $(@F))



$(module_out)/%.o	: %.cc | init
		$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean		:
		$(call quiet, $(RM) -rf $(module_out), CLEAN)

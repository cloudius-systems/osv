SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk
INCLUDES += -I.

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
autodepend 	= -MD -MT $@ -MP
CXXFLAGS  	= -g -Wall -std=c++11 -fPIC $(INCLUDES) -O2 $(autodepend)

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

MODULE		= josvsym
OUTDIR		:= $(OSV_BUILD_PATH)/modules/$(MODULE)
CPP_FILES 	:= josvsym.cc
OBJ_FILES 	:= $(addprefix $(OUTDIR)/,$(CPP_FILES:.cc=.o))
DEPS 		:= $(OBJ_FILES:.o=.d)
TARGET		:= $(OUTDIR)/lib$(MODULE).so
LIBS		=

quiet 		= $(if $V, $1, @echo " $2"; $1)
very-quiet 	= $(if $V, $1, @$1)

module		: all

all		: $(OUTDIR) $(TARGET)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

$(OUTDIR)	:
		$(call very-quiet, mkdir -p $(OUTDIR))

.PHONY		: all module

$(TARGET)	: $(OBJ_FILES) $(STUB_FILES)
		$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LIBS), LINK $(@F))



$(OUTDIR)/%.o	: %.cc
		$(call quiet, $(CXX) $(CXXFLAGS) -c -MMD  -o $@ $<, CXX $@)

clean		:
		$(call quiet, $(RM) -f $(TARGET), CLEAN)
		$(call very-quiet, $(RM) -rf $(OUTDIR/*.o))
		$(call very-quiet, $(RM) -rf $(OUTDIR/*.d))

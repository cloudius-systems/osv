SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk

autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) $(autodepend)

# the build target executable:
TARGET = go
CPP_FILES := $(TARGET).cc
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET).so $^ $(LIBS), LINK $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)
.PHONY: init

module: init $(TARGET).so
	echo '/go.so: $${MODULE_DIR}/go.so' > usr.manifest

clean:
	rm -f $(TARGET)*.so usr.manifest
	$(call very-quiet, $(RM) -rf obj)

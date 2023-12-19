include ../common.gmk

module_out := $(out)/modules/golang

CXXFLAGS  = -g -rdynamic -Wall -std=$(conf_cxx_level) -fPIC $(COMMON)

# the build target executable:
TARGET = go
CPP_FILES := $(TARGET).cc
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

$(module_out)/$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $(module_out)/$(TARGET).so $^ $(LIBS), LINK $@)

$(module_out)/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p $(module_out))
.PHONY: init

module: init $(module_out)/$(TARGET).so
	echo '/go.so: ./modules/golang/go.so' > usr.manifest

clean:
	rm -f $(TARGET)*.so usr.manifest
	$(call very-quiet, $(RM) -rf $(module_out))

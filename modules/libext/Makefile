arch=x64
include ../common.gmk

module_out := $(out)/modules/libext

CXXFLAGS = -fPIC -std=gnu++11 $(INCLUDES) -I../lwext4/upstream/lwext4/include -I../lwext4/upstream/lwext4/build_lib_only/include \
	 -D_KERNEL -D_GNU_SOURCE -Wall -fno-exceptions -fno-rtti

# the build target executable:
TARGET = libext
CPP_FILES := ext_vfsops.cc ext_vnops.cc
OBJ_FILES := $(addprefix $(module_out)/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

LIBS = -L../lwext4/upstream/lwext4/build_lib_only/src/ -llwext4

$(module_out)/$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -static-libstdc++ -shared -o $(module_out)/$(TARGET).so $^ $(LIBS), LINK $@)

$(module_out)/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p $(module_out))
.PHONY: init

module: init $(module_out)/$(TARGET).so
	echo '/usr/lib/fs/libext.so: ./modules/libext/libext.so' > usr.manifest

clean:
	rm -f $(TARGET)*.so usr.manifest
	$(call very-quiet, $(RM) -rf $(module_out))

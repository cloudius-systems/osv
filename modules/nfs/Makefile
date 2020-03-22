INCLUDES = -I. -I../libnfs/upstream/libnfs/include -I../../include
INCLUDES += -I../../arch/$(ARCH) -I../.. -I../../build/$(mode)/gen/include
INCLUDES += -isystem ../../include/glibc-compat
#Only for host, not external
INCLUDES += $(shell $(CXX) -E -xc++ - -v </dev/null 2>&1 | awk '/^End/ {exit} /^ .*c\+\+/ {print "-isystem" $$0}')
#
INCLUDES += -isystem ../../include/api -isystem ../../include/api/$(ARCH) -isystem ../../build/$(mode)/gen/include
INCLUDES += -isystem ../../bsd/sys -isystem ../../bsd/ -isystem ../../bsd/$(ARCH)

autodepend = -MD -MT $@ -MP
CXXFLAGS  = -g -rdynamic -Wall -std=c++11 -fPIC $(INCLUDES) -D_KERNEL -D_GNU_SOURCE $(autodepend)

# the build target executable:
TARGET = nfs
CPP_FILES := $(wildcard *.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))
DEPS := $(OBJ_FILES:.o=.d)

LIBS = -L../libnfs/upstream/libnfs/lib -lnfs
ifndef ARCH
	ARCH = x64
endif
ifndef mode
	mode = release
endif

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
	echo '/usr/lib/fs/nfs.so: $${MODULE_DIR}/nfs.so' > usr.manifest

clean:
	rm -f $(TARGET)*.so usr.manifest
	$(call very-quiet, $(RM) -rf obj)

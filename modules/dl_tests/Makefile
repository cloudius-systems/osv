src := $(OSV_BASE)
out := $(OSV_BUILD_PATH)
arch := $(ARCH)
fs_type := $(fs_type)
bionic_test_libs := $(src)/modules/dl_tests/bionic/tests/libs

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)
makedir = $(call very-quiet, mkdir -p $(dir $@))

autodepend = -MD -MT $@ -MP

INCLUDES = -I$(src)/arch/$(ARCH) -I$(src) -I$(src)/include \
	-I$(src)/arch/common -isystem $(src)/include/glibc-compat \
	$(shell $(CXX) -E -xc++ - -v </dev/null 2>&1 | awk '/^End/ {exit} /^ .*c\+\+/ {print "-isystem" $$0}') \
	-isystem $(src)/include/api -isystem $(src)/include/api/$(ARCH) \
	-isystem $(out)/gen/include

COMMON = $(autodepend) $(INCLUDES) -g -O2 -fPIC -DBOOST_TEST_DYN_LINK \
	-U _FORTIFY_SOURCE -D_KERNEL -D__OSV__ -DCONF_debug_memory=0 \
	-Wall -Wno-pointer-arith -Wformat=0 -Wno-format-security

LIBS =

CXXFLAGS = -std=gnu++11 $(COMMON)
CXXFLAGS2 = -Wl,--no-as-needed -L=$(out)/dl_tests -L=/usr/lib -Wl,-rpath . -Wl,-rpath / -Wl,-rpath /usr/lib $(CXXFLAGS)

CFLAGS = -std=gnu99 $(COMMON)

tests := libtest_simple.so libtest_empty.so libtest_dlsym_from_this_grandchild.so \
	libtest_dlsym_from_this_child.so libtest_dlsym_from_this.so libdlext_test.so \
	libtest_with_dependency.so libtest_check_rtld_next_from_library.so

all_tests := $(tests:%=$(out)/dl_tests/%)

module: usr.manifest $(all_tests)

$(out)/dl_tests/libtest_simple.so: $(bionic_test_libs)/dlopen_testlib_simple.cpp
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_simple.so)

$(out)/dl_tests/libtest_empty.so: $(bionic_test_libs)/empty.cpp
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_empty.so)

$(out)/dl_tests/libtest_check_rtld_next_from_library.so: $(bionic_test_libs)/check_rtld_next_from_library.cpp
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_check_rtld_next_from_library.so)

$(out)/dl_tests/libtest_dlsym_from_this_grandchild.so: $(bionic_test_libs)/dlsym_from_this_symbol2.cpp
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_dlsym_from_this_grandchild.so)

$(out)/dl_tests/libtest_dlsym_from_this_child.so: $(bionic_test_libs)/dlsym_from_this_functions.cpp \
	$(out)/dl_tests/libtest_dlsym_from_this_grandchild.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS2) -ltest_dlsym_from_this_grandchild -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_dlsym_from_this_child.so)

$(out)/dl_tests/libtest_dlsym_from_this.so: $(bionic_test_libs)/dlsym_from_this_symbol.cpp \
	$(out)/dl_tests/libtest_dlsym_from_this_child.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS2) -ltest_dlsym_from_this_child -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_dlsym_from_this.so)

$(out)/dl_tests/libdlext_test.so: $(bionic_test_libs)/dlext_test_library.cpp \
	$(out)/dl_tests/libtest_simple.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS2) -ltest_simple -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libdlext_test.so)

$(out)/dl_tests/libtest_with_dependency.so: $(bionic_test_libs)/dlopen_testlib_simple.cpp \
	$(out)/dl_tests/libdlext_test.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS2) -ldlext_test -D__SHARED_OBJECT__=1 -shared -o $@ $<, CXX dl_tests/libtest_with_dependency.so)

usr.manifest:
	@echo "/usr/lib/libtest_simple.so: ./dl_tests/libtest_simple.so" > $@
	@echo "/usr/lib/libtest_empty.so: ./dl_tests/libtest_empty.so" >> $@
	@echo "/usr/lib/libtest_dlsym_from_this_grandchild.so: ./dl_tests/libtest_dlsym_from_this_grandchild.so" >> $@
	@echo "/usr/lib/libtest_dlsym_from_this_child.so: ./dl_tests/libtest_dlsym_from_this_child.so" >> $@
	@echo "/usr/lib/libtest_dlsym_from_this.so: ./dl_tests/libtest_dlsym_from_this.so" >> $@
	@echo "/usr/lib/libdlext_test.so: ./dl_tests/libdlext_test.so" >> $@
	@echo "/usr/lib/libtest_with_dependency.so: ./dl_tests/libtest_with_dependency.so" >> $@
	@echo "/usr/lib/libtest_check_rtld_next_from_library.so: ./dl_tests/libtest_check_rtld_next_from_library.so" >> $@
	@echo "/tests/libtest_simple.so: ./dl_tests/libtest_simple.so" >> $@
	@echo "/tests/libtest_empty.so: ./dl_tests/libtest_empty.so" >> $@
	@echo "/tests/libtest_dlsym_from_this_grandchild.so: ./dl_tests/libtest_dlsym_from_this_grandchild.so" >> $@
	@echo "/tests/libtest_dlsym_from_this_child.so: ./dl_tests/libtest_dlsym_from_this_child.so" >> $@
	@echo "/tests/libtest_dlsym_from_this.so: ./dl_tests/libtest_dlsym_from_this.so" >> $@
	@echo "/tests/libdlext_test.so: ./dl_tests/libdlext_test.so" >> $@
	@echo "/tests/libtest_with_dependency.so: ./dl_tests/libtest_with_dependency.so" >> $@
	@echo "/tests/libtest_check_rtld_next_from_library.so: ./dl_tests/libtest_check_rtld_next_from_library.so" >> $@
.PHONY: FORCE
FORCE:

clean:
	-rm -f usr.manifest

ifneq ($(MAKECMDGOALS),clean)
include $(shell test -d $(out)/dl_tests && find $(out)/dl_tests -name '*.d')
endif

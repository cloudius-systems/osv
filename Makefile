INCLUDES = -I.
CXXFLAGS = -std=gnu++11 -lstdc++ $(CFLAGS) $(do-sys-includes) $(INCLUDES)
CFLAGS = $(autodepend) -g -Wall -Wno-pointer-arith $(INCLUDES)
ASFLAGS = -g $(autodepend.s)

sys-includes = $(jdkbase)/include $(jdkbase)/include/linux
autodepend = -MD $(@.o=.d) -MT $@
autodepend.s = -MD $(@:.o=.d)

do-sys-includes = $(foreach inc, $(sys-includes), -isystem $(inc))

all: loader.bin

loader.bin: arch/x64/boot32.o arch/x64/loader32.ld
	$(LD) -nostartfiles -static -nodefaultlibs -o $@ \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld))

arch/x64/boot32.o: loader.elf

fs = fs/fs.o fs/bootfs.o bootfs.o

drivers = drivers/vga.o drivers/console.o drivers/isa-serial.o
drivers += $(fs)
drivers += mmu.o
drivers += elf.o

objects = exceptions.o
objects += arch/x64/entry.o
objects += mutex.o
objects += pthread.o
objects += mempool.o

libc = libc/string/strcmp.o

libstdc++.a = $(shell $(CXX) -static -print-file-name=libstdc++.a)
libsupc++.a = $(shell $(CXX) -static -print-file-name=libsupc++.a)
libgcc_s.a = $(shell $(CXX) -static -print-libgcc-file-name)

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
        $(objects) dummy-shlib.so \
		$(libc) bootfs.bin
	$(LD) -o $@ \
		-Bdynamic --export-dynamic \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	    $(libstdc++.a) $(libsupc++.a) $(libgcc_s.a) libunwind.a

dummy-shlib.so: dummy-shlib.o
	$(CXX) -nodefaultlibs -shared -o $@ $^

jdk-jni.h := $(shell rpm -ql java-1.7.0-openjdk-devel | grep include/jni.h$$)
jdkbase := $(jdk-jni.h:%/include/jni.h=%)

bootfs.bin: scripts/mkbootfs.py bootfs.manifest
	scripts/mkbootfs.py -o $@ -d $@.d -m bootfs.manifest \
		-D jdkbase=$(jdkbase)

bootfs.o: bootfs.bin

runtime.o: ctype-data.h

ctype-data.h: gen-ctype-data
	./gen-ctype-data > $@

clean:
	find -name '*.[od]' | xargs rm
	rm -f loader.elf loader.bin

-include $(shell find -name '*.d')

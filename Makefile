
CXXFLAGS = -std=gnu++11 -lstdc++ $(CFLAGS)
CFLAGS = $(autodepend) -g -Wall -Wno-pointer-arith

autodepend = -MD $(@.o=.d) -MT $@

all: loader.bin

loader.bin: loader.elf
	objcopy -O elf32-i386 $^ $@

fs = fs/fs.o fs/bootfs.o bootfs.o

drivers = drivers/vga.o drivers/console.o
drivers += $(fs)
drivers += mmu.o
drivers += elf.o

libc = libc/string/strcmp.o

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
		$(libc) bootfs.bin
	$(CXX) $(CXXFLAGS) -nostartfiles -static -nodefaultlibs -o $@ \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	     -lsupc++ libunwind.a -lstdc++

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

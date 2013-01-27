
arch = x64
INCLUDES = -I. -I$(src)/arch/$(arch) -I$(src) -I$(src)/external/libunwind/include -I$(src)/includes
COMMON = $(autodepend) -g -Wall -Wno-pointer-arith -Werror -Wformat=0 \
	-U _FORTIFY_SOURCE -fno-stack-protector $(INCLUDES) \
	$(arch-cflags) $(cflags-$(mode)) -mno-red-zone

CXXFLAGS = -std=gnu++11 -lstdc++ $(do-sys-includes) $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

# should be limited to files under libc/ eventually
CFLAGS += -I $(src)/libc/internal -I  $(src)/libc/arch/$(arch) \
	-Wno-missing-braces -Wno-parentheses

ASFLAGS = -g $(autodepend)

cflags-debug =
cflags-release = -O2

arch-cflags = -msse4.1


quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

makedir = $(call very-quiet, mkdir -p $(dir $@))
build-cxx = $(CXX) $(CXXFLAGS) -c -o $@ $<
q-build-cxx = $(call quiet, $(build-cxx), CXX $@)
build-c = $(CC) $(CFLAGS) -c -o $@ $<
q-build-c = $(call quiet, $(build-c), CC $@)
build-s = $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<
q-build-s = $(call quiet, $(build-s), AS $@)
build-so = $(CC) $(CFLAGS) -o $@ $^
q-build-so = $(call quiet, $(build-so), CC $@)
	
%.o: %.cc
	$(makedir)
	$(q-build-cxx)

%.o: %.c
	$(makedir)
	$(q-build-c)

%.o: %.S
	$(makedir)
	$(q-build-s)

%.class: %.java
	$(makedir)
	$(call quiet, javac -d $(javabase) -cp $(src)/$(javabase) $^,JAVAC $@)

tests/%.o: CFLAGS += -fPIC

%.so: CFLAGS+=-fPIC -shared
%.so: %.o
	$(makedir)
	$(q-build-so)

sys-includes = $(jdkbase)/include $(jdkbase)/include/linux
sys-includes +=  $(gccbase)/usr/include -I$(glibcbase)/usr/include
autodepend = -MD -MT $@ -MP

do-sys-includes = $(foreach inc, $(sys-includes), -isystem $(inc))

tests := tests/tst-pthread.so tests/tst-ramdisk.so tests/hello/Hello.class
tests += tests/bench/bench.jar

tests/hello/Hello.class: javabase=tests/hello

RunJar.class: javabase=.

tests/tst-pthread.so: tests/tst-pthread.o
tests/tst-ramdisk.so: tests/tst-ramdisk.o

all: loader.img loader.bin

boot.bin: arch/x64/boot16.ld arch/x64/boot16.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

loader.img: boot.bin loader.elf
	$(call quiet, dd if=boot.bin of=$@ > /dev/null 2>&1, DD $@ boot.bin)
	$(call quiet, dd if=loader.elf of=$@ conv=notrunc seek=128 > /dev/null 2>&1, \
		DD $@ loader.elf)
	$(call quiet, $(src)/scripts/imgedit.py setargs $@ java.so Hello, IMGEDIT $@)

loader.bin: arch/x64/boot32.o arch/x64/loader32.ld
	$(call quiet, $(LD) -nostartfiles -static -nodefaultlibs -o $@ \
	                $(filter-out %.bin, $(^:%.ld=-T %.ld)), LD $@)

arch/x64/boot32.o: loader.elf

fs = fs/fs.o bootfs.o

fs +=	fs/vfs/main.o \
	fs/vfs/subr_uio.o \
	fs/vfs/vfs_conf.o \
	fs/vfs/vfs_lookup.o \
	fs/vfs/vfs_mount.o \
	fs/vfs/vfs_vnode.o \
	fs/vfs/vfs_task.o \
	fs/vfs/vfs_syscalls.o

fs +=	fs/ramfs/ramfs_vfsops.o \
	fs/ramfs/ramfs_vnops.o

fs +=	fs/devfs/devfs_vnops.o \
	fs/devfs/device.o

drivers :=
drivers += drivers/console.o drivers/vga.o drivers/isa-serial.o
drivers += drivers/ramdisk.o
drivers += $(fs)
drivers += mmu.o
drivers += elf.o
drivers += drivers/pci-device.o drivers/pci-function.o drivers/pci-bridge.o drivers/device-factory.o
drivers += drivers/driver.o drivers/driver-factory.o
drivers += drivers/virtio.o
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-net.o
drivers += drivers/virtio-blk.o
drivers += drivers/clock.o drivers/kvmclock.o
drivers += drivers/clockevent.o

objects = arch/x64/exceptions.o
objects += arch/x64/entry.o
objects += arch/x64/math.o
objects += arch/x64/apic.o
objects += arch/x64/apic-clock.o
objects += arch/x64/arch-setup.o
objects += mutex.o
objects += debug.o
objects += drivers/pci.o
objects += mempool.o
objects += arch/x64/elf-dl.o
objects += linux.o
objects += sched.o
objects += mmio.o

objects += kern/sglist.o

include $(src)/libc/build.mak

objects += $(addprefix libc/, $(libc))

libstdc++.a = $(shell find $(gccbase) -name libstdc++.a)
libsupc++.a = $(shell find $(gccbase) -name libsupc++.a)
libgcc_s.a = $(shell find $(gccbase) -name libgcc.a |  grep -v /32/)
libgcc_eh.a = $(shell find $(gccbase) -name libgcc_eh.a |  grep -v /32/)

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
        $(objects) dummy-shlib.so \
		bootfs.bin
	$(call quiet, $(LD) -o $@ \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	    $(libstdc++.a) $(libsupc++.a) $(libgcc_s.a) $(libgcc_eh.a) $(src)/libunwind.a, \
		LD $@)

dummy-shlib.so: dummy-shlib.o
	$(call quiet, $(CXX) -nodefaultlibs -shared -o $@ $^, LD $@)

jdkbase := $(shell find $(src)/external/openjdk.bin/usr/lib/jvm \
                         -maxdepth 1 -type d -name 'java*')
glibcbase = $(src)/external/glibc.bin
gccbase = $(src)/external/gcc.bin

java.so: java.o

java.o: CXXFLAGS += -fPIC

bootfs.bin: scripts/mkbootfs.py bootfs.manifest $(tests) java.so RunJar.class
	$(call quiet, $(src)/scripts/mkbootfs.py -o $@ -d $@.d -m $(src)/bootfs.manifest \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase), MKBOOTFS $@)

bootfs.o: bootfs.bin

runtime.o: ctype-data.h

ctype-data.h: gen-ctype-data
	$(call quiet, ./gen-ctype-data > $@, GEN $@)

gen-ctype-data: gen-ctype-data.o
	$(call quiet, $(CXX) -o $@ $^, LD $@)

-include $(shell find -name '*.d')

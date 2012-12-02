
CXXFLAGS = -std=gnu++11 -lstdc++ $(CFLAGS)
CFLAGS = $(autodepend)

autodepend = -MD $(@.o=.d) -MT $@

all: loader.bin

loader.bin: loader.elf
	objcopy -O elf32-i386 $^ $@

fs = fs/fs.o

drivers = drivers/vga.o
drivers += $(fs)

libc = libc/string/strcmp.o

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
		$(libc)
	$(CXX) $(CXXFLAGS) -nostartfiles -static -nodefaultlibs -o $@ \
	    $(^:%.ld=-T %.ld) -lsupc++

clean:
	find -name '*.[od]' | xargs rm
	rm -f loader.elf loader.bin

-include $(shell find -name '*.d')

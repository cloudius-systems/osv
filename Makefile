
CXXFLAGS = -std=gnu++11 -lstdc++ $(CFLAGS)
CFLAGS = $(autodepend)

autodepend = -MD $(@.o=.d) -MT $@

all: loader.bin

loader.bin: loader.elf
	objcopy -O elf32-i386 $^ $@

drivers = drivers/vga.o

libc = libc/string/strcmp.o

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
		$(libc)
	$(CXX) $(CXXFLAGS) -nostartfiles -static -nodefaultlibs -o $@ \
	    $(^:%.ld=-T %.ld) -lsupc++

-include $(shell find -name '*.d')

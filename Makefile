


all: loader.bin

loader.bin: loader.elf
	objcopy -O elf32-i386 $^ $@

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o
	$(CXX) -nostartfiles -static -nodefaultlibs -o $@ $^


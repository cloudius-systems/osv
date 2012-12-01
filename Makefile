


all: loader.bin

loader.bin: loader.elf
	objcopy -O elf32-x86-64 $^ $@

loader.elf: loader.o
	$(CXX) -o $@ $^


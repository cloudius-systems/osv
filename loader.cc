
#include "drivers/vga.hh"
#include "fs/bootfs.hh"

extern void (*debug_write)(const char *msg);

Console *debug_console;

void console_debug_write(const char *msg)
{
    debug_console->writeln(msg);
}

int main(int ac, char **av)
{
    VGAConsole console;

    debug_console = &console;
    debug_write = console_debug_write;
    console.writeln("Loader Copyright 2013 Unnamed");
    bootfs fs;
    file* f = fs.open("/usr/lib/libjvm.so");
    char buf[100];
    f->read(buf, 0, 100);
    console.writeln(buf);
    while (true)
	;
}

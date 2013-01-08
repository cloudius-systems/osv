#include "stdio.hh"
#include "debug.hh"

uint64_t console_file::size()
{
    return 0;
}

void console_file::read(void *buffer, uint64_t offset, uint64_t len)
{
    abort();
}

void console_file::write(const void* buffer, uint64_t offset, uint64_t len)
{
    debug(std::string(reinterpret_cast<const char*>(buffer), len), false);
}

console_file the_console_file;
fileref  __attribute__((init_priority(40000))) console_fileref(&the_console_file);

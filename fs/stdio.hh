#ifndef STDIO_HH_
#define STDIO_HH_

#include "fs.hh"

class console_file : public file {
public:
    virtual uint64_t size();
    virtual void read(void *buffer, uint64_t offset, uint64_t len);
    virtual void write(const void* buffer, uint64_t offset, uint64_t len);
};

extern fileref console_fileref;

#endif /* STDIO_HH_ */

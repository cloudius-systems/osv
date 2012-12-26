#ifndef MEMPOOL_HH
#define MEMPOOL_HH

#include <cstdint>
#include <boost/intrusive/set.hpp>

namespace memory {

using std::size_t;

const size_t page_size = 4096;

void* alloc_page();
void free_page(void* page);
void* alloc_page_range(size_t bytes);
void free_page_range(void* start, size_t bytes);
void setup_free_memory(void* start, size_t bytes);

class pool {
public:
    explicit pool(unsigned size);
    ~pool();
    void* alloc();
    void free(void* object);
    static pool* from_object(void* object);
private:
    struct page_header;
    struct free_object;
private:
    void add_page();
    static page_header* to_header(free_object* object);
private:
    unsigned _size;
    free_object* _free;
public:
    static const size_t max_object_size;
};

struct pool::page_header {
    pool* owner;
    unsigned nalloc;
    free_object* free;
};

struct pool::free_object {
    free_object* next;
    page_header* to_page_header();
};

class malloc_pool : public pool {
public:
    malloc_pool();
private:
    static size_t compute_object_size(unsigned pos);
};

struct page_range {
    explicit page_range(size_t size);
    size_t size;
    boost::intrusive::set_member_hook<> member_hook;
};

}

#endif

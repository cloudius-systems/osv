#ifndef SGLIST_H
#define SGLIST_H

#include <list>
#include "types.hh"

class sglist {
public:
    struct sg_node {
        u64 _paddr;
        u32 _len;

        sg_node(u64 addr, u32 len) :_paddr(addr), _len(len) {};
        sg_node(const sg_node& n) :_paddr(n._paddr), _len(n._len) {};
    };

    const int max_sgs = 1000;

    sglist() : _nsgs(0), _max_sgs(max_sgs) {};
    sglist(int n) : _nsgs(n), _max_sgs(max_sgs) {};

    int get_sgs() {return _nsgs;}

    bool add(u64 paddr, u32 len, bool front=false);
    void dump();

    std::list<struct sg_node> _nodes;
    int _nsgs;
    int _max_sgs;
};

#endif // SGLIST_H


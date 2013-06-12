#include "sglist.hh"
#include "debug.hh"

void
sglist::dump() {
    debug(fmt("nsgs=%d, max=%d\n") % _nsgs % _max_sgs);
    for (auto i = _nodes.begin(); i != _nodes.end(); i++) {
        debug(fmt("\t paddr=%x, len=%d\n") % i->_paddr % i->_len);
    }
}

bool
sglist::add(u64 paddr, u32 len, bool front) {
    if (_nsgs == max_sgs)
        return false;

    sg_node n(paddr, len);
    auto ii = (front)? _nodes.begin() : _nodes.end();
    _nodes.insert(ii, n);
    _nsgs++;
    _tot_len += len;

    return true;
}

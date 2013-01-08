#ifndef SIGNAL_HH_
#define SIGNAL_HH_

#include <signal.h>
#include <bitset>

namespace osv {

static const unsigned nsignals = 64;

struct sigset {
    std::bitset<nsignals> mask;
};

extern struct sigaction signal_actions[nsignals];

sigset* from_libc(sigset_t* s);
const sigset* from_libc(const sigset_t* s);

sigset* thread_signals();

}


#endif /* SIGNAL_HH_ */

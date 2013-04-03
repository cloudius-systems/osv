#ifndef SIGNAL_HH_
#define SIGNAL_HH_

#include <signal.h>
#include <bitset>
#include "exceptions.hh"

namespace osv {

static const unsigned nsignals = 64;

struct sigset {
    std::bitset<nsignals> mask;
};

extern struct sigaction signal_actions[nsignals];

sigset* from_libc(sigset_t* s);
const sigset* from_libc(const sigset_t* s);

sigset* thread_signals();

void generate_signal(siginfo_t &siginfo, exception_frame* ef);
void handle_segmentation_fault(ulong addr, exception_frame* ef);

}

namespace arch {
    void build_signal_frame(exception_frame* ef,
                 const siginfo_t& si,
                 const struct sigaction& sa);
}


#endif /* SIGNAL_HH_ */

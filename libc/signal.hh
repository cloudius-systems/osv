/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SIGNAL_HH_
#define SIGNAL_HH_

#include <signal.h>
#include <bitset>
#include "exceptions.hh"

namespace osv {

static const unsigned nsignals = 64;
static_assert(nsignals == NSIG - 1); //NSIG is actually 65 in both glibc and musl headers

//In theory, it would not matter how we encode the information about
//which signals are masked as long as our implementation of it stays
//completely internal. But in reality, when we implement rt_sigprocmask
//and other related syscalls, the users of those like glibc rely on
//the format of the bitmask used by Linux kernel. In Linux, the least
//significant bit (or the most right) represents the mask of the signal
//number 1, the 2nd bit represents the mask of the signal number 2, etc
//all the way to the maximum number 64. So we must ensure that our internal
//sigset structure can fit all these 64 bits and nothing more as some
//runtimes like golang assume the oldset argument of the rt_sigprocmask
//can be as small as NSIG/8 (8 bytes).
//To get or set a specific signal mask bit, we simply substract 1
//from the signal number - <mask index> = signum - 1
struct sigset {
    std::bitset<nsignals> mask;
};

static_assert(sizeof(sigset) == NSIG / 8,
    "size of sigset does not match the corresponding one in Linux kernel");

//We use the same 0-based index access pattern, where the signal action
//of a given signal with number "signo" is stored at the index "signo - 1"
extern struct sigaction signal_actions[nsignals];

sigset* from_libc(sigset_t* s);
const sigset* from_libc(const sigset_t* s);

sigset* thread_signals();

void generate_signal(siginfo_t &siginfo, exception_frame* ef);
void handle_mmap_fault(ulong addr, int sig, exception_frame* ef);
}

namespace arch {
    void build_signal_frame(exception_frame* ef,
                 const siginfo_t& si,
                 const struct sigaction& sa);
}


#endif /* SIGNAL_HH_ */

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_BSD_PCPU_H
#define _OSV_BSD_PCPU_H

#include <sys/cdefs.h>
#include <bsd/porting/netport.h>

// Those live themselves in pcpu.h in BSD, so who am I to argue.
#ifndef NR_VIRQS
#define NR_VIRQS    24
#endif
#ifndef NR_IPIS
#define NR_IPIS     2
#endif

// Heavily butchedred version of BSD's pcpu structures
struct pcpu {
    u_int		pc_cpuid;		/* This cpu number */
    unsigned int pc_last_processed_l1i;
    unsigned int pc_last_processed_l2i;
    int pc_virq_to_irq[NR_VIRQS];
    int pc_ipi_to_irq[NR_IPIS];
} __aligned(CACHE_LINE_SIZE);


__BEGIN_DECLS
struct pcpu *__pcpu_find(int cpu);
struct pcpu *pcpu_this();
__END_DECLS

#define pcpu_find(x) ({ struct pcpu *_p = __pcpu_find(x); _p; })
// FIXME: disable preemption
#define PCPU_GET(var) ({ struct pcpu *_p = pcpu_this(); _p->pc_##var; })
#endif

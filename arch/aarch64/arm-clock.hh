/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_ARM_CLOCK_HH
#define ARCH_ARM_CLOCK_HH

namespace processor {
}

#define NANO_PER_SEC 1000000000
#define MHZ 1000000

#define TIMER_CTL_ISTATUS_BIT 4
#define TIMER_CTL_IMASK_BIT   2
#define TIMER_CTL_ENABLE_BIT  1

#define DEFAULT_TIMER_IRQ_ID  (16 + 11)

int get_timer_irq_id();

#endif /* ARCH_ARM_CLOCK_HH */

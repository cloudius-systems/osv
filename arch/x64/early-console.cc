/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/isa-serial.hh"
#include <osv/prio.hh>

namespace console {

IsaSerialConsole arch_early_console __attribute__((init_priority((int)init_prio::console)));

}

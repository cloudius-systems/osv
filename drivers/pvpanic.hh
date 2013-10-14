/*
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OS_DRIVERS_PVPANIC_HH_
#define _OS_DRIVERS_PVPANIC_HH_

namespace panic {
namespace pvpanic {

void probe_and_setup();
// avoid using panic() as a name because of macro substitutions
void panicked();

}}
#endif // !_OS_DRIVERS_PVPANIC_HH_

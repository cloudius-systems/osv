#ifndef INCLUDED_OSV_POWER_H
#define INCLUDED_OSV_POWER_H

namespace osv {

void halt() __attribute__((noreturn));
void poweroff() __attribute__((noreturn));

}

#endif /* INCLUDED_OSV_POWER_H */

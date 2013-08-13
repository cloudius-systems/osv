#ifndef _ALTERNATIVE_HH_
#define _ALTERNATIVE_HH_

#define ALTERNATIVE(cond, x, y) do {    \
    if (!cond) {                        \
        do  x  while (0);               \
    } else {                            \
        do  y  while (0);               \
    }                                   \
} while (0)

#endif

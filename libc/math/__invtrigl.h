#include <float.h>

#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
/* shared by acosl, asinl and atan2l */
#define pio2_hi __pio2_hi
#define pio2_lo __pio2_lo
extern const long double pio2_hi, pio2_lo;

long double __invtrigl_R(long double z);
#endif

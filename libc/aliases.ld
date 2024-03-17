/* This file defines symbols as *aliases* to other symbols. The linker
 * statically-linking the OSv kernel will set the alias's address to be
 * the same one as the original symbol.
 * 
 * This technique is more powerful than the C compiler's "alias(...)"
 * attribute - the compiler-only technique is only usable when the alias
 * and original symbol are defined in the same translation unit, because
 * it is the compiler - not the linker - who need to copy the symbol's
 * address.
 */

/* errno */
___errno_location = __errno_location;

/* network */
__res_init = res_init;

/* math */
__exp_finite = exp;
__exp2_finite = exp2;
__log10_finite = log10;
__log2_finite = log2;
__log2f_finite = log2f;
__log_finite = log;
__pow_finite = pow;
__finite = finite;
__finitef = finitef;
__finitel = finitel;

/* locale */
__toupper_l = toupper_l;
newlocale = __newlocale;

/* multibyte */
__mbrlen = mbrlen;

/* mman */
__munmap = munmap;
__mmap = mmap;
mmap64 = mmap;

/* stdio */
__dup3 = dup3;

/* string */
__strtok_r = strtok_r;
__strndup = strndup;
__stpcpy = stpcpy;
__stpncpy = stpncpy;
__strdup = strdup;
strerrordesc_np = strerror;

/* time */
__gmtime_r = gmtime_r;
__strftime_l = strftime_l;

/* vfs */
statfs64 = statfs;
fstatfs64 = fstatfs;
statvfs64 = statvfs;
fstatvfs64 = fstatvfs;
preadv64 = preadv;
pwritev64 = pwritev;
stat64 = stat;
fstat64 = fstat;
lstat64 = lstat;
fstatat64 = fstatat;
__lseek = lseek;

__getauxval = getauxval;
__getpagesize = getpagesize;

/* Since we do nothing for those anyway, they are equal. */
_exit = exit;
_Exit = exit;

__dn_expand = dn_expand;
__sysconf = sysconf;

/* glibc uses these functions as C23 versions of the existing functions,
   which are supposed to also allow binary constants (0b...). So they
   should be slightly different than the classic functions, but until
   we implement that, lets at least support them as an alias - see
   issue #1299.
*/
__isoc23_strtoul = strtoul;
__isoc23_sscanf = sscanf;

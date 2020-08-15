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

/* multibyte */
__mbrlen = mbrlen;

/* string */
__strtok_r = strtok_r;
__strndup = strndup;

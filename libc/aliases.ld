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

__res_init = res_init;

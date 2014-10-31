#ifndef _OSV_SYMBOLS_HH
#define _OSV_SYMBOLS_HH
// By taking the address of these functions, we force the compiler to generate
// a symbol for it even when the function is inlined into all call sites. In a
// situation like that, the symbol would simply not be generated. That seems to
// be true even if we use "inline" instead of "static inline"
#define __MAKE_SYMBOL(name, num) static void *__address_##num __attribute__((used)) = (void *)&name
#define _MAKE_SYMBOL(name, num) __MAKE_SYMBOL(name, num)
#define MAKE_SYMBOL(name) _MAKE_SYMBOL(name, __COUNTER__)
#endif

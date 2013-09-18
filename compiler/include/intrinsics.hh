
#ifndef COMPILER_INTRINSICS_HH
#define COMPILER_INTRINSICS_HH

#ifndef ASSEMBLY

#if __GNUC__ == 4 && __GNUC_MINOR__ < 8

extern unsigned char __builtin_ia32_addcarryx_u32(unsigned char, unsigned int, unsigned int, unsigned int*);
extern unsigned char __builtin_ia32_addcarryx_u64(unsigned char, long long unsigned int, long long unsigned int, long long unsigned int*);

#endif

#endif

#endif

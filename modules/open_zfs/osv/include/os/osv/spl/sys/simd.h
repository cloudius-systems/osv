// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SIMD support header.
 * Provides the SIMD abstraction that OpenZFS checksumming/crypto uses.
 */
#ifndef _SPL_OSV_SIMD_H
#define	_SPL_OSV_SIMD_H

#include <sys/types.h>

/*
 * On OSv, SIMD (SSE/AVX) is always available since we run bare-metal
 * on x86_64 and don't need to worry about kernel FPU save/restore
 * (OSv is a unikernel, no user/kernel boundary).
 */
#define	kfpu_allowed()		1
#define	kfpu_begin()		do { } while (0)
#define	kfpu_end()		do { } while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do { } while (0)

/*
 * SIMD feature detection - report what the CPU supports.
 * On x86_64, we use compiler intrinsics to detect features.
 */
#if defined(__x86_64__) || defined(__i386__)

#include <cpuid.h>

static inline boolean_t
zfs_sse_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((edx & bit_SSE) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_sse2_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((edx & bit_SSE2) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_sse3_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_SSE3) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_ssse3_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_SSSE3) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_sse4_1_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_SSE4_1) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_sse4_2_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_SSE4_2) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_avx_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_AVX) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_avx2_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return ((ebx & bit_AVX2) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_avx512f_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return ((ebx & bit_AVX512F) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_avx512bw_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return ((ebx & bit_AVX512BW) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_aes_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_AES) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_pclmulqdq_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_PCLMUL) != 0);
	return (B_FALSE);
}

static inline boolean_t
zfs_sha256_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return ((ebx & (1U << 29)) != 0); /* SHA bit */
	return (B_FALSE);
}

/* MOVBE - not commonly needed */
static inline boolean_t
zfs_movbe_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return ((ecx & bit_MOVBE) != 0);
	return (B_FALSE);
}

#else /* !x86 */

/* Non-x86 stubs */
#define	zfs_sse_available()		B_FALSE
#define	zfs_sse2_available()		B_FALSE
#define	zfs_sse3_available()		B_FALSE
#define	zfs_ssse3_available()		B_FALSE
#define	zfs_sse4_1_available()		B_FALSE
#define	zfs_sse4_2_available()		B_FALSE
#define	zfs_avx_available()		B_FALSE
#define	zfs_avx2_available()		B_FALSE
#define	zfs_avx512f_available()		B_FALSE
#define	zfs_avx512bw_available()	B_FALSE
#define	zfs_aes_available()		B_FALSE
#define	zfs_pclmulqdq_available()	B_FALSE
#define	zfs_sha256_available()		B_FALSE
#define	zfs_movbe_available()		B_FALSE

#endif /* __x86_64__ || __i386__ */

/* NEON (aarch64) */
#define	zfs_neon_available()		B_FALSE
#define	zfs_sha256_neon_available()	B_FALSE
#define	zfs_sha512_neon_available()	B_FALSE

#endif /* _SPL_OSV_SIMD_H */

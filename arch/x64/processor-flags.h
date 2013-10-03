#ifndef OSV_X64_PROCESSOR_FLAGS_H
#define OSV_X64_PROCESSOR_FLAGS_H

#define X86_CR0_PE		(1 << 0)
#define X86_CR0_MP		(1 << 1)
#define X86_CR0_EM		(1 << 2)
#define X86_CR0_TS		(1 << 3)
#define X86_CR0_ET		(1 << 4)
#define X86_CR0_NE		(1 << 5)
#define X86_CR0_WP		(1 << 16)
#define X86_CR0_AM		(1 << 18)
#define X86_CR0_NW		(1 << 29)
#define X86_CR0_CD		(1 << 30)
#define X86_CR0_PG		(1 << 31)

#define X86_CR4_VME		(1 << 0)
#define X86_CR4_PVI		(1 << 1)
#define X86_CR4_TSD		(1 << 2)
#define X86_CR4_DE		(1 << 3)
#define X86_CR4_PSE		(1 << 4)
#define X86_CR4_PAE		(1 << 5)
#define X86_CR4_MCE		(1 << 6)
#define X86_CR4_PGE		(1 << 7)
#define X86_CR4_PCE		(1 << 8)
#define X86_CR4_OSFXSR		(1 << 9)
#define X86_CR4_OSXMMEXCPT	(1 << 10)
#define X86_CR4_VMXE		(1 << 13)
#define X86_CR4_SMXE		(1 << 14)
#define X86_CR4_FSGSBASE	(1 << 16)
#define X86_CR4_PCIDE		(1 << 17)
#define X86_CR4_OSXSAVE		(1 << 18)
#define X86_CR4_SMEP		(1 << 20)

#endif

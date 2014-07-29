#define FE_INVALID    1
#define __FE_DENORM   2
#define FE_DIVBYZERO  4
#define FE_OVERFLOW   8
#define FE_UNDERFLOW  16
#define FE_INEXACT    32

#define FE_ALL_EXCEPT 63

#define FE_TONEAREST  0
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xc00

typedef unsigned short fexcept_t;

typedef struct {
	unsigned short __control_word;
	unsigned short ___unused1;
	unsigned short __status_word;
	unsigned short ___unused2;
	unsigned short __tags;
	unsigned short ___unused3;
	unsigned int __eip;
	unsigned short __cs_selector;
	unsigned int __opcode:11;
	unsigned int ___unused4:5;
	unsigned int __data_offset;
	unsigned short __data_selector;
	unsigned short ___unused5;
	unsigned int __mxcsr;
} fenv_t;

#define FE_DFL_ENV      ((const fenv_t *) -1)

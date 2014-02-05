static inline struct pthread *__pthread_self()
{
	struct pthread *self;
	__asm__ __volatile__ ("movq %%fs:0,%0" : "=r" (self) );
	return self;
}

#define TP_ADJ(p) (p)

#define CANCEL_REG_IP 16

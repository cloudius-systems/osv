
extern "C" {
    void __cxa_pure_virtual(void);
    void abort(void);
    void _Unwind_Resume(void);
}

void __cxa_pure_virtual()
{
    abort();
}

void abort()
{
    while (true)
	;
}

void _Unwind_Resume()
{
    abort();
}

void operator delete(void *)
{
    abort();
}

void *__gxx_personality_v0;

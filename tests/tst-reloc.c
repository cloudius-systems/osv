#include <stdio.h>

int func_impl()
{
   return 42;
}

void *func_resolver() {
    puts("func_resolver");
    return (void *)func_impl;
}

int func() __attribute__((ifunc("func_resolver")));

// .rela.dyn.rel => R_X86_64_64 referencing STT_GNU_IFUNC in .rela.dyn
int (*fptr_func)() = func;

static void test_indirect_relocations()
{
#ifdef __x86_64__
    printf("%d\n", func());
#endif
}

int main()
{
    test_indirect_relocations();
}

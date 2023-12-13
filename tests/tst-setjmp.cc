#include <fenv.h>
#ifdef __OSV__
#include <__fenv.h>
#endif
#include <signal.h>
#include <assert.h>
#include <setjmp.h>
#include <math.h>

#include <iostream>

static int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}

//This is a simple test that verifies that setjmp/longjmp/sigsetjmp/siglongjmp
//flow control works correctly.

#ifndef __OSV__
extern "C" int __sigsetjmp(sigjmp_buf env, int savemask);
#define sigsetjmp(env, savemask) __sigsetjmp (env, savemask)
#endif

static jmp_buf env;
bool setjmp_check() {
    if (setjmp(env)) {
        std::cout << "Back from longjmp()!\n";
        return true;
    }
    std::cout << "After setjmp()!\n";
    longjmp(env, 1);
    return false;
}

static sigjmp_buf sig_env;
bool sigsetjmp_check(int savesigs) {
    if (sigsetjmp(sig_env, savesigs)) {
        std::cout << "Back from siglongjmp()!\n";
        return true;
    }
    std::cout << "After sigsetjmp()!\n";
    siglongjmp(sig_env, 1);
    return false;
}

int main(int argc, char **argv)
{
    expect(setjmp_check(), true);
    expect(sigsetjmp_check(0), true);
    expect(sigsetjmp_check(1), true);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}

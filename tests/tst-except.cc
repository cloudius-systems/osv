#include <debug.hh>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int ac, char** av)
{
    try {
        throw 1;
        report (0, "don't continue after throw");
    } catch (int e) {
        report (e == 1, "catch 1");
    }
    debug("SUMMARY: %d tests, %d failures\n", tests, fails);

}

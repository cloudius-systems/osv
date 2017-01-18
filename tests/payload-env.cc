#include <cstdlib>

int main(int argc, char **argv)
{
    setenv("FOO", "BAR", 1);

    return 0;
}

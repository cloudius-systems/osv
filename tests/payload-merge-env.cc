#include <string>

#include <cstdlib>

int main(int argc, char **argv)
{
    char *first = getenv("FIRST");
    char *second = getenv("SECOND");
    char *third = getenv("THIRD");

    if (std::string(first) != "LIBELLULE") {
        return 1;
    }

    if (std::string(second) != "MOUCHE") {
        return 1;
    }

    if (std::string(third) != "COCCINELLE") {
        return 1;
    }

    return 0;
}

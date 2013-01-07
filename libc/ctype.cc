#include <ctype.h>
#include <locale>


// ugly


int isalpha(int c)
{
    std::locale loc;
    return std::use_facet<std::ctype<char>>(loc).is(std::ctype_base::alpha, c);
}

int isdigit(int c)
{
    std::locale loc;
    return std::use_facet<std::ctype<char>>(loc).is(std::ctype_base::digit, c);
}

int isspace(int c)
{
    std::locale loc;
    return std::use_facet<std::ctype<char>>(loc).is(std::ctype_base::space, c);
}

int tolower(int c)
{
    std::locale loc;
    return std::use_facet<std::ctype<char>>(loc).tolower(c);
}

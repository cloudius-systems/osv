#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <memory>
#include <vector>
#include <stdint.h>
#include <cctype>
#include <string.h>
#include "debug.hh"

namespace {

// printf() is difficult because we must traverse the argument list
// (va_list ap) linearly, while the format string may specify random
// access using %4$d.
//
// our strategy is to parse the string and construct two vectors:
//
// args: objects of type 'arg' that are able to call the va_arg
//       macro with the appropriate type, and place it into a member of
//       'arg'.  'args' actually holds unique_ptr<>s to avoid leaks.
// frags: function objects, that, when called, yield a string that is
//        a fragment of the object being constructed.  Plaintext is
//        converted to a function that yields the text, while conversions
//        call more complicated functions.
//
// once we parse the format string, we iterate over args to read in the
// arguments, then iterator over frags to generate the string.


struct arg {
    virtual ~arg() {}
    virtual void consume(va_list ap) = 0;
};

struct arg_intx : arg {
    intmax_t val;
};

struct arg_char : arg_intx {
    virtual void consume(va_list ap) { val = va_arg(ap, int); }
};

struct arg_short : arg_intx {
    virtual void consume(va_list ap) { val = va_arg(ap, int); }
};

struct arg_int : arg_intx {
    virtual void consume(va_list ap) { val = va_arg(ap, int); }
};

struct arg_long : arg_intx {
    virtual void consume(va_list ap) { val = va_arg(ap, long int); }
};

struct arg_longlong : arg_intx {
    virtual void consume(va_list ap) { val = va_arg(ap, long long int); }
};

struct arg_uintx : arg {
    uintmax_t val;
};

struct arg_uint : arg_uintx {
    virtual void consume(va_list ap) { val = va_arg(ap, unsigned); }
};

struct arg_ulint : arg_uintx {
    virtual void consume(va_list ap) { val = va_arg(ap, unsigned long); }
};

struct arg_ullint : arg_uintx {
    virtual void consume(va_list ap) { val = va_arg(ap, unsigned long long); }
};

struct arg_ptr : arg {
    void* val;
    virtual void consume(va_list ap) { val = va_arg(ap, void*); }
};

struct arg_double : arg {
    double val;
    virtual void consume(va_list ap) { val = va_arg(ap, double); }
};

struct arg_str: arg {
    const char* val;
    virtual void consume(va_list ap) { val = va_arg(ap, const char*); }
};

enum intlen {
    len_char,
    len_short,
    len_int,
    len_long,
    len_longlong,
};

void fill_right_to(std::string& str, char c, size_t len)
{
    while (str.length() < len) {
        str.push_back(c);
    }
}

std::string strprintf(const char* fmt, va_list ap)
{
    auto orig = fmt;
    std::vector<std::unique_ptr<arg>> args;
    std::vector<std::function<std::string ()>> frags;
    unsigned lastpos = 0;
    auto add_arg = [&] (int pos, arg* parg) {
        args.resize(std::max(size_t(pos), args.size()));
        args[pos-1] = std::unique_ptr<arg>(parg);
    };
    auto make_int_arg = [&] (int pos, intlen len = len_int) {
        arg_intx* parg;
        switch (len) {
        case len_char: parg = new arg_char; break;
        case len_short: parg = new arg_short; break;
        case len_int: parg = new arg_int; break;
        case len_long: parg = new arg_long; break;
        case len_longlong: parg = new arg_longlong; break;
        default: abort();
        }
        add_arg(pos, parg);
        return parg;
    };
    while (*fmt) {
        auto p = fmt;
        while (*p && *p != '%') {
            ++p;
        }
        if (p != fmt) {
            frags.push_back([=] { return std::string(fmt, p); } );
            fmt = p;
        }
        if (!*fmt) {
            break;
        }
        // found '%'
        ++fmt;
        if (*fmt == '%') {
            frags.push_back([] { return std::string("%"); });
            continue;
        } else if (!*fmt) {
            break;
        }
        auto get_number = [&fmt] {
            int pos = 0;
            while (std::isdigit(*fmt)) {
                pos = pos * 10 + (*fmt++ - '0');
            }
            return pos;
        };
        auto get_positional = [&fmt, &get_number, &lastpos] {
            auto save = fmt;
            auto pos = get_number();
            if (pos == 0 || *fmt != '$') {
                fmt = save;
                pos = ++lastpos;
            }
            return pos;
        };
        unsigned pos = get_positional();
        bool alt_form = false;
        char pad_char = ' ';
        bool left_adjust = false;
        bool add_blank = false;
        bool add_sign = false;
        bool thousands = false;
        bool alt_digits = false;
        bool go = true;
        while (go) {
            switch (*fmt++) {
            case '#': alt_form = true; break;
            case '0': pad_char = '0'; break;
            case '-': left_adjust = true; break;
            case ' ': add_blank = true; break;
            case '+': add_sign = true; break;
            case '\'': thousands = true; break;
            case 'I': alt_digits = true; break;
            default: --fmt; go = false; break;
            }
        }
        if (alt_form || left_adjust || add_blank || add_sign
                || thousands || alt_digits) {
            debug(boost::format("unimplemented format %1%") % orig);
        }
        int width = get_number();
        std::function<int ()> width_fn = [=] { return width; };
        if (!width && *fmt == '*') {
            ++fmt;
            auto arg = make_int_arg(get_positional());
            width_fn = [=] { return int(arg->val); };
        }
        bool has_precision = *fmt == '.';
        std::function<int ()> precision_fn;
        if (has_precision) {
            ++fmt;
            auto precision = get_number();
            precision_fn = [=] { return precision; };
            if (!precision && *fmt == '*') {
                ++fmt;
                auto arg = make_int_arg(get_positional());
                precision_fn = [=] { return int(arg->val); };
            }
        }
        int hcount = 0, lcount = 0, Lcount = 0, jcount = 0, zcount = 0, tcount = 0;
        go = true;
        while (go) {
            switch (*fmt++) {
            case 'h': ++hcount; break;
            case 'l': ++lcount; break;
            case 'L': ++Lcount; break;
            case 'j': ++jcount; break;
            case 'z': ++zcount; break;
            case 't': ++tcount; break;
            default: --fmt; go = false; break;
            }
        }
        auto int_size = [=] {
            if (hcount >= 2) return len_char;
            if (hcount == 1) return len_short;
            if (lcount >= 2) return len_longlong;
            if (lcount == 1) return len_long;
            return len_int;
        };
        auto get_precision_or_default = [=] (int prec) -> std::function<int ()> {
            if (!has_precision) {
                return [=] { return prec; };
            }
            return precision_fn;
        };
        auto unsigned_conversion = [=, &frags] (unsigned base, const char* digits) {
            auto precision_fn = get_precision_or_default(1);
            auto arg = make_int_arg(pos, int_size());
            frags.push_back([=] {
                std::string ret;
                uintmax_t val = arg->val;
                while (val) {
                    ret.push_back(digits[val % base]);
                    val /= base;
                }
                fill_right_to(ret, '0', precision_fn());
                fill_right_to(ret, pad_char, width_fn());
                std::reverse(ret.begin(), ret.end());
                return ret;
            });
        };
        switch (*fmt++) {
        case 'd':
        case 'i': {
            auto precision_fn = get_precision_or_default(1);
            auto arg = make_int_arg(pos, int_size());
            frags.push_back([=] {
                std::string ret;
                bool negative = arg->val < 0;
                uintmax_t val = std::abs(arg->val);
                while (val) {
                    ret.push_back('0' + val % 10);
                    val /= 10;
                };
                fill_right_to(ret, '0', precision_fn() - negative);
                if (negative) {
                    ret.push_back('-');
                }
                fill_right_to(ret, ' ', width_fn());
                std::reverse(ret.begin(), ret.end());
                return ret;
            });
            break;
        }
        case 'u':
             unsigned_conversion(10u, "0123456789");
             break;
        case 'o':
             unsigned_conversion(8u, "01234567");
             break;
        case 'x':
             unsigned_conversion(16u, "0123456789abcdef");
             break;
        case 'X':
             unsigned_conversion(16u, "0123456789ABCDEF");
             break;
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            abort();
        case 'c': {
            if (lcount) {
                debug("%lc conversion ignored");
            }
            auto arg = make_int_arg(pos);
            frags.push_back([=] { char c = arg->val; return std::string(c, 1); });
            break;
        }
        case 's': {
            auto arg = new arg_str;
            add_arg(pos, arg);
            frags.push_back([=] { return std::string(arg->val); });
            break;
        }
        default:
            abort();
        }
    }
    for (size_t i = 0; i < args.size(); ++i) {
        args[i]->consume(ap);
    }
    std::string ret;
    for (auto p : frags) {
        ret += (p)();
    }
    return ret;
}

}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    auto out = strprintf(format, ap);
    auto trunc = out.substr(0, size - 1);
    auto last = std::copy(trunc.begin(), trunc.end(), str);
    *last = '\0';
    return out.length();
}

int sprintf(char* str, const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
    auto out = strprintf(format, ap);
    va_end(ap);
    strcpy(str, out.c_str());
    return out.length();
}

int snprintf(char* str, size_t n, const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
    auto out = strprintf(format, ap);
    va_end(ap);
    std::string trunc = out.substr(0, n - 1);
    strcpy(str, trunc.c_str());
    return out.length();
}

int printf(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
    auto out = strprintf(format, ap);
    va_end(ap);

    debug(out);
    return out.length();
}

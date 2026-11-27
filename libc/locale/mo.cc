/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// GNU .mo catalogs and plural expressions (see mo.h). The rules that are not
// in the file-format description were measured on glibc 2.41
// (scripts/locale-oracle/messages/rules*.sh, limits*.sh):
// - major revision (revision >> 16) above 1 is rejected; a minor revision
//   other than 0 (12-word header, system-dependent strings) is searched only
//   through the hash table; a hash table of fewer than 3 entries is ignored;
//   when a hash table is used it is authoritative;
// - without one, a binary search over the originals (top = act, bottom =
//   act + 1): an unsorted table misses what a binary search misses;
// - tables need not be aligned.

#include "mo.h"
#include <stdlib.h>
#include <string.h>

namespace {

uint32_t word(const osv_mo *m, uint64_t off)
{
    uint32_t w;
    memcpy(&w, m->base + off, 4);
    return m->swap ? __builtin_bswap32(w) : w;
}

// String i of the table at tab, if it ends with a NUL within the file.
const char *entry(const osv_mo *m, uint32_t tab, uint32_t i, size_t *len)
{
    uint32_t l = word(m, tab + 8 * uint64_t(i));
    uint32_t o = word(m, tab + 8 * uint64_t(i) + 4);
    if (uint64_t(o) + l >= m->size || m->base[uint64_t(o) + l]) {
        return nullptr;
    }
    *len = l;
    return m->base + o;
}

// glibc's .mo hash: hashpjw over the bytes of the msgid.
uint32_t hashpjw(const char *s)
{
    uint32_t h = 0;
    for (; *s; s++) {
        h = (h << 4) + (unsigned char)*s;
        uint32_t g = h & 0xf0000000u;
        if (g) {
            h ^= g >> 24;
            h ^= g;
        }
    }
    return h;
}

} // namespace

bool osv_mo_open(const char *base, size_t size, osv_mo *m) noexcept
{
    if (size < 28 || size > UINT32_MAX) {
        return false;
    }
    m->base = base;
    m->size = size;
    m->swap = false;
    uint32_t magic = word(m, 0);
    if (magic == 0xde120495u) {
        m->swap = true;
    } else if (magic != 0x950412deu) {
        return false;
    }
    uint32_t rev = word(m, 4);
    if (rev >> 16 > 1) {
        return false;
    }
    m->n = word(m, 8);
    m->otab = word(m, 12);
    m->ttab = word(m, 16);
    m->hsize = word(m, 20);
    m->htab = word(m, 24);
    if (uint64_t(m->otab) + 8 * uint64_t(m->n) > size ||
        uint64_t(m->ttab) + 8 * uint64_t(m->n) > size) {
        return false;
    }
    if (m->hsize < 3) {
        m->hsize = 0;
    } else if (uint64_t(m->htab) + 4 * uint64_t(m->hsize) > size) {
        return false;
    }
    m->hash_only = (rev & 0xffff) != 0;
    return true;
}

const char *osv_mo_find(const osv_mo *m, const char *msgid, size_t *len,
                        uint32_t *index) noexcept
{
    size_t ol;
    const char *o;
    uint32_t i = 0;
    if (m->hsize) {
        uint32_t h = hashpjw(msgid);
        uint32_t idx = h % m->hsize, incr = 1 + h % (m->hsize - 2);
        for (uint32_t probe = 0;; probe++) {
            if (probe == m->hsize) {
                return nullptr;
            }
            uint32_t e = word(m, m->htab + 4 * uint64_t(idx));
            if (!e) {
                return nullptr;
            }
            // Entries past n are system-dependent strings: not supported.
            if (e - 1 < m->n && (o = entry(m, m->otab, e - 1, &ol)) &&
                !strcmp(msgid, o)) {
                i = e - 1;
                break;
            }
            idx = uint32_t((uint64_t(idx) + incr) % m->hsize);
        }
    } else if (m->hash_only) {
        return nullptr;
    } else {
        uint32_t bottom = 0, top = m->n;
        for (;;) {
            if (bottom >= top) {
                return nullptr;
            }
            i = bottom + (top - bottom) / 2;
            if (!(o = entry(m, m->otab, i, &ol))) {
                return nullptr;
            }
            int c = strcmp(msgid, o);
            if (!c) {
                break;
            }
            if (c < 0) {
                top = i;
            } else {
                bottom = i + 1;
            }
        }
    }
    *index = i;
    return entry(m, m->ttab, i, len);
}

// ---- plural expressions ---------------------------------------------------

namespace {

// Tokens, parser stack kinds and tree node operators share one numbering.
enum : uint8_t {
    OR = 1, AND, EQ, NE, LT, GT, LE, GE, ADD, SUB, MUL, DIV, MOD, // binary
    NUM, VAR, NOT, COND,                                          // nodes
    LP, RP, QUEST, COLON, END, BAD, EXP,                          // tokens, stack
};

bool binary(uint8_t op)
{
    return op >= OR && op <= MOD;
}

int prec(uint8_t op)
{
    static const int p[] = { 0, 1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6 };
    return p[op];
}

uint8_t lex(const char **sp, uint64_t *v)
{
    const char *s = *sp;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    uint8_t t;
    char c = *s++;
    switch (c) {
    case '\0': case ';': case '\n': t = END; s--; break;
    case 'n': t = VAR; break;
    case '(': t = LP; break;
    case ')': t = RP; break;
    case '?': t = QUEST; break;
    case ':': t = COLON; break;
    case '+': t = ADD; break;
    case '-': t = SUB; break;
    case '*': t = MUL; break;
    case '/': t = DIV; break;
    case '%': t = MOD; break;
    case '!': t = *s == '=' ? (s++, NE) : NOT; break;
    case '=': t = *s == '=' ? (s++, EQ) : BAD; break;
    case '<': t = *s == '=' ? (s++, LE) : LT; break;
    case '>': t = *s == '=' ? (s++, GE) : GT; break;
    case '&': t = *s == '&' ? (s++, AND) : BAD; break;
    case '|': t = *s == '|' ? (s++, OR) : BAD; break;
    default:
        if (c >= '0' && c <= '9') {
            uint64_t x = c - '0';
            while (*s >= '0' && *s <= '9') {
                x = x * 10 + (*s++ - '0'); // wraps, as glibc's literals do
            }
            *v = x;
            t = NUM;
        } else {
            t = BAD;
        }
    }
    *sp = s;
    return t;
}

// glibc's parser fails when its stack (the initial state included) would
// reach 10000 entries: measured (limits2.sh) on nested parentheses, '!',
// ?: and n*( chains, which push one entry per symbol as an LR parser does.
constexpr unsigned max_stack = 9999;

struct item {
    uint8_t kind;
    uint32_t node, depth;
};

struct parser {
    item *st;
    unsigned sp;
    osv_plural_node *nodes;
    uint32_t count;

    bool push(uint8_t kind)
    {
        if (1 + sp + 1 > max_stack) {
            return false;
        }
        st[sp++] = item{kind, 0, 0};
        return true;
    }
    uint32_t node(uint8_t op, uint64_t v, uint32_t a, uint32_t b, uint32_t c)
    {
        nodes[count] = osv_plural_node{v, a, b, c, op};
        return count++;
    }
    void exp(item *it, uint32_t n, uint32_t depth)
    {
        *it = item{EXP, n, depth};
    }
    static uint32_t deeper(uint32_t a, uint32_t b)
    {
        return a > b ? a : b;
    }
    // '!' binds tighter than anything that can follow an operand.
    void reduce_not()
    {
        while (sp >= 2 && st[sp - 1].kind == EXP && st[sp - 2].kind == NOT) {
            item e = st[--sp];
            exp(&st[sp - 1], node(NOT, 0, e.node, 0, 0), e.depth + 1);
        }
    }
    // Reduce what the lookahead t ends (t follows an operand).
    void reduce(uint8_t t)
    {
        while (sp >= 3 && st[sp - 1].kind == EXP) {
            uint8_t k = st[sp - 2].kind;
            if (binary(k)) {
                if (binary(t) && prec(k) < prec(t)) {
                    return;
                }
                item &l = st[sp - 3], &r = st[sp - 1];
                uint32_t d = deeper(l.depth, r.depth) + 1;
                exp(&l, node(k, 0, l.node, r.node, 0), d);
                sp -= 2;
            } else if (k == COLON) {
                // [cond ? a : b]: '?' is right-associative and the loosest.
                if (binary(t) || t == QUEST) {
                    return;
                }
                item &c = st[sp - 5], &a = st[sp - 3], &b = st[sp - 1];
                uint32_t d = deeper(deeper(c.depth, a.depth), b.depth) + 1;
                exp(&c, node(COND, 0, c.node, a.node, b.node), d);
                sp -= 4;
            } else {
                return;
            }
        }
    }
};

} // namespace

bool osv_plural_compile(const char *s, osv_plural *p) noexcept
{
    size_t len = strcspn(s, ";\n");
    size_t cap = len + 2 < max_stack ? len + 2 : max_stack;
    parser ps{static_cast<item *>(malloc(cap * sizeof(item))), 0,
              static_cast<osv_plural_node *>(malloc((len + 1) * sizeof(osv_plural_node))), 0};
    bool ok = false;
    if (!ps.st || !ps.nodes) {
        goto out;
    }
    for (;;) {
        uint64_t v = 0;
        uint8_t t = lex(&s, &v);
        bool after = ps.sp && ps.st[ps.sp - 1].kind == EXP;
        if (t == BAD) {
            goto out;
        }
        if (t == NUM || t == VAR || t == NOT || t == LP) {
            if (after || !ps.push(t)) {
                goto out;
            }
            if (t == NUM || t == VAR) {
                ps.exp(&ps.st[ps.sp - 1], ps.node(t, v, 0, 0, 0), 1);
                ps.reduce_not();
            }
            continue;
        }
        if (!after) {
            goto out;
        }
        ps.reduce(t);
        if (t == END) {
            if (ps.sp != 1) {
                goto out;
            }
            break;
        }
        if (t == COLON && (ps.sp < 2 || ps.st[ps.sp - 2].kind != QUEST)) {
            goto out;
        }
        if (t == RP && (ps.sp < 2 || ps.st[ps.sp - 2].kind != LP)) {
            goto out;
        }
        if (!ps.push(t)) {
            goto out;
        }
        if (t == RP) {
            item e = ps.st[ps.sp - 2];
            ps.sp -= 2;
            ps.st[ps.sp - 1] = e;
            ps.reduce_not();
        }
    }
    {
        osv_plural_node *nodes = static_cast<osv_plural_node *>(
            malloc(ps.count * sizeof(osv_plural_node)));
        if (!nodes) {
            goto out;
        }
        memcpy(nodes, ps.nodes, ps.count * sizeof(osv_plural_node));
        p->nodes = nodes;
        p->count = ps.count;
        p->root = ps.st[0].node;
        p->depth = ps.st[0].depth;
        ok = true;
    }
out:
    free(ps.st);
    free(ps.nodes);
    return ok;
}

void osv_plural_from_header(const char *header, osv_plural *p) noexcept
{
    p->nodes = nullptr;
    p->count = p->root = p->depth = 0;
    p->nplurals = 2;
    const char *e = header ? strstr(header, "plural=") : nullptr;
    const char *np = header ? strstr(header, "nplurals=") : nullptr;
    if (!e || !np) {
        return;
    }
    np += 9;
    while (*np == ' ' || (*np >= '\t' && *np <= '\r')) {
        np++;
    }
    if (*np < '0' || *np > '9') {
        return;
    }
    unsigned long n = 0;
    for (; *np >= '0' && *np <= '9'; np++) {
        unsigned d = *np - '0';
        n = n > (~0UL - d) / 10 ? ~0UL : n * 10 + d; // strtoul saturates
    }
    unsigned long keep = p->nplurals;
    if (osv_plural_compile(e + 7, p)) {
        p->nplurals = n;
    } else {
        p->nplurals = keep;
    }
}

namespace {

struct frame {
    uint32_t node;
    uint8_t phase;
    uint64_t left;
};

bool eval(const osv_plural *p, uint64_t n, uint64_t *out)
{
    frame local[64], *fr = local;
    if (p->depth > 64) {
        fr = static_cast<frame *>(malloc(p->depth * sizeof(frame)));
        if (!fr) {
            return false;
        }
    }
    bool ok = true;
    unsigned sp = 0;
    uint64_t r = 0;
    fr[sp++] = frame{p->root, 0, 0};
    while (sp) {
        frame &f = fr[sp - 1];
        const osv_plural_node &nd = p->nodes[f.node];
        uint8_t ph = f.phase++;
        switch (nd.op) {
        case NUM: r = nd.v; sp--; continue;
        case VAR: r = n; sp--; continue;
        case NOT:
            if (ph == 0) { fr[sp++] = frame{nd.a, 0, 0}; } else { r = !r; sp--; }
            continue;
        case COND:
            if (ph == 0) { fr[sp++] = frame{nd.a, 0, 0}; }
            else if (ph == 1) { fr[sp++] = frame{r ? nd.b : nd.c, 0, 0}; }
            else { sp--; }
            continue;
        case OR: case AND:
            if (ph == 0) { fr[sp++] = frame{nd.a, 0, 0}; }
            else if (ph == 1 && (nd.op == OR ? r != 0 : r == 0)) { r = nd.op == OR; sp--; }
            else if (ph == 1) { fr[sp++] = frame{nd.b, 0, 0}; }
            else { r = r != 0; sp--; }
            continue;
        }
        if (ph == 0) {
            fr[sp++] = frame{nd.a, 0, 0};
            continue;
        }
        if (ph == 1) {
            f.left = r;
            fr[sp++] = frame{nd.b, 0, 0};
            continue;
        }
        uint64_t a = f.left, b = r;
        sp--;
        switch (nd.op) {
        case EQ: r = a == b; break;
        case NE: r = a != b; break;
        case LT: r = a < b; break;
        case GT: r = a > b; break;
        case LE: r = a <= b; break;
        case GE: r = a >= b; break;
        case ADD: r = a + b; break;
        case SUB: r = a - b; break;
        case MUL: r = a * b; break;
        case DIV: case MOD:
            if (!b) {
                ok = false;
                sp = 0;
                break;
            }
            r = nd.op == DIV ? a / b : a % b;
            break;
        }
    }
    if (fr != local) {
        free(fr);
    }
    *out = r;
    return ok;
}

} // namespace

unsigned long osv_plural_index(const osv_plural *p, unsigned long n) noexcept
{
    uint64_t r;
    if (!p->count || !eval(p, n, &r)) {
        r = n != 1;
    }
    return r < p->nplurals ? r : 0;
}

void osv_plural_free(osv_plural *p) noexcept
{
    free(p->nodes);
    p->nodes = nullptr;
    p->count = 0;
}

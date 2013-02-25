/*-
 * Copyright (c) 2000-2008 Poul-Henning Kamp
 * Copyright (c) 2000-2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <bsd/porting/netport.h>
#include <sys/cdefs.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/sbuf.h>

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#define        SBMALLOC(size)          malloc(size)
#define        SBFREE(buf)             free(buf)

/*
 * Predicates
 */
#define	SBUF_ISDYNAMIC(s)	((s)->s_flags & SBUF_DYNAMIC)
#define	SBUF_ISDYNSTRUCT(s)	((s)->s_flags & SBUF_DYNSTRUCT)
#define	SBUF_ISFINISHED(s)	((s)->s_flags & SBUF_FINISHED)
#define	SBUF_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define	SBUF_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1))
#define	SBUF_CANEXTEND(s)	((s)->s_flags & SBUF_AUTOEXTEND)

/*
 * Set / clear flags
 */
#define	SBUF_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	SBUF_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)

#define	SBUF_MINEXTENDSIZE	16		/* Should be power of 2. */

#ifdef PAGE_SIZE
#define	SBUF_MAXEXTENDSIZE	PAGE_SIZE
#define	SBUF_MAXEXTENDINCR	PAGE_SIZE
#else
#define	SBUF_MAXEXTENDSIZE	4096
#define	SBUF_MAXEXTENDINCR	4096
#endif


#define	assert_sbuf_integrity(s) do { } while (0)
#define	assert_sbuf_state(s, i)	 do { } while (0)

static int
sbuf_extendsize(int size)
{
	int newsize;

	if (size < (int)SBUF_MAXEXTENDSIZE) {
		newsize = SBUF_MINEXTENDSIZE;
		while (newsize < size)
			newsize *= 2;
	} else {
		newsize = roundup2(size, SBUF_MAXEXTENDINCR);
	}
	KASSERT(newsize >= size, ("%s: %d < %d\n", __func__, newsize, size));
	return (newsize);
}

/*
 * Extend an sbuf.
 */
static int
sbuf_extend(struct sbuf *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!SBUF_CANEXTEND(s))
		return (-1);
	newsize = sbuf_extendsize(s->s_size + addlen);
	newbuf = SBMALLOC(newsize);
	if (newbuf == NULL)
		return (-1);
	memcpy(newbuf, s->s_buf, s->s_size);
	if (SBUF_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	else
		SBUF_SETFLAG(s, SBUF_DYNAMIC);
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

/*
 * Initialize the internals of an sbuf.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
static struct sbuf *
sbuf_newbuf(struct sbuf *s, char *buf, int length, int flags)
{

	memset(s, 0, sizeof(*s));
	s->s_flags = flags;
	s->s_size = length;
	s->s_buf = buf;

	if ((s->s_flags & SBUF_AUTOEXTEND) == 0) {
		KASSERT(s->s_size >= 0,
		    ("attempt to create a too small sbuf"));
	}

	if (s->s_buf != NULL)
		return (s);

	if ((flags & SBUF_AUTOEXTEND) != 0)
		s->s_size = sbuf_extendsize(s->s_size);

	s->s_buf = SBMALLOC(s->s_size);
	if (s->s_buf == NULL)
		return (NULL);
	SBUF_SETFLAG(s, SBUF_DYNAMIC);
	return (s);
}

/*
 * Initialize an sbuf.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int length, int flags)
{

	KASSERT(length >= 0,
	    ("attempt to create an sbuf of negative length (%d)", length));
	KASSERT((flags & ~SBUF_USRFLAGMSK) == 0,
	    ("%s called with invalid flags", __func__));

	flags &= SBUF_USRFLAGMSK;
	if (s != NULL)
		return (sbuf_newbuf(s, buf, length, flags));

	s = SBMALLOC(sizeof(*s));
	if (s == NULL)
		return (NULL);
	if (sbuf_newbuf(s, buf, length, flags) == NULL) {
		SBFREE(s);
		return (NULL);
	}
	SBUF_SETFLAG(s, SBUF_DYNSTRUCT);
	return (s);
}

#ifdef _KERNEL
/*
 * Create an sbuf with uio data
 */
struct sbuf *
sbuf_uionew(struct sbuf *s, struct uio *uio, int *error)
{

	KASSERT(uio != NULL,
	    ("%s called with NULL uio pointer", __func__));
	KASSERT(error != NULL,
	    ("%s called with NULL error pointer", __func__));

	s = sbuf_new(s, NULL, uio->uio_resid + 1, 0);
	if (s == NULL) {
		*error = ENOMEM;
		return (NULL);
	}
	*error = uiomove(s->s_buf, uio->uio_resid, uio);
	if (*error != 0) {
		sbuf_delete(s);
		return (NULL);
	}
	s->s_len = s->s_size - 1;
	*error = 0;
	return (s);
}
#endif

/*
 * Clear an sbuf and reset its position.
 */
void
sbuf_clear(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */

	SBUF_CLEARFLAG(s, SBUF_FINISHED);
	s->s_error = 0;
	s->s_len = 0;
}

/*
 * Set the sbuf's end position to an arbitrary value.
 * Effectively truncates the sbuf at the new position.
 */
int
sbuf_setpos(struct sbuf *s, ssize_t pos)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	KASSERT(pos >= 0,
	    ("attempt to seek to a negative position (%jd)", (intmax_t)pos));
	KASSERT(pos < s->s_size,
	    ("attempt to seek past end of sbuf (%jd >= %jd)",
	    (intmax_t)pos, (intmax_t)s->s_size));

	if (pos < 0 || pos > s->s_len)
		return (-1);
	s->s_len = pos;
	return (0);
}

/*
 * Set up a drain function and argument on an sbuf to flush data to
 * when the sbuf buffer overflows.
 */
void
sbuf_set_drain(struct sbuf *s, sbuf_drain_func *func, void *ctx)
{

	assert_sbuf_state(s, 0);
	assert_sbuf_integrity(s);
	KASSERT(func == s->s_drain_func || s->s_len == 0,
	    ("Cannot change drain to %p on non-empty sbuf %p", func, s));
	s->s_drain_func = func;
	s->s_drain_arg = ctx;
}

/*
 * Call the drain and process the return.
 */
static int
sbuf_drain(struct sbuf *s)
{
	int len;

	KASSERT(s->s_len > 0, ("Shouldn't drain empty sbuf %p", s));
	KASSERT(s->s_error == 0, ("Called %s with error on %p", __func__, s));
	len = s->s_drain_func(s->s_drain_arg, s->s_buf, s->s_len);
	if (len < 0) {
		s->s_error = -len;
		return (s->s_error);
	}
	KASSERT(len > 0 && len <= s->s_len,
	    ("Bad drain amount %d for sbuf %p", len, s));
	s->s_len -= len;
	/*
	 * Fast path for the expected case where all the data was
	 * drained.
	 */
	if (s->s_len == 0)
		return (0);
	/*
	 * Move the remaining characters to the beginning of the
	 * string.
	 */
	memmove(s->s_buf, s->s_buf + len, s->s_len);
	return (0);
}

/*
 * Append a byte to an sbuf.  This is the core function for appending
 * to an sbuf and is the main place that deals with extending the
 * buffer and marking overflow.
 */
static void
sbuf_put_byte(struct sbuf *s, int c)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (s->s_error != 0)
		return;
	if (SBUF_FREESPACE(s) <= 0) {
		/*
		 * If there is a drain, use it, otherwise extend the
		 * buffer.
		 */
		if (s->s_drain_func != NULL)
			(void)sbuf_drain(s);
		else if (sbuf_extend(s, 1) < 0)
			s->s_error = ENOMEM;
		if (s->s_error != 0)
			return;
	}
	s->s_buf[s->s_len++] = c;
}

/*
 * Append a byte string to an sbuf.
 */
int
sbuf_bcat(struct sbuf *s, const void *buf, size_t len)
{
	const char *str = buf;
	const char *end = str + len;

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (s->s_error != 0)
		return (-1);
	for (; str < end; str++) {
		sbuf_put_byte(s, *str);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

/*
 * Copy a byte string into an sbuf.
 */
int
sbuf_bcpy(struct sbuf *s, const void *buf, size_t len)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	sbuf_clear(s);
	return (sbuf_bcat(s, buf, len));
}

/*
 * Append a string to an sbuf.
 */
int
sbuf_cat(struct sbuf *s, const char *str)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (s->s_error != 0)
		return (-1);

	while (*str != '\0') {
		sbuf_put_byte(s, *str++);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

/*
 * Copy a string into an sbuf.
 */
int
sbuf_cpy(struct sbuf *s, const char *str)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	sbuf_clear(s);
	return (sbuf_cat(s, str));
}

/* This is actually used with radix [2..36] */
char const hex2ascii_data[] = "0123456789abcdefghijklmnopqrstuvwxyz";
#define hex2ascii(hex)  (hex2ascii_data[hex])

/*
 * Put a NUL-terminated ASCII number (base <= 36) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */
static char *
ksprintn(char *nbuf, uintmax_t num, int base, int *lenp, int upper)
{
    char *p, c;

    p = nbuf;
    *p = '\0';
    do {
        c = hex2ascii(num % base);
        *++p = upper ? toupper(c) : c;
    } while (num /= base);
    if (lenp)
        *lenp = p - nbuf;
    return (p);
}

/*
 * Scaled down version of printf(3).
 *
 * Two additional formats:
 *
 * The format %b is supported to decode error registers.
 * Its usage is:
 *
 *  printf("reg=%b\n", regval, "<base><arg>*");
 *
 * where <base> is the output base expressed as a control character, e.g.
 * \10 gives octal; \20 gives hex.  Each arg is a sequence of characters,
 * the first of which gives the bit number to be inspected (origin 1), and
 * the next characters (up to a control character, i.e. a character <= 32),
 * give the name of the register.  Thus:
 *
 *  kvprintf("reg=%b\n", 3, "\10\2BITTWO\1BITONE\n");
 *
 * would produce output:
 *
 *  reg=3<BITTWO,BITONE>
 *
 * XXX:  %D  -- Hexdump, takes pointer and separator string:
 *      ("%6D", ptr, ":")   -> XX:XX:XX:XX:XX:XX
 *      ("%*D", len, ptr, " " -> XX XX XX XX ...
 */
int
kvprintf(char const *fmt, void (*func)(int, void*), void *arg, int radix, va_list ap)
{
#define PCHAR(c) {int cc=(c); if (func) (*func)(cc,arg); else *d++ = cc; retval++; }
    char nbuf[MAXNBUF];
    char *d;
    const char *p, *percent, *q;
    u_char *up;
    int ch, n;
    uintmax_t num;
    int base, lflag, qflag, tmp, width, ladjust, sharpflag, neg, sign, dot;
    int cflag, hflag, jflag, tflag, zflag;
    int dwidth, upper;
    char padc;
    int stop = 0, retval = 0;

    num = 0;
    if (!func)
        d = (char *) arg;
    else
        d = NULL;

    if (fmt == NULL)
        fmt = "(fmt null)\n";

    if (radix < 2 || radix > 36)
        radix = 10;

    for (;;) {
        padc = ' ';
        width = 0;
        while ((ch = (u_char)*fmt++) != '%' || stop) {
            if (ch == '\0')
                return (retval);
            PCHAR(ch);
        }
        percent = fmt - 1;
        qflag = 0; lflag = 0; ladjust = 0; sharpflag = 0; neg = 0;
        sign = 0; dot = 0; dwidth = 0; upper = 0;
        cflag = 0; hflag = 0; jflag = 0; tflag = 0; zflag = 0;
reswitch:   switch (ch = (u_char)*fmt++) {
        case '.':
            dot = 1;
            goto reswitch;
        case '#':
            sharpflag = 1;
            goto reswitch;
        case '+':
            sign = 1;
            goto reswitch;
        case '-':
            ladjust = 1;
            goto reswitch;
        case '%':
            PCHAR(ch);
            break;
        case '*':
            if (!dot) {
                width = va_arg(ap, int);
                if (width < 0) {
                    ladjust = !ladjust;
                    width = -width;
                }
            } else {
                dwidth = va_arg(ap, int);
            }
            goto reswitch;
        case '0':
            if (!dot) {
                padc = '0';
                goto reswitch;
            }
        case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
                for (n = 0;; ++fmt) {
                    n = n * 10 + ch - '0';
                    ch = *fmt;
                    if (ch < '0' || ch > '9')
                        break;
                }
            if (dot)
                dwidth = n;
            else
                width = n;
            goto reswitch;
        case 'b':
            num = (u_int)va_arg(ap, int);
            p = va_arg(ap, char *);
            for (q = ksprintn(nbuf, num, *p++, NULL, 0); *q;)
                PCHAR(*q--);

            if (num == 0)
                break;

            for (tmp = 0; *p;) {
                n = *p++;
                if (num & (1 << (n - 1))) {
                    PCHAR(tmp ? ',' : '<');
                    for (; (n = *p) > ' '; ++p)
                        PCHAR(n);
                    tmp = 1;
                } else
                    for (; *p > ' '; ++p)
                        continue;
            }
            if (tmp)
                PCHAR('>');
            break;
        case 'c':
            PCHAR(va_arg(ap, int));
            break;
        case 'D':
            up = va_arg(ap, u_char *);
            p = va_arg(ap, char *);
            if (!width)
                width = 16;
            while(width--) {
                PCHAR(hex2ascii(*up >> 4));
                PCHAR(hex2ascii(*up & 0x0f));
                up++;
                if (width)
                    for (q=p;*q;q++)
                        PCHAR(*q);
            }
            break;
        case 'd':
        case 'i':
            base = 10;
            sign = 1;
            goto handle_sign;
        case 'h':
            if (hflag) {
                hflag = 0;
                cflag = 1;
            } else
                hflag = 1;
            goto reswitch;
        case 'j':
            jflag = 1;
            goto reswitch;
        case 'l':
            if (lflag) {
                lflag = 0;
                qflag = 1;
            } else
                lflag = 1;
            goto reswitch;
        case 'n':
            if (jflag)
                *(va_arg(ap, intmax_t *)) = retval;
            else if (qflag)
                *(va_arg(ap, quad_t *)) = retval;
            else if (lflag)
                *(va_arg(ap, long *)) = retval;
            else if (zflag)
                *(va_arg(ap, size_t *)) = retval;
            else if (hflag)
                *(va_arg(ap, short *)) = retval;
            else if (cflag)
                *(va_arg(ap, char *)) = retval;
            else
                *(va_arg(ap, int *)) = retval;
            break;
        case 'o':
            base = 8;
            goto handle_nosign;
        case 'p':
            base = 16;
            sharpflag = (width == 0);
            sign = 0;
            num = (uintptr_t)va_arg(ap, void *);
            goto number;
        case 'q':
            qflag = 1;
            goto reswitch;
        case 'r':
            base = radix;
            if (sign)
                goto handle_sign;
            goto handle_nosign;
        case 's':
            p = va_arg(ap, char *);
            if (p == NULL)
                p = "(null)";
            if (!dot)
                n = strlen (p);
            else
                for (n = 0; n < dwidth && p[n]; n++)
                    continue;

            width -= n;

            if (!ladjust && width > 0)
                while (width--)
                    PCHAR(padc);
            while (n--)
                PCHAR(*p++);
            if (ladjust && width > 0)
                while (width--)
                    PCHAR(padc);
            break;
        case 't':
            tflag = 1;
            goto reswitch;
        case 'u':
            base = 10;
            goto handle_nosign;
        case 'X':
            upper = 1;
        case 'x':
            base = 16;
            goto handle_nosign;
        case 'y':
            base = 16;
            sign = 1;
            goto handle_sign;
        case 'z':
            zflag = 1;
            goto reswitch;
handle_nosign:
            sign = 0;
            if (jflag)
                num = va_arg(ap, uintmax_t);
            else if (qflag)
                num = va_arg(ap, u_quad_t);
            else if (tflag)
                num = va_arg(ap, ptrdiff_t);
            else if (lflag)
                num = va_arg(ap, u_long);
            else if (zflag)
                num = va_arg(ap, size_t);
            else if (hflag)
                num = (u_short)va_arg(ap, int);
            else if (cflag)
                num = (u_char)va_arg(ap, int);
            else
                num = va_arg(ap, u_int);
            goto number;
handle_sign:
            if (jflag)
                num = va_arg(ap, intmax_t);
            else if (qflag)
                num = va_arg(ap, quad_t);
            else if (tflag)
                num = va_arg(ap, ptrdiff_t);
            else if (lflag)
                num = va_arg(ap, long);
            else if (zflag)
                num = va_arg(ap, ssize_t);
            else if (hflag)
                num = (short)va_arg(ap, int);
            else if (cflag)
                num = (char)va_arg(ap, int);
            else
                num = va_arg(ap, int);
number:
            if (sign && (intmax_t)num < 0) {
                neg = 1;
                num = -(intmax_t)num;
            }
            p = ksprintn(nbuf, num, base, &n, upper);
            tmp = 0;
            if (sharpflag && num != 0) {
                if (base == 8)
                    tmp++;
                else if (base == 16)
                    tmp += 2;
            }
            if (neg)
                tmp++;

            if (!ladjust && padc == '0')
                dwidth = width - tmp;
            width -= tmp + max(dwidth, n);
            dwidth -= n;
            if (!ladjust)
                while (width-- > 0)
                    PCHAR(' ');
            if (neg)
                PCHAR('-');
            if (sharpflag && num != 0) {
                if (base == 8) {
                    PCHAR('0');
                } else if (base == 16) {
                    PCHAR('0');
                    PCHAR('x');
                }
            }
            while (dwidth-- > 0)
                PCHAR('0');

            while (*p)
                PCHAR(*p--);

            if (ladjust)
                while (width-- > 0)
                    PCHAR(' ');

            break;
        default:
            while (percent < fmt)
                PCHAR(*percent++);
            /*
             * Since we ignore an formatting argument it is no
             * longer safe to obey the remaining formatting
             * arguments as the arguments will no longer match
             * the format specs.
             */
            stop = 1;
            break;
        }
    }
#undef PCHAR
}

/*
 * Append a non-NUL character to an sbuf.  This prototype signature is
 * suitable for use with kvprintf(9).
 */
static void
sbuf_putc_func(int c, void *arg)
{

    if (c != '\0')
        sbuf_put_byte(arg, c);
}

int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{

    assert_sbuf_integrity(s);
    assert_sbuf_state(s, 0);

    KASSERT(fmt != NULL,
        ("%s called with a NULL format string", __func__));

    (void)kvprintf(fmt, sbuf_putc_func, s, 10, ap);
    if (s->s_error != 0)
        return (-1);
    return (0);
}

/*
 * Format the given arguments and append the resulting string to an sbuf.
 */
int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = sbuf_vprintf(s, fmt, ap);
	va_end(ap);
	return (result);
}

/*
 * Append a character to an sbuf.
 */
int
sbuf_putc(struct sbuf *s, int c)
{

	sbuf_put_byte(s, c);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

/*
 * Trim whitespace characters from end of an sbuf.
 */
int
sbuf_trim(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	if (s->s_error != 0)
		return (-1);

	while (s->s_len > 0 && isspace(s->s_buf[s->s_len-1]))
		--s->s_len;

	return (0);
}

/*
 * Check if an sbuf has an error.
 */
int
sbuf_error(const struct sbuf *s)
{

	return (s->s_error);
}

/*
 * Finish off an sbuf.
 */
int
sbuf_finish(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (s->s_drain_func != NULL) {
		while (s->s_len > 0 && s->s_error == 0)
			s->s_error = sbuf_drain(s);
	}
	s->s_buf[s->s_len] = '\0';
	SBUF_SETFLAG(s, SBUF_FINISHED);
#ifdef _KERNEL
	return (s->s_error);
#else
	errno = s->s_error;
	if (s->s_error)
		return (-1);
	return (0);
#endif
}

/*
 * Return a pointer to the sbuf data.
 */
char *
sbuf_data(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, SBUF_FINISHED);
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	return (s->s_buf);
}

/*
 * Return the length of the sbuf data.
 */
ssize_t
sbuf_len(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	if (s->s_error != 0)
		return (-1);
	return (s->s_len);
}

/*
 * Clear an sbuf, free its buffer if necessary.
 */
void
sbuf_delete(struct sbuf *s)
{
	int isdyn;

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */

	if (SBUF_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	isdyn = SBUF_ISDYNSTRUCT(s);
	memset(s, 0, sizeof(*s));
	if (isdyn)
		SBFREE(s);
}

/*
 * Check if an sbuf has been finished.
 */
int
sbuf_done(const struct sbuf *s)
{

	return (SBUF_ISFINISHED(s));
}

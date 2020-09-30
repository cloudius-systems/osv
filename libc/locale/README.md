The files in this folder and OSv specific locale-related code in runtime.cc (see `__newlocale` function)
provide minimal implementation of the locale functionality. Unfortunately musl 1.1.24 had enhanced
its implementation to support categories (for example new 'cat' field in locale struct)
and does not quite fit OSv minimal need. For now, we have made minimal changes to existing
files and mostly retained older musl 0.9.12 copies to make locale-specific logic work.
We may need to adjust it futher and decide what files constitute OSv version
of it and completely cut it off of any future musl upgrades.

Please see following musl commits that should shed some light:
- https://git.musl-libc.org/cgit/musl/commit/?id=0bc03091bb674ebb9fa6fe69e4aec1da3ac484f2 (add locale framework)
- https://git.musl-libc.org/cgit/musl/commit/?id=16f18d036d9a7bf590ee6eb86785c0a9658220b6 (byte-based C locale, phase 2: stdio and iconv (multibyte callers))
- https://git.musl-libc.org/cgit/musl/commit/?id=61a3364d246e72b903da8b76c2e27a225a51351e (overhaul locale internals to treat categories roughly uniformly)

`langinfo.c` - copied from musl 1.1.24 and adjusted for category handling (struct `__locale_struct` has no member named `cat`; for now copied from 1.1.24 and changed to drop references to `cat` field in `__locale_struct`)

These 4 files symlink to musl 0.9.12 version
* `intl.c`
* `catopen.c`
* `duplocale.c`
* `setlocale.c`

These 4 files go together and can possibly be replaced with musl copies.
* `strtod_l.c`
* `strtof_l.c`
* `strtold_l.c`
* `../stdlib/strtod.c`

Other changes:
* `freelocale.c` has been tweaked to handle memory destruction in OSv unique way.
* `langinfo.c` has been updated to 1.1.24 copy and tweaked to remove code referencing `loc->cat` (new category related field)

These files in 1.1.24 more less map to what intl.c used to be
* `locale/c_locale.o`
* `locale/__lctrans.o`
* `locale/bind_textdomain_codeset.o`
* `locale/dcngettext.o` - fails to compile (old musl does not have it)
* `locale/textdomain.o`

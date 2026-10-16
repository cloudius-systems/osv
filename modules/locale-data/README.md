# locale-data

Copyright (C) 2026 Greg Burd. BSD-3-Clause.

Compiled glibc-format locale data for a `conf_locale=1` OSv image.

With `conf_locale=1` (Stage 1) OSv can LOAD glibc-format locale data, but a
normal image ships none, so every named locale is `ENOENT` unless the operator
supplies `LOCPATH` data. This module provides the data: it compiles glibc's
locale SOURCE definitions with `localedef` and installs the COMPILED output
where the loader looks (`/usr/lib/locale/<name>.utf8/` and
`/usr/lib/locale/locale-archive`), plus the `libc.mo` message catalogs under
`/usr/share/locale/<ll>/LC_MESSAGES/`. A `conf_locale=1` image with this module
serves the 14 default locales out of the box.

## Clean room

No glibc C source is committed or shipped. The data is a build artifact
produced at image build time from a pinned glibc release tarball:

- glibc 2.44, `https://ftp.gnu.org/gnu/glibc/glibc-2.44.tar.xz`
- sha256 `37f600f2bef3c5e8300147059568b2a2e40a7ad6ccc65ce942556d49429cc667`

The Makefile fetches and sha256-verifies the tarball, extracts only
`localedata/` (localedef's input) and `po/` (msgfmt's input), and compiles
from there. The FSF claims no copyright in the locale data files; even so we
ship them as a built artifact, not as committed source.

## localedef

The data format must match what the Stage 1 loader validates. The build uses
the host's `localedef` when it is >= 2.41, because the host's output is
byte-for-byte identical to glibc 2.44's own `localedef` for these locales
(verified per-category and for the archive against a 2.44 localedef built from
the pinned tree). If the host's `localedef` is older or absent, the Makefile
builds 2.44's `localedef` from the pinned source tree (`build-localedef.sh`).

## Options

Passed on the build line, like `conf_zfs`:

- `conf_locale=1` is REQUIRED. A `conf_locale=0` kernel cannot load locale
  data, so including this module with `conf_locale=0` fails the build with a
  clear message.
- `conf_locale_data=14` (default): the 14 default locales (en_US en_GB de_DE
  fr_FR es_ES it_IT pt_BR nl_NL sv_SE pl_PL ru_RU ja_JP zh_CN ko_KR), each as
  per-locale directories AND in a `locale-archive`, with `libc.mo` for their
  languages.
- `conf_locale_data=full`: every UTF-8 locale glibc ships, archive form only
  (directory form would be thousands of files), with `libc.mo` for every
  language that has a catalog.
- `conf_locale_data=none`: nothing (for images that supply their own
  `LOCPATH`); the module installs no data.

## Image-size cost

See the Stage 2 report for measured deltas (14 dir+archive, full archive, none).

## Build

    scripts/build image=<app>,locale-data conf_locale=1 conf_locale_data=14

The module requires network access at build time to fetch the pinned tarball,
and `localedef` and `msgfmt` (gettext) on the build host.

## Notes

- Changing `conf_locale` or `conf_locale_data` requires a full clean build:
  the kernel's `__ctype_*_loc` objects differ between `conf_locale=0` and `=1`,
  and the data mode is cached in a build marker.
- The downloaded glibc tarball is verified by sha256, which is the trust
  anchor (not TLS); a mismatch fails the build before the tarball is used.
- `conf_locale_data=14` ships both the per-locale directories and the archive.
  The archive alone serves every category (as `full` relies on), so an image
  that needs only `setlocale`-driven lookups can drop the directory form to
  save about 17 MB; a future option may make archive-only the default.

#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Compile the pinned glibc locale sources and install the compiled data for a
# conf_locale=1 OSv image, and write the module's usr.manifest for them:
#   build.sh <localedata-dir> <install-dir> <localedef> <mode> <module-dir> <default-locales> <default-langs>
# <mode> is 14 (the default locales, directory AND archive), full (every UTF-8
# locale glibc ships, archive only) or none (nothing). The manifest is written
# here, not in a module.py, because scripts/module.py imports a module.py
# during dependency resolution (before 'make module' runs), so a module.py that
# walked install/ would capture a stale or empty tree; the manifest built here
# always reflects the data just produced.
set -eu

LOCALEDATA=$1
INSTALL=$2
LOCALEDEF=$3
MODE=$4
MODULE_DIR=$5
DEFAULT_LOCALES=$6
DEFAULT_LANGS=$7

LOCALE_DIR=$INSTALL/usr/lib/locale
MSG_DIR=$INSTALL/usr/share/locale
PO=$(dirname "$LOCALEDATA")/po
MANIFEST=$MODULE_DIR/usr.manifest

command -v msgfmt >/dev/null 2>&1 || {
    echo "locale-data: no msgfmt; install gettext (the libc.mo catalogs need it)" >&2
    exit 1
}

# Write usr.manifest for whatever is under install/, with image paths equal to
# the install-relative path and host paths as ${MODULE_DIR}/install/...
write_manifest() {
    printf '[manifest]\n' > "$MANIFEST"
    [ -d "$INSTALL" ] || return 0
    ( cd "$INSTALL" && find . -type f ! -name .stamp | LC_ALL=C sort ) | while read -r rel; do
        rel=${rel#./}
        printf '/%s: ${MODULE_DIR}/install/%s\n' "$rel" "$rel" >> "$MANIFEST"
    done
}

rm -rf "$INSTALL"

if [ "$MODE" = none ]; then
    echo "locale-data: conf_locale_data=none, installing no data"
    mkdir -p "$INSTALL"
    write_manifest
    exit 0
fi

mkdir -p "$LOCALE_DIR"

# One locale: compile to a directory, add to the archive. localedef finds
# charmaps/locales/repertoiremaps under I18NPATH. The archive path is
# prefix-relative to the host localedef's built-in default (/usr/lib/locale),
# so --prefix="$INSTALL" writes "$INSTALL/usr/lib/locale/locale-archive".
compile_dir() {
    name=$1
    I18NPATH="$LOCALEDATA" $LOCALEDEF -c -i "$LOCALEDATA/locales/$name" \
        -f "$LOCALEDATA/charmaps/UTF-8" "$LOCALE_DIR/$name.utf8" >/dev/null 2>&1 || {
        echo "locale-data: localedef -i $name failed" >&2
        return 1
    }
}
add_archive() {
    name=$1
    I18NPATH="$LOCALEDATA" $LOCALEDEF -c --prefix="$INSTALL" \
        --add-to-archive "$LOCALE_DIR/$name.utf8" >/dev/null 2>&1 || {
        echo "locale-data: --add-to-archive $name failed" >&2
        return 1
    }
}

# libc.mo for a language, built with msgfmt from the pinned po (en has none).
build_mo() {
    lang=$1
    [ -f "$PO/$lang.po" ] || return 0
    mkdir -p "$MSG_DIR/$lang/LC_MESSAGES"
    msgfmt -o "$MSG_DIR/$lang/LC_MESSAGES/libc.mo" "$PO/$lang.po" >/dev/null 2>&1 || {
        echo "locale-data: msgfmt $lang failed" >&2
        return 1
    }
}

if [ "$MODE" = 14 ]; then
    echo "locale-data: compiling the 14 default locales (directory + archive)"
    for l in $DEFAULT_LOCALES; do
        compile_dir "$l"
        add_archive "$l"
    done
    for l in $DEFAULT_LANGS; do
        build_mo "$l"
    done
elif [ "$MODE" = full ]; then
    echo "locale-data: compiling every UTF-8 locale glibc ships (archive only)"
    tmpd=$(mktemp -d)
    trap 'rm -rf "$tmpd"' EXIT
    # SUPPORTED lists one locale per line as "<name>[.<charset>][@mod]/<charset> \".
    # Take the UTF-8 ones; <name> is the locale as the user names it (keep the
    # @modifier), the localedef source is <name> with its codeset dropped, and
    # the archive entry is named "<name-without-codeset>.utf8" to match how the
    # 14-locale archive stores de_DE.utf8 etc.
    grep 'UTF-8' "$LOCALEDATA/SUPPORTED" | sed 's/[[:space:]]*\\$//' | while read -r entry; do
        name=${entry%%/*}                 # strip "/<charset>"
        [ -n "$name" ] || continue
        mod=''
        case "$name" in *@*) mod="@${name#*@}"; name=${name%@*} ;; esac
        base=${name%%.*}                  # locale name without its codeset
        src="$base$mod"                   # localedef source file name
        out="$base.utf8$mod"              # archive-entry / scratch dir name
        [ -f "$LOCALEDATA/locales/$src" ] || src="$base"
        I18NPATH="$LOCALEDATA" $LOCALEDEF -c -i "$LOCALEDATA/locales/$src" \
            -f "$LOCALEDATA/charmaps/UTF-8" "$tmpd/$out" >/dev/null 2>&1 || {
            echo "locale-data: localedef -i $src ($out) failed, skipping" >&2
            continue
        }
        I18NPATH="$LOCALEDATA" $LOCALEDEF -c --prefix="$INSTALL" \
            --add-to-archive "$tmpd/$out" >/dev/null 2>&1 || {
            echo "locale-data: --add-to-archive $out failed, skipping" >&2
        }
        rm -rf "$tmpd/$out"
    done
    # Every language that has a catalog.
    for po in "$PO"/*.po; do
        build_mo "$(basename "$po" .po)"
    done
else
    echo "locale-data: unknown conf_locale_data='$MODE' (want 14, full or none)" >&2
    exit 1
fi

[ -f "$LOCALE_DIR/locale-archive" ] || {
    echo "locale-data: no locale-archive was produced" >&2
    exit 1
}
write_manifest
echo "locale-data: done; archive $(du -h "$LOCALE_DIR/locale-archive" | cut -f1)"

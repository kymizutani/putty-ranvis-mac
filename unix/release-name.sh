#!/bin/sh
#
# Work out what a release file made from this source should be called.
#
#   release-name.sh --tree <source-dir> --ext <dmg|zip> [--dir <out-dir>]
#
# Prints three tab-separated fields: the base part of the file name,
# the port build number, and the name of the top directory an unpacked
# Zip would make.
#
# The version the About box gives - TEXTVER in version.h - is the one
# place the version is written down, so the name of the archive comes
# from there: a build that says it's 0.85-003 is handed round as
# putty-ranvis-0.85-macos-arm64-003.dmg. When the source doesn't say
# (upstream's, or one where the version hasn't been set), fall back to
# naming after the directory and picking the next unused number.

set -e

tree=
ext=dmg
dir=

while [ $# -gt 0 ]; do
    case "$1" in
        --tree) tree=$2; shift 2 ;;
        --ext)  ext=$2;  shift 2 ;;
        --dir)  dir=$2;  shift 2 ;;
        *) echo "release-name.sh: unrecognised option $1" >&2; exit 2 ;;
    esac
done

if [ -z "$tree" ]; then
    echo "release-name.sh: --tree <source-dir> is required" >&2
    exit 2
fi

textver=
if [ -f "$tree/version.h" ]; then
    textver=$(sed -n 's/^#define TEXTVER "\([^"]*\)".*/\1/p' "$tree/version.h" |
              head -1)
fi

case "$ext" in
    zip) suffix="macos-port-src" ;;
    dmg) suffix="macos-$(uname -m)" ;;
    *)   suffix="macos-$(uname -m)" ;;
esac

version=
seq=
case "$textver" in
    # 0.85-003: everything before the last hyphen is the version of
    # the source, the three digits after it are this port's build.
    *[0-9]-[0-9][0-9][0-9])
        version=${textver%-*}
        seq=${textver##*-}
        ;;
esac

if [ -n "$version" ]; then
    pkg="putty-ranvis-$version"
else
    pkg=$(basename "$tree")
    if [ -n "$dir" ]; then
        seq=$(/bin/sh "$(dirname "$0")/next-sequence-number.sh" \
                  --dir "$dir" --base "$pkg-$suffix" --ext "$ext")
    fi
fi

printf '%s\t%s\t%s\n' "$pkg-$suffix" "$seq" "$pkg"

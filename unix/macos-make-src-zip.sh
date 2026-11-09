#!/bin/sh
#
# Put the ported source into a Zip file, so that it can be handed
# round. The result is buildable as it stands: see README-macOS.md for
# the two commands that make the applications from it.
#
#   macos-make-src-zip.sh [--tree <source-dir>] [--out-dir <dir>]
#                         [--base <name>] [--seq <nnn>] [--dry-run]
#
# The file's name gets a number on the end - -001, -002, and so on -
# for the same reason the disk image does: you can see which one is the
# latest without opening it, and making a new one doesn't throw away an
# old one. Nothing that's already been built is included.

set -e

tree="$(cd "$(dirname "$0")/.." && pwd)"
out_dir=
base=
seq=
dry_run=false

while [ $# -gt 0 ]; do
    case "$1" in
        --tree)    tree=$2; shift 2 ;;
        --out-dir) out_dir=$2; shift 2 ;;
        --base)    base=$2; shift 2 ;;
        --seq)     seq=$2; shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        *) echo "macos-make-src-zip.sh: unrecognised option $1" >&2; exit 2 ;;
    esac
done

if [ ! -d "$tree" ] || [ ! -f "$tree/CMakeLists.txt" ]; then
    echo "macos-make-src-zip.sh: $tree doesn't look like the source tree" >&2
    exit 1
fi

tree="${tree%/}"
if [ -z "$out_dir" ]; then
    out_dir="$(dirname "$tree")/build/macos"
fi
if [ -z "$base" ]; then
    base="$(basename "$tree")-macos-port-src"
fi
if [ -z "$seq" ]; then
    seq=$(/bin/sh "$(dirname "$0")/next-sequence-number.sh" \
              --dir "$out_dir" --base "$base" --ext zip)
fi

out="$out_dir/$base-$seq.zip"

if $dry_run; then
    echo "macos-make-src-zip.sh: would write $out"
    exit 0
fi

# Building the Zip from the tree's parent, with the tree's own name as
# the top entry, so that unpacking it gives you a directory instead of
# a pile of files.
mkdir -p "$out_dir"
rm -f "$out"
(cd "$(dirname "$tree")" && zip -r -X -q "$out" "$(basename "$tree")" \
    -x "*/.DS_Store" "*.DS_Store" "*/build/*" "*/.git/*")

if ! unzip -t -q "$out" >/dev/null; then
    echo "macos-make-src-zip.sh: the archive doesn't pass its own test" >&2
    exit 1
fi

count=$(unzip -l "$out" | awk 'END { print $2 }')
size=$(du -sh "$out" | awk '{print $1}')
echo "macos-make-src-zip.sh: wrote $out ($size, $count entries), containing:"
echo "    $(basename "$tree")/ - the source, with README-macOS.md in it"

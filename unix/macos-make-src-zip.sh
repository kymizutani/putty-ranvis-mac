#!/bin/sh
#
# Put the ported source into a Zip file, so that it can be handed
# round. The result is buildable as it stands: see README-macOS.md for
# the two commands that make the applications from it.
#
#   macos-make-src-zip.sh [--tree <source-dir>] [--out-dir <dir>]
#                         [--base <name>] [--seq <nnn>] [--commit <ref>]
#                         [--from-worktree] [--dry-run]
#
# The file is named after the version this source says it is (see
# release-name.sh), for the same reason the disk image is: the name,
# the About box and the tag all say the same thing, and making a new
# one doesn't throw away an old one.
#
# When the source is a git checkout, the archive is made from what's
# committed - HEAD, or --commit <ref> for some other place - because
# that's the thing that matches the tag you've just made. Uncommitted
# work and anything git isn't tracking (the build directories, the
# bundles) stay out. --from-worktree makes it from the files as they
# are instead, which is the only option when there's no git to ask.

set -e

tree="$(cd "$(dirname "$0")/.." && pwd)"
out_dir=
base=
seq=
commit=HEAD
from_worktree=false
dry_run=false

while [ $# -gt 0 ]; do
    case "$1" in
        --tree)    tree=$2; shift 2 ;;
        --out-dir) out_dir=$2; shift 2 ;;
        --base)    base=$2; shift 2 ;;
        --seq)     seq=$2; shift 2 ;;
        --commit)  commit=$2; shift 2 ;;
        --from-worktree) from_worktree=true; shift ;;
        --dry-run) dry_run=true; shift ;;
        *) echo "macos-make-src-zip.sh: unrecognised option $1" >&2; exit 2 ;;
    esac
done

tree="${tree%/}"
if [ ! -f "$tree/CMakeLists.txt" ]; then
    echo "macos-make-src-zip.sh: $tree doesn't look like the source tree" >&2
    exit 1
fi

if [ -z "$out_dir" ]; then
    out_dir="$tree/build/macos"
fi

naming=$(/bin/sh "$(dirname "$0")/release-name.sh" \
             --tree "$tree" --ext zip --dir "$out_dir")
if [ -z "$base" ]; then
    base=$(printf '%s\n' "$naming" | cut -f1)
fi
if [ -z "$seq" ]; then
    seq=$(printf '%s\n' "$naming" | cut -f2)
fi
pkg=$(printf '%s\n' "$naming" | cut -f3)
if [ -z "$seq" ]; then
    echo "macos-make-src-zip.sh: can't work out a build number; use --seq" >&2
    exit 1
fi

out="$out_dir/$base-$seq.zip"

if $dry_run; then
    echo "macos-make-src-zip.sh: would write $out"
    echo "macos-make-src-zip.sh:   unpacking it would create $pkg/"
    exit 0
fi

mkdir -p "$out_dir"
rm -f "$out"

if ! $from_worktree && git -C "$tree" rev-parse --git-dir >/dev/null 2>&1; then
    # Everything the tag points at, and nothing that isn't committed.
    git -C "$tree" archive --format=tar --prefix="$pkg/" "$commit" |
        gzip > "$out"
    how="committed at $commit"
else
    # No git to ask: collect the files, skipping what a build leaves
    # lying around.
    (cd "$(dirname "$tree")" && zip -r -X -q "$out" "$(basename "$tree")" \
        -x "*/.DS_Store" "*.DS_Store" "*/build*/*" "*/install/*" "*/.git/*")
    how="the files as they are"
fi

if ! unzip -t -qq "$out" >/dev/null 2>&1; then
    echo "macos-make-src-zip.sh: the archive doesn't pass its own test" >&2
    exit 1
fi

count=$(unzip -l "$out" | awk 'END { print $2 }')
size=$(du -sh "$out" | awk '{print $1}')
echo "macos-make-src-zip.sh: wrote $out ($size, $count entries), from $how:"
echo "    $pkg/ - the source, with README-macOS.md in it"

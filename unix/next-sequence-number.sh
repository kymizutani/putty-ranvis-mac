#!/bin/sh
#
# Work out the next sequence number for a family of distributed files.
#
#   next-sequence-number.sh --dir <directory> --base <name> [--ext <ext>]
#
# Files are named <base>-001.<ext>, <base>-002.<ext>, and so on, so
# that a version number in the name tells you which one is the newest
# without having to unpack it. This prints the next number to use, as
# three digits. Nothing is created or changed here.

set -e

dir=
base=
ext=dmg

while [ $# -gt 0 ]; do
    case "$1" in
        --dir) dir=$2; shift 2 ;;
        --base) base=$2; shift 2 ;;
        --ext) ext=$2; shift 2 ;;
        *) echo "next-sequence-number.sh: unrecognised option $1" >&2; exit 2 ;;
    esac
done

if [ -z "$dir" ] || [ -z "$base" ]; then
    echo "next-sequence-number.sh: --dir <directory> --base <name> are required" >&2
    exit 2
fi

# Find the highest existing number, ignoring anything that doesn't
# consist of exactly the base, a hyphen, digits, and the extension.
highest=0
for f in "$dir"/"$base"-[0-9]*."$ext"; do
    [ -e "$f" ] || continue
    number=$(basename "$f" | sed -e "s/^$base-//" -e "s/\.$ext\$//")
    case "$number" in
        *[!0-9]*) continue ;;
    esac
    number=$((10#$number))
    if [ "$number" -gt "$highest" ]; then
        highest=$number
    fi
done

printf "%03d\n" $((highest + 1))

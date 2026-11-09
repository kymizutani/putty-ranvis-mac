#!/bin/sh
#
# Collect the macOS application bundles that the build produced into a
# single disk image, the way Mac software is normally handed around.
#
#   macos-make-dmg.sh --build <build-dir> [--base <name>] [--seq <nnn>]
#                     [--out <file.dmg>] [--volname <name>]
#                     [--apps "Name1 Name2 ..."] [--readme <file>] [--dry-run]
#
# The image contains the .app bundles, a link to /Applications so that
# dragging an icon onto it installs the program, and the port's notes.
# Nothing in it needs anything from the machine it was built on: the
# bundles carry their own libraries (macos-embed-deps.sh), and each
# program works out where its data files are from where it is being
# run, so mounting the image somewhere new is fine.
#
# The file's name gets a number on the end - -001, -002, and so on - so
# that you can tell which one is the latest without opening it, and so
# that making a new one never throws away an old one. --dry-run prints
# the name that would be used and stops there.

set -e

build=
out=
base=
seq=
volname="PuTTY"
apps="PuTTY PTerm PuTTYgen"
readme=
dry_run=false

while [ $# -gt 0 ]; do
    case "$1" in
        --build)   build=$2; shift 2 ;;
        --out)     out=$2; shift 2 ;;
        --base)    base=$2; shift 2 ;;
        --seq)     seq=$2; shift 2 ;;
        --volname) volname=$2; shift 2 ;;
        --apps)    apps=$2; shift 2 ;;
        --readme)  readme=$2; shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        *) echo "macos-make-dmg.sh: unrecognised option $1" >&2; exit 2 ;;
    esac
done

if [ -z "$build" ]; then
    echo "macos-make-dmg.sh: --build <build-dir> is required" >&2
    exit 2
fi

macosdir="$build/macos"
if [ ! -d "$macosdir" ]; then
    echo "macos-make-dmg.sh: $macosdir does not exist (run the build first)" >&2
    exit 1
fi

if [ -z "$out" ]; then
    if [ -z "$base" ]; then
        # The source tree has a name like putty-ranvis-0.85, which is
        # the version information we want in the file name; the rest
        # says what it was built for.
        srctree="$(cd "$(dirname "$0")/.." && pwd)"
        base="$(basename "$srctree")-macos-$(uname -m)"
    fi
    if [ -z "$seq" ]; then
        seq=$(/bin/sh "$(dirname "$0")/next-sequence-number.sh" \
                  --dir "$macosdir" --base "$base" --ext dmg)
    fi
    out="$macosdir/$base-$seq.dmg"
fi

if $dry_run; then
    echo "macos-make-dmg.sh: would write $out"
    exit 0
fi

sourcetree="$(dirname "$0")/../README-macOS.md"
if [ -z "$readme" ] && [ -f "$sourcetree" ]; then
    readme="$sourcetree"
fi

work="$(dirname "$out")/.dmg-stage-$$"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/PuTTY"

for app in $apps; do
    if [ ! -d "$macosdir/$app.app" ]; then
        echo "macos-make-dmg.sh: $macosdir/$app.app does not exist" >&2
        exit 1
    fi
    echo "macos-make-dmg.sh: putting $app.app in the image"
    cp -R "$macosdir/$app.app" "$work/PuTTY/$app.app"
    # The bundles were signed where they were built, and copying them
    # doesn't disturb that, but check anyway: a damaged signature is the
    # kind of thing that makes Gatekeeper complain for no obvious reason.
    codesign --verify --strict --deep "$work/PuTTY/$app.app"
done

ln -s /Applications "$work/PuTTY/Applications"

if [ -n "$readme" ]; then
    cp "$readme" "$work/PuTTY/README-macOS.md"
fi

rm -f "$out"
hdiutil create -quiet -srcfolder "$work/PuTTY" -volname "$volname" \
    -format UDZO -ov "$out"
hdiutil verify -quiet "$out"

size=$(du -sh "$out" | awk '{print $1}')
echo "macos-make-dmg.sh: wrote $out ($size), containing:"
for app in $apps; do
    echo "    $app.app"
done
[ -n "$readme" ] && echo "    README-macOS.md, Applications -> /Applications"

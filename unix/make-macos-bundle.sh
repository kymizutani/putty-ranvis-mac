#!/bin/sh
#
# make-macos-bundle.sh: assemble a macOS application bundle (.app) for
# one of the Unix front ends.
#
# This deliberately does not try to be a replacement for
# gtk-mac-bundler: the GTK shared libraries are _not_ copied into the
# bundle, so the resulting application works for as long as the GTK
# installation it was built against remains present. What it does
# provide is the bundle layout, the Info.plist, an application icon
# converted from the Windows icon file, and the language files, which
# the programs look for in Contents/Resources/lang.
#
# Usage:
#   make-macos-bundle.sh --app APP.app --prog PROG --binary BINARY \
#       --name NAME --identifier ID --lang LANGDIR \
#       [--icon ICONFILE] [--version VERSION]
#
# PROG is the name the program will have inside the bundle, and is also
# the name used to look up its language file, so it should be the
# lower-case program name: putty, pterm, puttygen, pageant.

set -e

app="" prog="" binary="" name="" identifier="" langdir="" icon="" version=""

while [ $# -gt 0 ]; do
    case "$1" in
        --app)        app=$2;       shift 2 ;;
        --prog)       prog=$2;      shift 2 ;;
        --binary)     binary=$2;    shift 2 ;;
        --name)       name=$2;      shift 2 ;;
        --identifier) identifier=$2; shift 2 ;;
        --lang)       langdir=$2;   shift 2 ;;
        --icon)       icon=$2;      shift 2 ;;
        --version)    version=$2;   shift 2 ;;
        *) echo "make-macos-bundle.sh: unrecognised argument '$1'" >&2
           exit 2 ;;
    esac
done

for var in app prog binary name identifier langdir; do
    eval "val=\$$var"
    if [ -z "$val" ]; then
        echo "make-macos-bundle.sh: missing --$var argument" >&2
        exit 2
    fi
done

if [ ! -f "$binary" ]; then
    echo "make-macos-bundle.sh: no such binary '$binary'" >&2
    exit 1
fi
if [ ! -d "$langdir" ]; then
    echo "make-macos-bundle.sh: no such language directory '$langdir'" >&2
    exit 1
fi

[ -n "$version" ] || version="0.85"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$binary" "$app/Contents/MacOS/$prog"
chmod 755 "$app/Contents/MacOS/$prog"

cp -R "$langdir" "$app/Contents/Resources/lang"

# The icon, if we can make one. sips reads the Windows .ico files
# directly, which saves having to keep a separate copy of every icon.
iconfile=""
if [ -n "$icon" ] && [ -f "$icon" ] && command -v sips >/dev/null 2>&1; then
    if sips -s format icns "$icon" \
              --out "$app/Contents/Resources/$prog.icns" >/dev/null 2>&1; then
        iconfile="$prog.icns"
    else
        echo "make-macos-bundle.sh: could not convert '$icon' to icns" >&2
    fi
fi

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>$name</string>
    <key>CFBundleExecutable</key>
    <string>$prog</string>
    <key>CFBundleIconFile</key>
    <string>$iconfile</string>
    <key>CFBundleIdentifier</key>
    <string>$identifier</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>$name</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 1997-2024 Simon Tatham. All rights reserved. macOS port: MIZUTANI, Kimihiko (Kyoto University).</string>
</dict>
</plist>
EOF

if command -v plutil >/dev/null 2>&1; then
    plutil -lint "$app/Contents/Info.plist" >/dev/null
fi

# Sign the bundle with an ad-hoc signature, so that the system treats it
# as a properly formed application even when nobody has set up a signing
# identity.
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$app" >/dev/null 2>&1 || true
fi

touch "$app"

echo "make-macos-bundle.sh: built $app"

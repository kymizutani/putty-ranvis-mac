#!/bin/sh
#
# macos-embed-deps.sh: make an application bundle assembled by
# make-macos-bundle.sh self-contained, so that it runs on a Mac which has
# no Homebrew (and in particular no GTK) installed at all.
#
# The bundles produced by make-macos-bundle.sh contain only the program
# itself; its shared libraries are found in /opt/homebrew at run time.
# This script post-processes such a bundle in place:
#
#   1. it walks the dependency closure with otool -L, following every
#      library it finds, and copies each non-system library into
#      Contents/Frameworks. Libraries are identified by their resolved
#      real path, so the indirection through Homebrew's opt/ and lib/
#      symlink farms does not cause anything to be copied twice. Every
#      identity (-id) and every reference (-change) is then rewritten to
#      @executable_path/../Frameworks/..., whichever spelling (@rpath,
#      @loader_path, an opt path, a Cellar path) the reference originally
#      used.
#
#   2. the gdk-pixbuf loader modules are dlopened at run time rather than
#      linked, so they are not in the closure: they are copied into
#      Contents/Resources/gdk-pixbuf-2.0/<version>/loaders, their own
#      dependencies are pulled into the closure as well, and a loader
#      cache naming the copies is generated.
#
#   3. the data files those libraries go and look for by absolute path -
#      the loader cache, the GSettings schemas used by the file chooser,
#      fontconfig's configuration - are copied into the bundle, and an
#      LSEnvironment dictionary is added to Info.plist so that a
#      double-clicked application is told where to find them.
#
#   4. everything copied in is signed ad hoc, and then the bundle is
#      signed, because editing load commands invalidates the signatures
#      the binaries had when they were built.
#
# GTK input-method and print-backend modules are deliberately not copied.
# Nothing in this program loads them: none of the three bundles dlopens
# any .so at startup when traced with DYLD_PRINT_LIBRARIES, not even when
# an input method is asked for with GTK_IM_MODULE, because the Quartz
# backend does text input with the context built into libgtk; and PuTTY
# prints by piping to a command (unix/printing.c) rather than through
# GtkPrintOperation. GTK looks for the module _cache_ under the prefix
# compiled into libgtk, which no environment variable can move.
#
# fontconfig's font cache is not copied either: it names the fonts of the
# machine this script ran on, and fontconfig notices that a cache built
# for another machine is out of date and rebuilds it in the user's own
# cache directory on first use. The icon themes under Homebrew's share/
# are not copied because GTK's Adwaita theme is compiled into libgtk as a
# GResource; the files in share/ are only symlinks pointing back into the
# Cellar.
#
# The paths written into Info.plist and into the loader cache are
# absolute, so they name the place the bundle was when this script ran.
# If the bundle is moved afterwards, run this script on it again where it
# now is: it will notice that the libraries are already there and only
# put those paths right.
#
# Usage:
#   macos-embed-deps.sh --app APP.app
#
# APP.app must already have been assembled by make-macos-bundle.sh. The
# bundle is modified in place; nothing outside it is written to.

set -e

die() {
    echo "macos-embed-deps.sh: $*" >&2
    exit 1
}

warn() {
    echo "macos-embed-deps.sh: warning: $*" >&2
}

usage() {
    echo "usage: macos-embed-deps.sh --app APP.app" >&2
}

app=""

while [ $# -gt 0 ]; do
    case "$1" in
        --app)    app=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "macos-embed-deps.sh: unrecognised argument '$1'" >&2
           usage; exit 2 ;;
    esac
done

if [ -z "$app" ]; then
    echo "macos-embed-deps.sh: missing --app argument" >&2
    usage
    exit 2
fi
if [ ! -d "$app/Contents/MacOS" ] || [ ! -f "$app/Contents/Info.plist" ]; then
    die "'$app' does not look like an application bundle"
fi
for tool in otool install_name_tool; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        die "'$tool' was not found, so this script needs the Xcode command line tools"
    fi
done

# Work with a canonical absolute path from here on: the paths we write
# into Info.plist and into the loader cache have to be absolute, and
# resolving symlinks once here means we never write a path that goes
# through a symlink farm.
appabs=$(cd "$app" && pwd -P)
contents=$appabs/Contents
frameworks=$contents/Frameworks
resources=$contents/Resources
plist=$contents/Info.plist

# ----------------------------------------------------------------------
# The Info.plist is plain XML written by make-macos-bundle.sh, and
# PlistBuddy is the only thing that should edit it afterwards.
# ----------------------------------------------------------------------

have_plistbuddy=""
if [ -x /usr/libexec/PlistBuddy ]; then
    have_plistbuddy=yes
fi

plist_put() {
    # $1: key path, $2: value.
    if [ -n "$have_plistbuddy" ]; then
        if /usr/libexec/PlistBuddy -c "Print :$1" "$plist" >/dev/null 2>&1; then
            /usr/libexec/PlistBuddy -c "Set :$1 $2" "$plist"
        else
            /usr/libexec/PlistBuddy -c "Add :$1 string $2" "$plist"
        fi
    else
        plutil_key=$(printf '%s' "$1" | tr ':' '.')
        plutil -replace "$plutil_key" -string "$2" "$plist" 2>/dev/null ||
            plutil -insert "$plutil_key" -string "$2" "$plist"
    fi
}

# A bundle that has been embedded already has no references left to
# find, so there is nothing to rewrite. The one thing that can still be
# wrong is the set of absolute paths recorded in Info.plist and in the
# gdk-pixbuf loader cache, because they name the place the bundle was
# when it was processed. If the bundle has since been moved, put them
# right.
relocate_already_embedded() {
    keys=""
    if [ -n "$have_plistbuddy" ]; then
        keys=$(/usr/libexec/PlistBuddy -c "Print :LSEnvironment" "$plist" \
                    2>/dev/null |
                awk '/^    [A-Za-z_0-9]+ = /{ print $1 }')
    fi
    oldroot=""
    while IFS= read -r key; do
        [ -n "$key" ] || continue
        value=$(/usr/libexec/PlistBuddy -c "Print :LSEnvironment:$key" \
                     "$plist" 2>/dev/null)
        case $value in
            *"/Contents/"*)
                oldroot=${value%%"/Contents/"*}
                break
                ;;
        esac
    done <<EOF
$keys
EOF
    if [ -z "$oldroot" ] || [ "$oldroot" = "$appabs" ]; then
        echo "macos-embed-deps.sh: $appabs is already self-contained"
        return 0
    fi
    echo "macos-embed-deps.sh: $appabs was built at $oldroot, updating the paths it records"
    while IFS= read -r key; do
        [ -n "$key" ] || continue
        value=$(/usr/libexec/PlistBuddy -c "Print :LSEnvironment:$key" \
                     "$plist" 2>/dev/null)
        case $value in
            "$oldroot"*) plist_put "LSEnvironment:$key" "$appabs${value#"$oldroot"}" ;;
        esac
    done <<EOF
$keys
EOF
    for cache in "$contents"/Resources/gdk-pixbuf-2.0/*/loaders.cache; do
        if [ -f "$cache" ]; then
            sed -e "s|$oldroot|$appabs|g" "$cache" > "$cache.new" &&
                mv "$cache.new" "$cache"
        fi
    done
    if command -v plutil >/dev/null 2>&1; then
        plutil -lint "$plist" >/dev/null
    fi
    if command -v codesign >/dev/null 2>&1; then
        codesign --force --sign - "$appabs" ||
            warn "could not re-sign the bundle"
    fi
}

if [ -n "$(ls -A "$frameworks" 2>/dev/null)" ]; then
    relocate_already_embedded
    exit 0
fi

# ----------------------------------------------------------------------
# Find the main executable, using the Info.plist that
# make-macos-bundle.sh wrote.
# ----------------------------------------------------------------------

prog=""
if [ -x /usr/libexec/PlistBuddy ]; then
    prog=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" \
                                "$plist" 2>/dev/null || true)
fi
if [ -z "$prog" ] && command -v plutil >/dev/null 2>&1; then
    prog=$(plutil -extract CFBundleExecutable raw "$plist" 2>/dev/null || true)
fi
if [ -z "$prog" ]; then
    prog=$(ls "$contents/MacOS" 2>/dev/null | head -1)
fi
if [ -z "$prog" ] || [ ! -f "$contents/MacOS/$prog" ]; then
    die "cannot work out which program in '$app' is the main executable"
fi
exe=$contents/MacOS/$prog
exe_dir=$contents/MacOS

# ----------------------------------------------------------------------
# Scratch space.
#
#   $tmp/map        lines are <real path of a library we will copy> TAB
#                   <the path it will have, relative to Contents/>
#   $tmp/scanned    real paths whose references we have already read
#   $tmp/queue      real paths whose references we still have to read
#   $tmp/unresolved references we were unable to make sense of
# ----------------------------------------------------------------------

tmp=$(mktemp -d "${TMPDIR:-/tmp}/macos-embed-deps.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
TAB=$(printf '\t')
: > "$tmp/map"
: > "$tmp/scanned"
: > "$tmp/queue"
: > "$tmp/unresolved"

have_realpath=""
if command -v realpath >/dev/null 2>&1; then
    have_realpath=$(command -v realpath)
fi

# Print the fully resolved path of a file, following every symlink,
# because Homebrew's opt/ and lib/ directories are symlink farms.
real_path() {
    if [ -n "$have_realpath" ]; then
        "$have_realpath" "$1" 2>/dev/null && return 0
    fi
    dir=$(dirname "$1")
    base=$(basename "$1")
    if [ -L "$1" ]; then
        link=$(readlink "$1")
        case $link in
            /*) real_path "$link" ;;
            *)  real_path "$dir/$link" ;;
        esac
        return
    fi
    ( cd "$dir" 2>/dev/null && printf '%s/%s\n' "$(pwd -P)" "$base" )
}

# Look up a real path in the map, printing its path relative to
# Contents/Frameworks.
map_lookup() {
    awk -F'\t' -v key="$1" '$1 == key { print $2; found = 1; exit }
                            END { exit !found }' "$tmp/map"
}

# Print the source path of the first mapped library whose file name
# matches the shell pattern given as our argument. We use this to find
# the Homebrew packages the libraries we are shipping came from, so that
# we can pick up the data files installed beside them.
map_find_by_name() {
    pattern=$1
    while IFS= read -r line; do
        src=${line%%"$TAB"*}
        case ${src##*/} in
            $pattern) printf '%s\n' "$src"; return 0 ;;
        esac
    done < "$tmp/map"
    return 1
}

# For a path inside a Homebrew Cellar, print the prefix the Cellar is
# under (/opt/homebrew, or /usr/local on an Intel Mac).
homebrew_prefix_of() {
    path=$1
    while [ "$path" != "/" ] && [ -n "$path" ]; do
        if [ "${path##*/}" = "Cellar" ]; then
            printf '%s\n' "${path%/*}"
            return 0
        fi
        path=${path%/*}
    done
    return 1
}

# Is this file name already used by something we have decided to copy?
name_is_taken() {
    awk -F'\t' -v key="$1" '{ n = split($2, parts, "/")
                              if (parts[n] == key) { found = 1; exit } }
                            END { exit !found }' "$tmp/map"
}

# Choose the file name a library will have inside Contents/Frameworks.
# Two different libraries with the same file name would overwrite each
# other, so the second and any later one get a numeric suffix.
unique_name() {
    base=${1##*/}
    candidate=$base
    n=1
    while name_is_taken "$candidate"; do
        n=$((n + 1))
        if [ "$base" = "${base#*.}" ]; then
            candidate=$base-$n
        else
            candidate=${base%.*}-$n.${base##*.}
        fi
    done
    printf '%s\n' "$candidate"
}

# Add a library to the set we are going to copy, unless it is already in
# it. The map holds the path each library will have relative to
# Contents/. $2, if given, is that path; otherwise a library goes into
# Contents/Frameworks under a name derived from its own.
enqueue_target() {
    if ! map_lookup "$1" >/dev/null; then
        if [ $# -gt 1 ] && [ -n "$2" ]; then
            rel=$2
        else
            rel=Frameworks/$(unique_name "$1")
        fi
        printf '%s\t%s\n' "$1" "$rel" >> "$tmp/map"
        printf '%s\n' "$1" >> "$tmp/queue"
    fi
}

# Print just the reference strings from the output of otool -L.
list_refs() {
    otool -L "$1" 2>/dev/null | awk \
        'substr($0,1,1) == "\t" { l = substr($0,2);
                                  sub(/ \(compatibility.*/, "", l);
                                  sub(/ \(current.*/, "", l);
                                  print l }'
}

# Print the directories an object searches for @rpath-style references.
list_rpaths() {
    otool -l "$1" 2>/dev/null | awk \
        '$1 == "path" { line = $0;
                        sub(/^[ \t]*path[ \t]+/, "", line);
                        sub(/ \(offset.*/, "", line);
                        print line }'
}

# Print the file that a reference in a given object actually refers to.
# $1 is the object containing the reference, $2 the reference string.
resolve_ref() {
    from=$1
    ref=$2
    case $ref in
        @executable_path/*)
            target=$exe_dir/${ref#@executable_path/}
            ;;
        @loader_path/*)
            target=$(dirname "$from")/${ref#@loader_path/}
            ;;
        @rpath/*)
            target=""
            name=${ref#@rpath/}
            list_rpaths "$from" > "$tmp/rpaths"
            while IFS= read -r rpath; do
                # An rpath can itself be relative to the object.
                case $rpath in
                    @loader_path/*)
                        rpath=$(dirname "$from")/${rpath#@loader_path/}
                        ;;
                    @executable_path/*)
                        rpath=$exe_dir/${rpath#@executable_path/}
                        ;;
                    @*)
                        continue
                        ;;
                esac
                if [ -e "$rpath/$name" ]; then
                    target=$rpath/$name
                    break
                fi
            done < "$tmp/rpaths"
            ;;
        @*)
            return 1
            ;;
        *)
            target=$ref
            ;;
    esac
    [ -e "$target" ] || return 1
    printf '%s\n' "$target"
}

# Read the references of one object, and queue anything new we found.
scan_file() {
    target=$1
    if grep -qxF -- "$target" "$tmp/scanned" 2>/dev/null; then
        return 0
    fi
    printf '%s\n' "$target" >> "$tmp/scanned"

    list_refs "$target" > "$tmp/refs"
    while IFS= read -r ref; do
        case $ref in
            /System/*|/usr/lib/*) continue ;;
        esac
        resolved=$(resolve_ref "$target" "$ref" || true)
        if [ -z "$resolved" ]; then
            printf '%s\t%s\n' "$target" "$ref" >> "$tmp/unresolved"
            continue
        fi
        resolved=$(real_path "$resolved" || true)
        if [ -z "$resolved" ] || [ ! -f "$resolved" ]; then
            printf '%s\t%s\n' "$target" "$ref" >> "$tmp/unresolved"
            continue
        fi
        case $resolved in
            /System/*|/usr/lib/*) continue ;;
            "$appabs"/*) continue ;;
        esac
        # Anything else that a program links against has to be a shared
        # library, but be strict rather than copying something odd.
        case $resolved in
            *.dylib|*.so|*.o) ;;
            *) continue ;;
        esac
        enqueue_target "$resolved"
    done < "$tmp/refs"
}

# Walk the closure: process the queue until there is nothing left. The
# queue is drained a level at a time, because scan_file appends to it.
process_queue() {
    while [ -s "$tmp/queue" ]; do
        mv "$tmp/queue" "$tmp/level"
        : > "$tmp/queue"
        while IFS= read -r entry; do
            scan_file "$entry"
        done < "$tmp/level"
    done
}

# ----------------------------------------------------------------------
# The dependency closure of the program itself.
# ----------------------------------------------------------------------

scan_file "$exe"
process_queue

# ----------------------------------------------------------------------
# The gdk-pixbuf loader modules, which gdk-pixbuf dlopens rather than
# links, so they are invisible to otool. We find them beside the copy of
# libgdk_pixbuf that we are already committed to shipping, and mirror
# their directory layout under Contents/Resources: those modules are data
# rather than code that the loader will ask dyld for, and a directory
# inside Contents/Frameworks would be taken for a nested bundle by
# codesign.
# ----------------------------------------------------------------------

pixbuf_src=$(map_find_by_name 'libgdk_pixbuf-*.dylib' || true)
loaders_src=""
loaders_rel=""
if [ -n "$pixbuf_src" ]; then
    pixbuf_libdir=$(dirname "$pixbuf_src")
    for dir in "$pixbuf_libdir"/gdk-pixbuf-2.0/*/loaders; do
        if [ -d "$dir" ]; then
            loaders_src=$dir
            loaders_rel=Resources/gdk-pixbuf-2.0/$(basename "$(dirname "$dir")")/loaders
        fi
    done
fi
if [ -n "$loaders_src" ]; then
    for module in "$loaders_src"/*.so; do
        if [ -f "$module" ]; then
            module_real=$(real_path "$module" || true)
            if [ -n "$module_real" ]; then
                enqueue_target "$module_real" "$loaders_rel/${module##*/}"
            fi
        fi
    done
    # A loader links against whatever it needs to decode its format -
    # libtiff, for example - so their closure has to be walked too.
    process_queue
else
    warn "found no gdk-pixbuf loader modules, so none were copied"
fi

# ----------------------------------------------------------------------
# Copy everything we decided to embed.
# ----------------------------------------------------------------------

mkdir -p "$frameworks"
copied=0
while IFS= read -r line; do
    src=${line%%"$TAB"*}
    rel=${line#*"$TAB"}
    dest=$contents/$rel
    mkdir -p "$(dirname "$dest")"
    rm -f "$dest"
    cp -pL "$src" "$dest"
    # Cellar files are read-only, and we are about to edit them.
    chmod u+w "$dest"
    copied=$((copied + 1))
done < "$tmp/map"

# ----------------------------------------------------------------------
# Rewrite identities and references.
#
# The reference list is read from the original library in Homebrew rather
# than from the copy we just made, because that is the object whose
# @rpath and @loader_path references mean something; the edits are
# applied to the copy.
# ----------------------------------------------------------------------

# Run install_name_tool, swallowing the note that the code signature has
# just been invalidated, which we know about: everything inside the
# bundle is signed again at the end. Anything else it says is passed on,
# and a failure stops the build.
run_install_name_tool() {
    if ! install_name_tool "$@" 2> "$tmp/intl.err"; then
        cat "$tmp/intl.err" >&2
        die "install_name_tool failed"
    fi
    grep -v "invalidate the code signature" "$tmp/intl.err" >&2 || true
}

while IFS= read -r line; do
    rel=${line#*"$TAB"}
    run_install_name_tool -id "@executable_path/../$rel" \
                          "$contents/$rel"
done < "$tmp/map"

rewrite_references_in() {
    # $1: object to read references from, $2: object to edit.
    from=$1
    dest=$2
    list_refs "$from" > "$tmp/refs"
    seen=""
    set --
    while IFS= read -r ref; do
        case $seen in
            *"|$ref|"*) continue ;;
        esac
        seen=$seen"|"$ref"|"
        resolved=$(resolve_ref "$from" "$ref" || true)
        [ -n "$resolved" ] || continue
        resolved=$(real_path "$resolved" || true)
        [ -n "$resolved" ] || continue
        rel=$(map_lookup "$resolved" || true)
        [ -n "$rel" ] || continue
        set -- "$@" -change "$ref" "@executable_path/../$rel"
    done < "$tmp/refs"
    if [ $# -gt 0 ]; then
        run_install_name_tool "$@" "$dest"
    fi
}

rewrite_references_in "$exe" "$exe"
while IFS= read -r line; do
    src=${line%%"$TAB"*}
    rel=${line#*"$TAB"}
    rewrite_references_in "$src" "$contents/$rel"
done < "$tmp/map"

if [ -s "$tmp/unresolved" ]; then
    sort -u "$tmp/unresolved" > "$tmp/unresolved.sorted"
    while IFS= read -r line; do
        from=${line%%"$TAB"*}
        ref=${line#*"$TAB"}
        warn "could not resolve reference '$ref' from ${from##*/}, left it alone"
    done < "$tmp/unresolved.sorted"
fi

# ----------------------------------------------------------------------
# The gdk-pixbuf loader cache.
#
# The query tool dlopens every module in order to ask it what it can
# load, so it has to be run against the originals in Homebrew, which are
# still undisturbed at this point. The paths it prints are then replaced
# by the paths of the copies inside the bundle. If no query tool is
# installed, the cache file that came with gdk-pixbuf is rewritten
# instead; it names the same modules.
# ----------------------------------------------------------------------

cache_file=""
if [ -n "$loaders_src" ]; then
    cache_file=$contents/${loaders_rel%/*}/loaders.cache
    query_tool=""
    pkg_bin=$(dirname "$(dirname "$loaders_src")")/bin
    for candidate in "$pkg_bin/gdk-pixbuf-query-loaders" \
                     "$pkg_bin/gdk-pixbuf-query-loader"; do
        if [ -x "$candidate" ]; then
            query_tool=$candidate
            break
        fi
    done
    if [ -z "$query_tool" ]; then
        for name in gdk-pixbuf-query-loaders gdk-pixbuf-query-loader; do
            if command -v "$name" >/dev/null 2>&1; then
                query_tool=$(command -v "$name")
                break
            fi
        done
    fi

    if [ -n "$query_tool" ]; then
        if ! "$query_tool" > "$tmp/loaders.cache" 2>/dev/null; then
            warn "'$query_tool' failed, using the cache file that came with gdk-pixbuf"
            query_tool=""
        fi
    fi
    if [ -z "$query_tool" ] && [ -f "$loaders_src/../loaders.cache" ]; then
        cp "$loaders_src/../loaders.cache" "$tmp/loaders.cache"
    fi

    if [ -f "$tmp/loaders.cache" ]; then
        mkdir -p "$(dirname "$cache_file")"
        while IFS= read -r line; do
            case $line in
                '"'*)
                    body=${line#'"'}
                    module=${body%%'"'*}
                    rest=${body#*'"'}
                    case $module in
                        /*.so|/*.dylib) ;;
                        *) printf '%s\n' "$line"; continue ;;
                    esac
                    module_real=$(real_path "$module" || true)
                    rel=""
                    if [ -n "$module_real" ]; then
                        rel=$(map_lookup "$module_real" || true)
                    fi
                    if [ -n "$rel" ]; then
                        printf '"%s/%s"%s\n' "$contents" "$rel" "$rest"
                    else
                        warn "loader '$module' was not copied, its cache entry is unchanged"
                        printf '%s\n' "$line"
                    fi
                    ;;
                '# LoaderDir = '*)
                    printf '# LoaderDir = %s/%s\n' "$contents" "$loaders_rel"
                    ;;
                *)
                    printf '%s\n' "$line"
                    ;;
            esac
        done < "$tmp/loaders.cache" > "$cache_file"
    else
        warn "no way to generate a gdk-pixbuf loader cache, the loaders may not be found"
        cache_file=""
    fi
fi

# ----------------------------------------------------------------------
# Data files that the libraries look for by absolute path.
#
# GSettings schemas: the GTK file chooser calls g_settings_new() for
# org.gtk.Settings.FileChooser, and glib aborts if that schema is not
# installed, so the compiled schemas have to travel with the program.
# glib looks in $XDG_DATA_DIRS/glib-2.0/schemas, and in the directory
# named by GSETTINGS_SCHEMA_DIR.
#
# fontconfig: libfontconfig looks in a sysconfdir compiled into it,
# which here is Homebrew's etc/fonts. On a Mac with no Homebrew that
# directory does not exist and fontconfig is left with no font
# directories at all, so the configuration is copied in and named
# explicitly. Every font directory it names is an Apple system one, which
# is present on any Mac; its cache directories are not copied, so
# fontconfig rebuilds its cache in the user's own cache directory the
# first time the program is run.
# ----------------------------------------------------------------------

share=$resources/share

glib_prefix=""
glib_src=$(map_find_by_name 'libglib-2.0.*.dylib' || true)
if [ -n "$glib_src" ]; then
    glib_prefix=$(homebrew_prefix_of "$glib_src" || true)
fi
if [ -n "$glib_prefix" ] && [ -d "$glib_prefix/share/glib-2.0/schemas" ]; then
    schemas_src=$glib_prefix/share/glib-2.0/schemas
else
    schemas_src=""
fi
if [ -n "$schemas_src" ]; then
    rm -rf "$share/glib-2.0/schemas"
    mkdir -p "$share/glib-2.0/schemas"
    # glib only ever reads gschemas.compiled at run time; the .xml files
    # are input to glib-compile-schemas, and are what we have to fall
    # back on if there is no compiled file to copy.
    if [ -f "$schemas_src/gschemas.compiled" ]; then
        cp -L "$schemas_src/gschemas.compiled" "$share/glib-2.0/schemas/"
    else
        for schema in "$schemas_src"/*.xml; do
            if [ -f "$schema" ]; then
                cp -L "$schema" "$share/glib-2.0/schemas/"
            fi
        done
        if command -v glib-compile-schemas >/dev/null 2>&1; then
            glib-compile-schemas "$share/glib-2.0/schemas" 2>/dev/null || true
        else
            warn "there are no compiled GSettings schemas and glib-compile-schemas is not installed"
        fi
    fi
fi

fc_src=$(map_find_by_name 'libfontconfig*.dylib' || true)
fc_config=""
if [ -n "$fc_src" ]; then
    fc_pkg=$(dirname "$(dirname "$fc_src")")
    # Homebrew keeps a formula's etc/ in the prefix rather than beside
    # its libraries, with the bottle's copy left in the Cellar.
    brew_prefix=$(homebrew_prefix_of "$fc_src" || true)
    for etcdir in "${brew_prefix:+$brew_prefix/etc/fonts}" \
                  "$fc_pkg/.bottle/etc/fonts" "$fc_pkg/etc/fonts"; do
        if [ -f "$etcdir/fonts.conf" ]; then
            mkdir -p "$resources/etc"
            rm -rf "$resources/etc/fonts"
            # -L because Homebrew's etc/ is a symlink farm: the copies
            # have to be real files, since /opt/homebrew will not be
            # there on the machine we are shipping to.
            cp -RL "$etcdir" "$resources/etc/fonts"
            # Homebrew's own font cache directory is not going to exist
            # on the machine we are shipping to, and fontconfig
            # grumbles about a cache directory it cannot write to, so
            # drop that one line. The cache directories the rest of the
            # configuration names are the user's own, which are always
            # there.
            if [ -n "$brew_prefix" ]; then
                awk -v grumble="<cachedir>${brew_prefix}/" \
                    'index($0, grumble) == 0' \
                    "$resources/etc/fonts/fonts.conf" \
                    > "$resources/etc/fonts/fonts.conf.new" &&
                    mv "$resources/etc/fonts/fonts.conf.new" \
                       "$resources/etc/fonts/fonts.conf"
            fi
            fc_config=$resources/etc/fonts
            break
        fi
    done
fi

# ----------------------------------------------------------------------
# Tell the program where all of that is. LSEnvironment is applied by
# LaunchServices when the application is opened, which is the case we
# care about. A program started from a shell does not get these
# variables, but that is already true of the bundle's other properties;
# to test one from a shell, set the same variables yourself.
# ----------------------------------------------------------------------

if [ -n "$have_plistbuddy" ]; then
    /usr/libexec/PlistBuddy -c "Print :LSEnvironment" "$plist" >/dev/null 2>&1 ||
        /usr/libexec/PlistBuddy -c "Add :LSEnvironment dict" "$plist"
elif command -v plutil >/dev/null 2>&1; then
    plutil -insert LSEnvironment -dict "$plist" 2>/dev/null || true
fi

if [ -n "$cache_file" ]; then
    plist_put "LSEnvironment:GDK_PIXBUF_MODULE_FILE" "$cache_file"
    plist_put "LSEnvironment:GDK_PIXBUF_MODULEDIR" "$contents/$loaders_rel"
fi
if [ -d "$share/glib-2.0/schemas" ]; then
    plist_put "LSEnvironment:GSETTINGS_SCHEMA_DIR" "$share/glib-2.0/schemas"
fi
if [ -d "$share" ]; then
    # Ours first, but keep the standard places as well so that anything
    # the user has installed in the usual XDG directories still works.
    plist_put "LSEnvironment:XDG_DATA_DIRS" "$share:/usr/local/share:/usr/share"
    plist_put "LSEnvironment:GTK_DATA_PREFIX" "$resources"
fi
if [ -n "$fc_config" ]; then
    plist_put "LSEnvironment:FONTCONFIG_FILE" "$fc_config/fonts.conf"
    plist_put "LSEnvironment:FONTCONFIG_PATH" "$fc_config"
fi

if command -v plutil >/dev/null 2>&1; then
    plutil -lint "$plist" >/dev/null
fi

# ----------------------------------------------------------------------
# Check our own work: nothing should still name a library outside the
# bundle.
# ----------------------------------------------------------------------

: > "$tmp/targets"
printf '%s\n' "$exe" >> "$tmp/targets"
while IFS= read -r line; do
    rel=${line#*"$TAB"}
    printf '%s\n' "$contents/$rel" >> "$tmp/targets"
done < "$tmp/map"

remaining=0
while IFS= read -r target; do
    list_refs "$target" > "$tmp/refs"
    while IFS= read -r ref; do
        case $ref in
            /opt/*|/usr/local/*|/usr/X11*)
                echo "macos-embed-deps.sh: ${target##*/} still refers to $ref" >&2
                remaining=$((remaining + 1))
                ;;
        esac
    done < "$tmp/refs"
done < "$tmp/targets"
if [ "$remaining" -gt 0 ]; then
    warn "$remaining references still point outside the bundle"
fi

# ----------------------------------------------------------------------
# Signature. Editing load commands invalidates the ad-hoc signature the
# binaries were built with, and the bundle cannot be signed until
# everything inside it is signed, so we sign from the inside out.
# ----------------------------------------------------------------------

if command -v codesign >/dev/null 2>&1; then
    while IFS= read -r target; do
        case $target in
            "$exe") continue ;;
        esac
        codesign --force --sign - "$target" 2>/dev/null ||
            warn "could not sign ${target##*/}"
    done < "$tmp/targets"
    codesign --force --sign - "$appabs" ||
        warn "could not sign the bundle"
    codesign --verify --strict --deep "$appabs" ||
        warn "the signature of '$appabs' does not verify"
else
    warn "codesign was not found, so nothing was signed"
fi

echo "macos-embed-deps.sh: embedded $copied libraries and their data in $appabs"
size=$(du -sh "$appabs" 2>/dev/null | awk '{ print $1 }')
echo "macos-embed-deps.sh: the bundle is now $size in total"

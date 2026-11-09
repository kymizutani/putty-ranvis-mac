/*
 * Find the data files that belong to us.
 *
 * On macOS, the application bundles built by unix/make-macos-bundle.sh
 * and filled out by unix/macos-embed-deps.sh contain copies of the
 * libraries we link against, along with the data those libraries go
 * looking for at run time: the gdk-pixbuf loader cache, the GSettings
 * schemas the file chooser needs, fontconfig's configuration. The
 * library copies find each other by paths relative to themselves, but
 * the data files are looked up by absolute path, so somebody has to
 * tell the libraries where they are.
 *
 * The bundle does record that in its Info.plist, in the form of
 * environment variables that LaunchServices sets up when you double-click
 * the application. But those are absolute paths written down when the
 * bundle was built, so they stop being the right answer as soon as the
 * bundle is moved - to /Applications, to another machine, or to the
 * mount point of a disk image. So we work the paths out here instead,
 * from where the executable turns out to actually be, and use the
 * recorded values only if we can't find our own data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "putty.h"

void setup_macos_bundle_environment(void)
{
#ifdef __APPLE__
    char exe[PATH_MAX], real[PATH_MAX], path[2 * PATH_MAX];
    char *marker, *last, *prefix;
    static const struct {
        const char *envvar, *relative, is_dir;
    } things[] = {
        { "GDK_PIXBUF_MODULE_FILE",
          "Contents/Resources/gdk-pixbuf-2.0/2.10.0/loaders.cache", 0 },
        { "GDK_PIXBUF_MODULEDIR",
          "Contents/Resources/gdk-pixbuf-2.0/2.10.0/loaders", 1 },
        { "GSETTINGS_SCHEMA_DIR",
          "Contents/Resources/share/glib-2.0/schemas", 1 },
        { "FONTCONFIG_FILE",
          "Contents/Resources/etc/fonts/fonts.conf", 0 },
        { "FONTCONFIG_PATH",
          "Contents/Resources/etc/fonts", 1 },
    };
    unsigned i;

    /*
     * Where are we? _NSGetExecutablePath gives us the path we were
     * started with, which might have symlinks in it, and a bundle that
     * has been copied around can be reached through them, so we resolve
     * it first.
     */
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size) != 0)
        return;
    if (!realpath(exe, real))
        return;

    /*
     * If we're inside a bundle at all, the path looks like
     *
     *     <anything>/<Name>.app/Contents/MacOS/<program>
     *
     * so everything up to and including "/Contents" is the prefix we
     * want. The last occurrence of the marker is the right one to
     * break at, in case a directory above the bundle has that name too.
     */
    last = NULL;
    for (marker = real;
         (marker = strstr(marker, "/Contents/MacOS/")) != NULL;
         marker += 1)
        last = marker;
    if (!last)
        return;
    *last = '\0';
    prefix = real;

    /*
     * Tell the libraries where our copies of their data are. Anything
     * we can't find in the bundle - because it wasn't embedded, or
     * because this program isn't one that needs it - we leave alone, so
     * that a value somebody else has set up still works.
     */
    for (i = 0; i < lenof(things); i++) {
        struct stat st;

        snprintf(path, sizeof path, "%s/%s", prefix, things[i].relative);
        if (stat(path, &st) != 0)
            continue;
        if (things[i].is_dir && !S_ISDIR(st.st_mode))
            continue;
        setenv(things[i].envvar, path, 1);
    }

    /*
     * The prefix is also something GTK itself understands: it's where
     * GTK looks for its settings and icon themes, and it's the thing
     * the gdk-pixbuf and GSettings paths are relative to in GTK's own
     * scheme of things.
     */
    setenv("GTK_DATA_PREFIX", prefix, 1);

    /*
     * And the file chooser's schemas are found through the ordinary
     * data-directory mechanism too, which wants the standard system
     * locations kept in as well.
     */
    snprintf(path, sizeof path, "%s/Contents/Resources/share:/usr/local/share:"
            "/usr/share", prefix);
    setenv("XDG_DATA_DIRS", path, 1);
#endif /* __APPLE__ */
}

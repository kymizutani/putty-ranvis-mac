/*
 * uxl10n.c: translation of user-interface strings, for the Unix front
 * ends (including the OS X / macOS one).
 *
 * The Windows front end achieves this by hooking the Win32 control
 * system, and reads its translations using GetPrivateProfileString().
 * Here we implement that same file format from scratch, so that the
 * language files in windows/lang can be used by both front ends.
 *
 * Summary of the file format:
 *
 *  - The file is divided into sections, whose names appear in square
 *    brackets. The section named by the "Language" key in the
 *    "[Default]" section is the one that contains the translations.
 *
 *  - In that section, every line is "<source string>=<translation>",
 *    and a ';' at the start of a line introduces a comment. Both the
 *    key and the value are written with a URL-like escaping, in which
 *    characters that would confuse the file format (control
 *    characters, '=', '%', and leading or trailing spaces) appear as
 *    '%XX'.
 *
 *  - A file whose "[Default]" section provides "CanonicalName"
 *    instead of "Language" holds no translations of its own, and
 *    defers to the named file in the same directory. (pterm.lng and
 *    puttytel.lng work that way, sharing putty.lng.)
 *
 *  - Keys beginning with an underscore are settings, not translations,
 *    and are made available through get_l10n_setting().
 *
 * Those files are stored in UTF-16LE with a byte-order mark (see
 * windows/lang/.gitattributes), so we convert their contents to UTF-8
 * while reading them. UTF-8 files are accepted too.
 *
 * Which file is used is decided like this:
 *
 *  - $PUTTY_LANGFILE, if set, names a language file directly. Setting
 *    it to the empty string, or $PUTTY_LANG to "none", turns
 *    translation off.
 *
 *  - Otherwise, we find the name of the running program, and look for
 *    a file of that name in a "lang" subdirectory of a number of
 *    plausible places, using the system locale (or $PUTTY_LANG) to
 *    decide which language subdirectory to try. On OS X and macOS, a
 *    program launched from the Finder has no locale in its environment
 *    at all, so if nothing is set we ask the operating system which
 *    language the user has chosen.
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "putty.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Where the language files will be installed, if the program can't
 * find them near itself. */
#ifndef PUTTY_DATADIR
#define PUTTY_DATADIR ""
#endif

#define LNG_HASH_SIZE 1024
#define LNG_SECTION_MAX 256

/* Copy a string into a fixed-size buffer, truncating if necessary. */
static void lng_strlcpy(char *dest, size_t size, const char *src)
{
    if (size == 0)
        return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/* Duplicate the first `len' bytes of a string. */
static char *lng_dupsubstr(const char *s, size_t len)
{
    char *ret = snewn(len + 1, char);
    memcpy(ret, s, len);
    ret[len] = '\0';
    return ret;
}

struct lng_entry {
    struct lng_entry *next;
    char *key;
    char *value;
};

static struct lng_entry *lng_hash[LNG_HASH_SIZE];
static bool lng_init_done = false;
static char *lng_file_path = NULL;
static char lng_section[LNG_SECTION_MAX];

static unsigned long lng_hash_key(const char *s)
{
    /* djb2, which is good enough for a few thousand short strings. */
    unsigned long h = 5381;
    while (*s)
        h = h * 33 ^ (unsigned char)*s++;
    return h % LNG_HASH_SIZE;
}

static const char *lng_find(const char *key);

/*
 * Windows marks the mnemonic key of a menu or dialog label by putting
 * an '&' in front of it, and Japanese translations tend to put the
 * marker at the end in brackets, as in "コピー(&C)". This takes those
 * bracketed markers out, and writes a real ampersand for the "&&" that
 * Windows uses for one. That's all we want done to a translation we're
 * about to show the user, because other kinds of string - the log file
 * name, for instance, where "&Y" means the year - use '&' for
 * something quite different.
 */
static char *lng_strip_bracketed_mnemonics(const char *s)
{
    char *out = snewn(strlen(s) + 1, char), *p = out;

    while (*s) {
        if (s[0] == '(' && s[1] == '&' && s[2] && s[3] == ')') {
            s += 4;                       /* a whole "(&C)" group */
            continue;
        }
        if (s[0] == '&' && s[1] == '&') {
            *p++ = '&';
            s += 2;
            continue;
        }
        *p++ = *s++;
    }
    *p = '\0';
    return out;
}

/*
 * The above, and also take out the bare '&' markers that Windows puts
 * in the middle of a label, as in "Ne&w Session...". Used on the keys
 * of the language file, so that a lookup from this front end, which
 * has no such markers, can still find the translation; and on menu
 * labels, which are the one place we know an '&' can only be a
 * mnemonic.
 */
static char *lng_mnemonic_free(const char *s)
{
    char *out = lng_strip_bracketed_mnemonics(s), *p = out;
    char *src = out;

    while (*src) {
        if (*src == '&') {
            src++;
            continue;
        }
        *p++ = *src++;
    }
    *p = '\0';
    return out;
}

static void lng_store(char *key, char *value)
{
    struct lng_entry *entry;
    char *alias;

    for (entry = lng_hash[lng_hash_key(key)]; entry; entry = entry->next) {
        if (!strcmp(entry->key, key)) {
            sfree(entry->value);
            entry->value = value;
            sfree(key);
            return;
        }
    }

    entry = snew(struct lng_entry);
    entry->key = key;
    entry->value = value;
    entry->next = lng_hash[lng_hash_key(key)];
    lng_hash[lng_hash_key(key)] = entry;

    /*
     * The Windows front end writes its menu and dialog labels with '&'
     * marking the mnemonic key, so the language files are keyed that
     way: "Ne&w Session...". Our labels don't have that, and we don't
     * want it either, because there's no Alt-key on a Mac to press. So
     * each string is also filed on record with those markers taken out,
     * which is what lets a lookup from our side find the translation.
     */
    alias = lng_mnemonic_free(key);
    if (strcmp(alias, key) && !lng_find(alias))
        lng_store(alias, dupstr(value));
}

static const char *lng_find(const char *key)
{
    struct lng_entry *entry;

    for (entry = lng_hash[lng_hash_key(key)]; entry; entry = entry->next)
        if (!strcmp(entry->key, key))
            return entry->value;
    return NULL;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/*
 * Undo the %-escaping used in the language file. Each escape is one
 * byte; a sequence of them can spell any byte at all, so we emit them
 * without trying to interpret them as characters in their own right.
 */
static char *lng_unescape(const char *text, size_t len)
{
    char *out = snewn(len + 1, char);
    char *q = out;
    size_t i;

    for (i = 0; i < len; i++) {
        if (text[i] == '%' && i + 2 < len) {
            int hi = hexval(text[i+1]);
            int lo = hi < 0 ? -1 : hexval(text[i+2]);
            if (hi >= 0 && lo >= 0) {
                *q++ = (char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        *q++ = text[i];
    }

    *q = '\0';
    return out;
}

/*
 * Convert a UTF-16LE string (with no byte-order mark) into a newly
 * allocated UTF-8 one.
 */
static char *utf16le_to_utf8(const char *input, size_t inlen, size_t *outlen)
{
    const unsigned char *p = (const unsigned char *)input;
    size_t nunits = inlen / 2;
    char *out = snewn(nunits * 3 + 4, char);
    char *q = out;
    size_t i;

    for (i = 0; i < nunits; i++) {
        unsigned long c = p[2*i] | (p[2*i+1] << 8);

        if (c >= 0xD800 && c < 0xDC00 && i + 1 < nunits) {
            unsigned long low = p[2*(i+1)] | (p[2*(i+1)+1] << 8);
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }

        if (c < 0x80) {
            *q++ = c;
        } else if (c < 0x800) {
            *q++ = 0xC0 | (c >> 6);
            *q++ = 0x80 | (c & 0x3F);
        } else if (c < 0x10000) {
            *q++ = 0xE0 | (c >> 12);
            *q++ = 0x80 | ((c >> 6) & 0x3F);
            *q++ = 0x80 | (c & 0x3F);
        } else {
            *q++ = 0xF0 | (c >> 18);
            *q++ = 0x80 | ((c >> 12) & 0x3F);
            *q++ = 0x80 | ((c >> 6) & 0x3F);
            *q++ = 0x80 | (c & 0x3F);
        }
    }

    *outlen = q - out;
    return out;
}

/*
 * Read a whole file into memory, and (if it isn't already UTF-8)
 * convert it to UTF-8.
 */
static char *read_language_file(const char *path, size_t *len_out)
{
    FILE *fp;
    char *data;
    size_t size = 0, capacity = 0;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    data = NULL;
    while (true) {
        size_t got;
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 0x10000;
            data = sresize(data, capacity, char);
        }
        got = fread(data + size, 1, capacity - size, fp);
        if (got == 0)
            break;
        size += got;
    }
    fclose(fp);

    if (size >= 2 &&
        (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        char *utf8 = utf16le_to_utf8(data + 2, size - 2, len_out);
        sfree(data);
        return utf8;
    }
    if (size >= 2 && data[1] == '\0') {
        /* UTF-16LE, but without the byte-order mark. */
        char *utf8 = utf16le_to_utf8(data, size, len_out);
        sfree(data);
        return utf8;
    }
    if (size >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        memmove(data, data + 3, size - 3);
        size -= 3;
    }

    *len_out = size;
    return data;
}

/*
 * Iterate over the sections and key-value lines of a language file.
 */
struct lng_reader {
    const char *p, *end;
    char section[LNG_SECTION_MAX];
};

static void lng_reader_init(struct lng_reader *r, const char *data,
                            size_t len)
{
    r->p = data;
    r->end = data + len;
    r->section[0] = '\0';
}

/*
 * Fetch the next key-value line. Section headers are consumed, and the
 * name of the section the line belongs to is returned in r->section.
 * Keys and values are unescaped. Returns false at end of file.
 */
static bool lng_next_line(struct lng_reader *r, char **key_out,
                          char **value_out)
{
    while (r->p < r->end) {
        const char *nl = memchr(r->p, '\n', r->end - r->p);
        const char *line_end = nl ? nl : r->end;
        const char *p = r->p;
        const char *eq, *start, *finish;
        size_t seclen;

        r->p = nl ? nl + 1 : r->end;

        while (line_end > p && (line_end[-1] == '\r' || line_end[-1] == ' ' ||
                                line_end[-1] == '\t'))
            line_end--;
        while (p < line_end && (*p == ' ' || *p == '\t'))
            p++;

        if (p == line_end || *p == ';')
            continue;

        if (*p == '[') {
            const char *close = memchr(p, ']', line_end - p);
            if (!close)
                continue;
            start = p + 1;
            finish = close;
            while (finish > start && (finish[-1] == ' ' || finish[-1] == '\t'))
                finish--;
            seclen = finish - start;
            if (seclen >= sizeof r->section)
                seclen = sizeof r->section - 1;
            memcpy(r->section, start, seclen);
            r->section[seclen] = '\0';
            continue;
        }

        eq = memchr(p, '=', line_end - p);
        if (!eq)
            continue;

        start = p;
        finish = eq;
        while (finish > start && (finish[-1] == ' ' || finish[-1] == '\t'))
            finish--;
        if (finish == start)
            continue;

        *key_out = lng_unescape(start, finish - start);
        *value_out = lng_unescape(eq + 1, line_end - (eq + 1));
        return true;
    }

    return false;
}

/*
 * Look up one raw value in one named section of a file that we haven't
 * loaded into the hash table yet.
 */
static char *lng_raw_setting(const char *data, size_t len,
                             const char *section, const char *key)
{
    struct lng_reader r;
    char *line_key, *line_value;

    lng_reader_init(&r, data, len);
    while (lng_next_line(&r, &line_key, &line_value)) {
        bool match = !strcmp(r.section, section) && !strcmp(line_key, key);
        if (!match) {
            sfree(line_key);
            sfree(line_value);
        } else {
            sfree(line_key);
            return line_value;
        }
    }
    return NULL;
}

static void lng_load_section(const char *data, size_t len,
                             const char *section)
{
    struct lng_reader r;
    char *line_key, *line_value;

    lng_reader_init(&r, data, len);
    while (lng_next_line(&r, &line_key, &line_value)) {
        if (strcmp(r.section, section)) {
            sfree(line_key);
            sfree(line_value);
        } else {
            lng_store(line_key, line_value);
        }
    }
}

static char *file_by_basename(const char *path, const char *newname)
{
    const char *slash = strrchr(path, '/');

    if (!slash)
        return dupstr(newname);
    return dupprintf("%.*s/%s", (int)(slash - path), path, newname);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

/*
 * Load one language file. If it defers to another one by name, follow
 * that link (at most a few times, in case of a cycle).
 */
static bool load_language_file(const char *path, int depth)
{
    char *data, *language, *canonical;
    size_t len;
    bool loaded = false;

    data = read_language_file(path, &len);
    if (!data)
        return false;

    language = lng_raw_setting(data, len, "Default", "Language");
    canonical = lng_raw_setting(data, len, "Default", "CanonicalName");

    if (!language && canonical && depth < 5) {
        /* This file defers to another one in the same directory. */
        char *next = file_by_basename(path, canonical);
        bool ok = file_exists(next) && load_language_file(next, depth + 1);
        if (ok) {
            sfree(lng_file_path);
            lng_file_path = dupstr(path);
        }
        loaded = ok;
        sfree(next);
    } else if (language && *language) {
        lng_load_section(data, len, language);
        lng_strlcpy(lng_section, sizeof lng_section, language);
        sfree(lng_file_path);
        lng_file_path = dupstr(path);
        loaded = true;
    }

    sfree(language);
    sfree(canonical);
    sfree(data);
    return loaded;
}

/*
 * Where is the running program, and what is it called?
 */
static char *my_executable_path(void)
{
#ifdef __APPLE__
    char buf[PATH_MAX];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0)
        return dupstr(buf);
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (len > 0) {
        buf[len] = '\0';
        return dupstr(buf);
    }
#endif
    return NULL;
}

/*
 * Language names that the user's locale might lead us to try, in order
 * of preference, together with the equivalent Windows locale
 * identifiers (which is what the language directories in this source
 * tree are named after).
 */
#define LNG_MAX_CANDIDATES 16
struct lng_candidates {
    char *cand[LNG_MAX_CANDIDATES];
    int ncand;
};

static void lng_add_candidate(struct lng_candidates *cs, const char *name)
{
    int i;
    size_t len;

    if (!name || !*name || cs->ncand >= LNG_MAX_CANDIDATES)
        return;
    len = strlen(name);
    while (len && (name[len-1] == '.' || name[len-1] == '-'))
        len--;
    if (!len)
        return;

    for (i = 0; i < cs->ncand; i++)
        if (!strcasecmp(cs->cand[i], name))
            return;

    cs->cand[cs->ncand++] = dupstr(name);
}

static void lng_add_locale(struct lng_candidates *cs, const char *locale)
{
    static const struct {
        const char *tag, *lcid;
    } lcids[] = {
        { "ja", "0411" }, { "ko", "0412" }, { "zh", "0804" },
        { "zh_CN", "0804" }, { "zh_TW", "0404" }, { "fr", "040C" },
        { "de", "0407" }, { "es", "040A" }, { "it", "0410" },
        { "ru", "0419" }, { "pt", "0416" }, { "pl", "0415" },
    };
    char bare[64];
    size_t i, j;
    int lcid_index = -1;

    if (!locale || !*locale)
        return;
    if (!strcmp(locale, "C") || !strcmp(locale, "POSIX"))
        return;                  /* the language the programs are written in */

    /* Anything after a '.' or '@' is the codeset or a modifier. */
    lng_add_candidate(cs, locale);
    for (i = 0; i < sizeof bare - 1 && locale[i] &&
              locale[i] != '.' && locale[i] != '@'; i++)
        bare[i] = locale[i];
    bare[i] = '\0';
    lng_add_candidate(cs, bare);

    /* Just the language part of "ja_JP", for example. */
    if ((j = strcspn(bare, "-_")) > 0 && j < sizeof bare) {
        char lang[64];
        memcpy(lang, bare, j);
        lang[j] = '\0';
        lng_add_candidate(cs, lang);
        for (i = 0; i < lenof(lcids); i++)
            if (!strcasecmp(lcids[i].tag, lang))
                lcid_index = i;
        if (lcid_index >= 0)
            lng_add_candidate(cs, lcids[lcid_index].lcid);
    }
}

static void lng_free_candidates(struct lng_candidates *cs)
{
    int i;
    for (i = 0; i < cs->ncand; i++)
        sfree(cs->cand[i]);
    cs->ncand = 0;
}

#ifdef __APPLE__
/*
 * A program started by the Finder from an application bundle has no
 * locale in its environment at all, so on Mac we ask the operating
 * system which language the user is using.
 *
 * We deliberately do not just call CFLocaleCopyCurrent(): inside an
 * application bundle that takes the bundle's development region into
 * account, so it reports English for a Japanese-speaking user unless
 * the bundle itself has been localised into Japanese - which ours
 * haven't been, because we do our own localisation. Instead we read
 * the user's own preference settings, which are exactly what they chose
 * in the System Settings application: the list of interface languages,
 * falling back to the regional format setting.
 */
static void lng_add_pref_value(struct lng_candidates *cs, CFStringRef key,
                               CFStringRef appdomain)
{
    CFPropertyListRef pref = CFPreferencesCopyValue(
        key, appdomain, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);

    if (pref) {
        if (CFGetTypeID(pref) == CFStringGetTypeID()) {
            char value[256];
            if (CFStringGetCString((CFStringRef)pref, value, sizeof value,
                                   kCFStringEncodingUTF8))
                lng_add_locale(cs, value);
        } else if (CFGetTypeID(pref) == CFArrayGetTypeID()) {
            CFArrayRef array = (CFArrayRef)pref;
            CFIndex i;
            for (i = 0; i < CFArrayGetCount(array) && i < 4; i++) {
                CFStringRef item = (CFStringRef)CFArrayGetValueAtIndex(array, i);
                char value[256];
                if (CFGetTypeID(item) == CFStringGetTypeID() &&
                    CFStringGetCString(item, value, sizeof value,
                                       kCFStringEncodingUTF8))
                    lng_add_locale(cs, value);
            }
        }
        CFRelease(pref);
    }
}

/*
 * Each of these settings is looked up first in the application's own
 * preference domain, so that the per-application language override
 * offered by System Settings has an effect, and then in the global one.
 */
static void lng_add_pref_string(struct lng_candidates *cs, const char *name)
{
    CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, name,
                                                kCFStringEncodingUTF8);

    if (!key)
        return;

    lng_add_pref_value(cs, key, kCFPreferencesCurrentApplication);
    if (!cs->ncand)
        lng_add_pref_value(cs, key, kCFPreferencesAnyApplication);
    CFRelease(key);
}

static void lng_add_macos_locale(struct lng_candidates *cs)
{
    lng_add_pref_string(cs, "AppleLanguages");
    if (!cs->ncand)
        lng_add_pref_string(cs, "AppleLocale");

    if (!cs->ncand) {
        /* Last resort, if the preference settings couldn't be read. */
        CFLocaleRef locale = CFLocaleCopyCurrent();
        if (locale) {
            CFStringRef ident = CFLocaleGetIdentifier(locale);
            char name[256];
            if (ident && CFStringGetCString(ident, name, sizeof name,
                                            kCFStringEncodingUTF8))
                lng_add_locale(cs, name);
            CFRelease(locale);
        }
    }
}
#endif /* __APPLE__ */

static void l10n_init(void)
{
    struct lng_candidates cs;
    char *exec_path, *exec_dir, *prog_name;
    char *prog_names[2];
    int nprog = 0, i, j, k;
    bool locale_env_was_set = false;

    if (lng_init_done)
        return;
    lng_init_done = true;

    /* Explicit override, or explicit refusal. */
    {
        const char *override = getenv("PUTTY_LANGFILE");
        if (override) {
            if (*override && load_language_file(override, 0))
                return;
            if (!*override)
                return;
        }
    }
    {
        const char *lang = getenv("PUTTY_LANG");
        if (lang && (!strcasecmp(lang, "none") || !strcmp(lang, "-")))
            return;
    }

    cs.ncand = 0;
    {
        /*
         * The usual precedence for message translations: an explicit
         * request from us beats LC_ALL, which beats LC_MESSAGES, which
         * beats LANG. We stop at the first of them that is set, so
         * that (for example) LC_ALL=C really does turn translation off,
         * rather than falling through to LANG.
         *
         * We also note whether any of them was set at all, because
         * that's how we tell the difference between "the user asked for
         * the C locale" and "we don't know what the user asked for".
         */
        const char *vars[] = { "PUTTY_LANG", "LC_ALL", "LC_MESSAGES", "LANG" };
        size_t vi;
        for (vi = 0; vi < lenof(vars); vi++) {
            const char *value = getenv(vars[vi]);
            if (value && *value) {
                locale_env_was_set = true;
                lng_add_locale(&cs, value);
                break;
            }
        }
    }

#ifdef __APPLE__
    if (!cs.ncand && !locale_env_was_set)
        lng_add_macos_locale(&cs);
#endif

    exec_path = my_executable_path();
    if (!exec_path) {
        lng_free_candidates(&cs);
        return;
    }

    {
        const char *slash = strrchr(exec_path, '/');
        if (!slash) {
            sfree(exec_path);
            lng_free_candidates(&cs);
            return;
        }
        exec_dir = lng_dupsubstr(exec_path, slash - exec_path);
        prog_name = dupstr(slash + 1);
    }

    /*
     * The GUI binaries are sometimes named "puttyapp" or "ptermapp",
     * and the window-based key generator is "puttygen-gtk", but the
     * language files are named after the program as its users know it.
     *
     * A test program - tools/l10ntest.c, or cfgdump - has a name that
     * no user knows, and isn't a variant of anything either. For those,
     * PUTTY_PROGNAME says which language file to read.
     */
    {
        const char *want = getenv("PUTTY_PROGNAME");

        if (want && *want) {
            prog_names[nprog++] = dupstr(want);
            sfree(prog_name);
        } else {
            size_t plen = strlen(prog_name);
            prog_names[nprog++] = prog_name;
            if (plen > 3 && !strcmp(prog_name + plen - 3, "app"))
                prog_names[nprog++] = lng_dupsubstr(prog_name, plen - 3);
            else if (plen > 4 && !strcmp(prog_name + plen - 4, "-gtk"))
                prog_names[nprog++] = lng_dupsubstr(prog_name, plen - 4);
        }
    }

    /*
     * Places we look for a language file, in order of preference. Each
     * one is a directory that should contain "<lang>/<prog>.lng".
     */
    {
        char *prefixes[5];
        int nprefix = 0;
        char *langdir = getenv("PUTTY_LANGDIR");

        if (langdir && *langdir)
            prefixes[nprefix++] = dupstr(langdir);
        prefixes[nprefix++] = dupcat(exec_dir, "/lang");
        prefixes[nprefix++] = dupcat(exec_dir, "/../Resources/lang");
        prefixes[nprefix++] = dupcat(exec_dir, "/../share/putty/lang");
        if (PUTTY_DATADIR[0])
            prefixes[nprefix++] = dupcat(PUTTY_DATADIR, "/lang");

        for (i = 0; i < nprog && !lng_file_path; i++) {
            const char *prog = prog_names[i];
            char *path;

            /*
             * A language file sitting next to the program overrides
             * anything that depends on the locale.
             */
            path = dupprintf("%s/%s.lng", exec_dir, prog);
            if (file_exists(path)) {
                load_language_file(path, 0);
                sfree(path);
                break;
            }
            sfree(path);

            for (j = 0; j < cs.ncand && !lng_file_path; j++) {
                for (k = 0; k < nprefix && !lng_file_path; k++) {
                    path = dupprintf("%s/%s/%s.lng", prefixes[k],
                                     cs.cand[j], prog);
                    if (file_exists(path)) {
                        load_language_file(path, 0);
                        sfree(path);
                        break;
                    }
                    sfree(path);
                }
            }
        }

        for (k = 0; k < nprefix; k++)
            sfree(prefixes[k]);
    }

    for (i = 1; i < nprog; i++)
        sfree(prog_names[i]);
    sfree(prog_name);
    sfree(exec_dir);
    sfree(exec_path);
    lng_free_candidates(&cs);
}

/*
 * Copy a UTF-8 string into a fixed-size buffer, without splitting a
 * multi-byte character in half.
 */
static void lng_copy_truncated(char *dest, size_t size, const char *src)
{
    size_t i = 0;

    if (size == 0)
        return;

    while (i + 1 < size && src[i]) {
        size_t charlen;
        unsigned char c = (unsigned char)src[i];

        if (c < 0x80)
            charlen = 1;
        else if (c < 0xC0)
            charlen = 1;                 /* continuation byte: odd, but cope */
        else if (c < 0xE0)
            charlen = 2;
        else if (c < 0xF0)
            charlen = 3;
        else
            charlen = 4;

        if (i + charlen >= size)
            break;
        memcpy(dest + i, src + i, charlen);
        i += charlen;
    }
    dest[i] = '\0';
}

const char *l10n_translate_s(const char *str, char *buf, size_t len)
{
    const char *translated;

    l10n_init();
    if (!lng_file_path)
        return str;

    translated = lng_find(str);
    if (!translated)
        return str;

    /*
     * Some of the translations have a mnemonic-key marker in them, in
     * the form "(&C)", because that's how Windows says which key
     * combination selects the item. Nothing here can use one, so it
     * doesn't belong in what we show.
     */
    {
        char *shown = lng_strip_bracketed_mnemonics(translated);
        lng_copy_truncated(buf, len, shown);
        sfree(shown);
    }
    return buf;
}

char *l10n_dupstr(const char *str)
{
    char buf[512];
    return dupstr(l10n_translate_s(str, buf, sizeof buf));
}

/*
 * Look up the label to put on a menu item.
 *
 * Menu labels differ from other strings in two ways. Their entries in
 * the language files carry Windows' '&' mnemonic markers, which the
 * normalisation done when the file is loaded takes care of. And their
 * wording isn't always the same as ours, because the language files
 * were written for the Windows program; where the two do differ, the
 * correspondence is recorded here.
 */
static const struct {
    const char *ours, *theirs;
} lng_menu_name_translations[] = {
    { "Copy All", "C&opy All to Clipboard" },
    { "Special Commands", "S&pecial Command" },
};

char *l10n_menu_label(const char *str)
{
    const char *translated;
    unsigned i;

    l10n_init();

    translated = lng_find(str);
    if (!translated) {
        for (i = 0; i < lenof(lng_menu_name_translations); i++) {
            if (!strcmp(lng_menu_name_translations[i].ours, str)) {
                translated = lng_find(
                    lng_menu_name_translations[i].theirs);
                break;
            }
        }
    }
    if (!translated)
        return dupstr(str);

    /*
     * And the translation may have markers in it of its own, such as
     * "(&C)", which in Windows says which key the Alt combination
     * uses. There's no such thing here, so we take them out.
     */
    return lng_mnemonic_free(translated);
}

/*
 * The language files may contain translations of *format* strings,
 * with the placeholders in them, so the functions that build a string
 * out of a format translate the format first, just as the Windows
 * front end does.
 */
char *l10n_dupvprintf(const char *format, va_list ap)
{
    char translated[512];

    return dupvprintf(l10n_translate_s(format, translated, sizeof translated),
                      ap);
}

char *l10n_dupprintf(const char *format, ...)
{
    char *ret;
    va_list ap;

    va_start(ap, format);
    ret = l10n_dupvprintf(format, ap);
    va_end(ap);
    return ret;
}

int get_l10n_setting(const char *keyname, char *buf, int size)
{
    const char *value;

    l10n_init();
    if (!lng_file_path) {
        if (size > 0)
            buf[0] = '\0';
        return 0;
    }

    value = lng_find(keyname);
    if (!value) {
        if (size > 0)
            buf[0] = '\0';
        return 0;
    }
    lng_copy_truncated(buf, size, value);
    return 1;
}

/*
 * The Windows front end can tell you the untranslated name of a
 * codepage whose displayed name has been translated, which it uses to
 * put the real name in a text box. We don't translate codepage names
 * that way, so there's never an original to recover.
 */
const char *l10n_get_codepage_orig_name(const char *cp_name)
{
    return NULL;
}

/*
 * Check what the Japanese language file does with a given string.
 *
 * With no arguments it runs a fixed set of checks; with "-", it reads
 * strings one per line from stdin and prints the translation of each.
 *
 * Build it with -DWITH_L10NTEST=ON. The language file that gets read
 * is the one named after the program, so say which one to use:
 *
 *   cmake -S . -B build -DWITH_L10NTEST=ON
 *   PUTTY_PROGNAME=putty PUTTY_LANGDIR=windows/lang build/l10ntest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "putty.h"

/* Provided by the console library in a real program. */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void show(const char *s)
{
    char *t = l10n_dupstr(s);
    printf("%s  %s%s\n", !strcmp(s, t) ? "  =" : "TR", s, t ? t : "");
    sfree(t);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-")) {
        char line[512];
        while (fgets(line, sizeof line, stdin)) {
            line[strcspn(line, "\n")] = '\0';
            if (*line)
                show(line);
        }
    } else {
        char buf[512];
        show("Cancel");
        show("Saved Sessions");
        show("Protocol:");
        show("No Such String");
        printf("dupprintf: %s\n", l10n_dupprintf("%s Configuration", "PuTTY"));
        show("Choose the Meta key:");
        show("Command key acts as Meta");
        if (get_l10n_setting("_LINECODEPAGE_", buf, sizeof buf))
            printf("_LINECODEPAGE_ = %s\n", buf);
    }
    return 0;
}

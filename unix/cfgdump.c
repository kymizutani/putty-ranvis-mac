/*
 * Build the real configuration dialog and print what it would show:
 * tree items, tab labels, control labels, and the font family names as
 * Pango hands them to us. Run it with PUTTY_LANGDIR set to the language
 * directory to see the translated forms.
 */
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "putty.h"

static void emit(const char *what, const char *s)
{
    if (!s)
        return;
    printf("%-10s |%s|\n", what, s);
}

/*
 * We aren't a real front end, so the things window.c expects from one -
 * opening another session, the X resource database, the window a dialog
 * should belong to - have nowhere to go.
 */
void window_setup_error(const char *errmsg) { (void)errmsg; }
const bool buildinfo_gtk_relevant = true;
void launch_duplicate_session(Conf *conf) { (void)conf; }
void launch_new_session(void) {}
void launch_saved_session(const char *str) { (void)str; }
void session_window_closed(void) {}
GtkWidget *make_gtk_toplevel_window(GtkFrontend *inst) { (void)inst; return NULL; }
char *x_get_default(const char *key) { (void)key; return NULL; }

extern GtkWidget *create_config_box(const char *title, Conf *conf,
                                    bool midsession, int protcfginfo,
                                    post_dialog_fn_t after, void *afterctx);

static void walk(GtkWidget *w, int depth)
{
    GList *kids, *l;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (GTK_IS_NOTEBOOK(w)) {
        int i, n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(w));
        for (i = 0; i < n; i++) {
            GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w), i);
            emit("TAB", gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(w), page));
            if (page)
                walk(page, depth + 1);
        }
        return;
    }
    if (GTK_IS_TREE_VIEW(w)) {
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
        if (model && gtk_tree_model_get_iter_first(model, &iter)) {
            int ncols = gtk_tree_model_get_n_columns(model);
            do {
                int c;
                for (c = 0; c < ncols; c++) {
                    if (gtk_tree_model_get_column_type(model, c) != G_TYPE_STRING)
                        continue;
                    char *s = NULL;
                    gtk_tree_model_get(model, &iter, c, &s, -1);
                    if (s) { emit("TREE", s); g_free(s); }
                }
            } while (gtk_tree_model_iter_next(model, &iter));
        }
    }
    if (GTK_IS_LABEL(w))
        emit("LABEL", gtk_label_get_text(GTK_LABEL(w)));
    if (GTK_IS_BUTTON(w) && GTK_IS_LABEL(gtk_bin_get_child(GTK_BIN(w))))
        emit("BUTTON", gtk_label_get_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w)))));
    if (GTK_IS_ENTRY(w))
        emit("ENTRY", gtk_entry_get_text(GTK_ENTRY(w)));

    kids = gtk_container_get_children(GTK_CONTAINER(w));
    for (l = kids; l; l = l->next)
        if (GTK_IS_WIDGET(l->data))
            walk(GTK_WIDGET(l->data), depth + 1);
    g_list_free(kids);
}

int main(int argc, char **argv)
{
    Conf *conf;
    GtkWidget *dialog;
    PangoFontMap *map;
    PangoFontFamily **families;
    int i, nfamilies, shown = 0;

    gtk_init(&argc, &argv);
    conf = conf_new();
    do_defaults(NULL, conf);
    dialog = create_config_box("PuTTY Configuration", conf, false, 0, NULL, NULL);
    if (!dialog)
        return 1;
    walk(dialog, 0);

    printf("=== font family names from Pango (non-ASCII ones, raw bytes):\n");
    map = pango_cairo_font_map_get_default();
    pango_font_map_list_families(map, &families, &nfamilies);
    for (i = 0; i < nfamilies; i++) {
        const char *name = pango_font_family_get_name(families[i]);
        {
            const unsigned char *p;
            bool nonascii = false;
            for (p = (const unsigned char *)name; *p; p++)
                if (*p >= 0x80)
                    nonascii = true;
            if (nonascii && shown++ < 12)
                printf("  [%s]\n", name);
        }
    }
    g_free(families);
    printf("=== total families: %d, non-ASCII names shown: %d\n",
           nfamilies, shown);

    /*
     * The terminal's own menu, which isn't part of the configuration
     * dialog and so doesn't appear above.
     */
    printf("=== terminal context menu, as window.c will label it:\n");
    {
        static const char *const menu_items[] = {
            "New Session...", "Restart Session", "Duplicate Session",
            "Saved Sessions", "Change Settings...", "Event Log",
            "Special Commands", "Clear Scrollback", "Reset Terminal",
            "Copy to " CLIPNAME_EXPLICIT_OBJECT,
            "Paste from " CLIPNAME_EXPLICIT_OBJECT, "Copy All",
        };
        char *about = l10n_dupprintf("About %s", "PuTTY");
        size_t j;
        for (j = 0; j < lenof(menu_items); j++) {
            char *label = l10n_menu_label(menu_items[j]);
            printf("    |%-34s| -> |%s|\n", menu_items[j], label);
            sfree(label);
        }
        printf("    |%-34s| -> |%s|\n", "About %s", about);
        sfree(about);
    }

    printf("=== dialog buttons and titles:\n");
    {
        static const char *const other_strings[] = {
            "Browse...", "Change...", "Select a font",
        };
        size_t j;
        for (j = 0; j < lenof(other_strings); j++)
            printf("    |%-34s| -> |%s|\n", other_strings[j],
                   l10n_dupstr(other_strings[j]));
    }
    return 0;
}

/*
 * unix/gtkgen.c - GTK front end for the PuTTY key generator.
 *
 * The command-line form of the same program is cmdgen.c, and the
 * Windows window-based form is windows/puttygen.c. This is the Unix
 * equivalent of the latter, and where a string already exists in that
 * file we use exactly the same English wording, so that the language
 * files in windows/lang - which contain puttygen.lng, translated for
 * those very strings - translate this program too.
 *
 * Things this front end doesn't do (yet): SSH-1 keys, OpenSSH
 * certificates, and the PPK save-parameter dialog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <gtk/gtk.h>

#include "putty.h"
#include "misc.h"
#include "ssh.h"
#include "sshkeygen.h"

#define KEYGEN_APP_ID "org.tartarus.projects.putty.macputtygen"

/*
 * The key types we can generate, and how the user chooses their size.
 */
enum genkeytype {
    KEYGEN_EDDSA,             /* Ed25519 or Ed448, chosen by curve */
    KEYGEN_ECDSA,             /* one of the NIST curves */
    KEYGEN_RSA,               /* any size */
    KEYGEN_DSA,               /* any size */
    KEYGEN_RSA1               /* an SSH-1 protocol RSA key */
};

struct keytype_option {
    const char *name;         /* English wording, for translation */
    enum genkeytype type;
    int bits;                 /* default size, or the only one */
    bool curves;              /* offer a choice of curve */
    bool size_entry;          /* offer a size entry */
};

static const struct keytype_option keytype_options[] = {
    { "SSH-2 ED25519 key", KEYGEN_EDDSA, 255, false, false },
    { "SSH-2 EdDSA key",   KEYGEN_EDDSA, 255, true,  false },
    { "SSH-2 ECDSA key",   KEYGEN_ECDSA, 256, true,  false },
    { "SSH-2 RSA key",     KEYGEN_RSA,   2048, false, true },
    { "SSH-2 DSA key",     KEYGEN_DSA,   2048, false, true },
    { "SSH-&1 key (RSA)",    KEYGEN_RSA1,  1024, false, true },
};
/*
 * The prime-generation policies you can ask for when making a key that
 * needs primes, in the order the selection offers them.
 */
static const struct {
    const char *name;                     /* English wording */
    const PrimeGenerationPolicy *policy;
} primegen_options[] = {
    { "Use probable primes (fast)", &primegen_probabilistic },
    { "Use proven primes (slower)", &primegen_provable_fast },
    { "Use proven primes with even distribution (slowest)",
      &primegen_provable_maurer_simple },
};
#define N_PRIMEGEN_OPTIONS ((int)lenof(primegen_options))
#define N_KEYTYPE_OPTIONS ((int)lenof(keytype_options))

struct keygen_state {
    GtkApplication *app;
    GtkWidget *window;

    /* Controls describing the key to generate. */
    GtkWidget *keytype_combo;
    GtkWidget *size_row, *size_spin;
    GtkWidget *curve_row, *curve_combo;
    GtkWidget *nothing_label;

    /* Things that only mean anything when a key is present. */
    GtkWidget *key_frame;
    GtkWidget *nokey_label;
    GtkWidget *keyinfo_label;
    GtkWidget *fptype_combo;
    GtkWidget *fingerprint_entry;
    GtkWidget *comment_entry;
    GtkWidget *passphrase_entry;
    GtkWidget *confirm_entry;
    GtkWidget *publicview, *public_label;
    GtkWidget *generate_button, *progressbar, *generating_label;
    GtkWidget *save_private_button, *save_public_button;

    /* Controls for how to look for primes, for the key types that
     * need them. */
    GtkWidget *prime_row, *prime_combo, *strong_check;

    /* The key we're working on, if any - exactly one of these two. */
    ssh2_userkey *ssh2key;
    RSAKey *ssh1key;
    bool key_unsaved;                 /* generated and not yet saved? */
    bool generating;
    bool updating_comment;            /* guard against recursion */
    FingerprintType fptype;

    /* Parameters used whenever we write out a PuTTY private key file. */
    ppk_save_parameters save_params;
};

/*
 * Gtk doesn't always give us a way to pass our own pointer to a
 * callback, and there's only ever one of these windows, so one
 * file-scope pointer is enough. The structure is never freed, so that a
 * background thread finishing after the window has gone can't dangle.
 */
static struct keygen_state *the_state;

/* ----------------------------------------------------------------------
 * Language support.
 *
 * The language files mark keyboard mnemonics in the Windows way, with
 * '&': the translation of "&Save private key" is "秘密鍵を保存(&S)".
 * This program doesn't use mnemonics at all, so we remove the markers,
 * including the parenthesised ones that appear in translated strings.
 */

static char *l10n_label(const char *english)
{
    char *string = l10n_dupstr(english);
    char *in, *out;

    in = out = string;
    while (*in) {
        if (in[0] == '(' && in[1] == '&' && in[2] && in[2] != '&' &&
            in[3] == ')') {
            in += 4;
            continue;
        }
        if (in[0] == '&') {
            if (in[1] == '&') {
                *out++ = '&';
                in += 2;
            } else {
                in++;
            }
            continue;
        }
        *out++ = *in++;
    }
    *out = '\0';

    return string;
}

static GtkWidget *make_label(const char *english)
{
    char *string = l10n_label(english);
    GtkWidget *widget = gtk_label_new(string);
    sfree(string);
    return widget;
}

static void set_label_text(GtkWidget *widget, const char *english)
{
    char *string = l10n_label(english);
    gtk_label_set_text(GTK_LABEL(widget), string);
    sfree(string);
}

static GtkWidget *make_button(const char *english, const char *action)
{
    char *string = l10n_label(english);
    GtkWidget *widget = gtk_button_new_with_label(string);
    sfree(string);
    if (action)
        gtk_actionable_set_action_name(GTK_ACTIONABLE(widget), action);
    return widget;
}

static void append_menu_item(GMenu *menu, const char *english,
                             const char *action)
{
    char *string = l10n_label(english);
    g_menu_append(menu, string, action);
    sfree(string);
}

/* ----------------------------------------------------------------------
 * Dialog boxes.
 *
 * Buttons are labelled with the translated name of the action the
 * dialog is asking permission to do, plus a cancel button, because
 * those are the wordings the language files contain.
 */

static void gui_message_raw(struct keygen_state *ks, GtkMessageType type,
                            const char *title, const char *text)
{
    GtkWidget *dialog, *button;
    char *title_string = l10n_label(title);

    dialog = gtk_message_dialog_new(GTK_WINDOW(ks->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    type, GTK_BUTTONS_NONE, "%s", text);
    gtk_window_set_title(GTK_WINDOW(dialog), title_string);
    button = gtk_dialog_add_button(GTK_DIALOG(dialog),
                                   l10n_label("&Close"), GTK_RESPONSE_CLOSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    gtk_widget_grab_focus(button);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    sfree(title_string);
}

static void gui_message(struct keygen_state *ks, GtkMessageType type,
                        const char *title, const char *text)
{
    char *text_string = l10n_label(text);
    gui_message_raw(ks, type, title, text_string);
    sfree(text_string);
}

/*
 * Whether there's a key loaded or generated at the moment. (Defined
 * further down, along with everything else that shows the key.)
 */
static bool key_is_present(struct keygen_state *ks);

static void gui_notice(struct keygen_state *ks, const char *text)
{
    gui_message(ks, GTK_MESSAGE_INFO, "PuTTYgen Notice", text);
}

static void gui_error(struct keygen_state *ks, const char *text)
{
    gui_message(ks, GTK_MESSAGE_ERROR, "PuTTYgen Error", text);
}

/*
 * Report a problem that has already been put into words - typically
 * because it needed a translated format string with something like a
 * file name substituted into it.
 */
static void gui_error_text(struct keygen_state *ks, const char *text)
{
    gui_message_raw(ks, GTK_MESSAGE_ERROR, "PuTTYgen Error", text);
}

static bool gui_confirm(struct keygen_state *ks, const char *title,
                        const char *text, const char *positive)
{
    GtkWidget *dialog, *button;
    char *title_string = l10n_label(title);
    char *text_string = l10n_label(text);
    char *positive_string = l10n_label(positive);
    bool confirmed;

    dialog = gtk_message_dialog_new(GTK_WINDOW(ks->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                    "%s", text_string);
    gtk_window_set_title(GTK_WINDOW(dialog), title_string);
    gtk_dialog_add_button(GTK_DIALOG(dialog), l10n_label("&Cancel"),
                          GTK_RESPONSE_CANCEL);
    button = gtk_dialog_add_button(GTK_DIALOG(dialog), positive_string,
                                   GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(button);
    confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    gtk_widget_destroy(dialog);

    sfree(title_string);
    sfree(text_string);
    sfree(positive_string);

    return confirmed;
}

/*
 * Ask the user whether it's all right to throw away the key they
 * already have, in the situations where that can happen.
 */
static bool confirm_discarding_key(struct keygen_state *ks,
                                  const char *positive)
{
    if (!key_is_present(ks) && !ks->generating)
        return true;

    return gui_confirm(ks, "PuTTYgen Warning",
                       "The key you have generated will be discarded."
                       "\nAre you sure you want to continue?", positive);
}

/*
 * Ask for the passphrase of a key file we're about to read. Returns a
 * newly allocated string, or NULL if the user cancelled.
 */
static char *ask_passphrase(struct keygen_state *ks)
{
    GtkWidget *dialog, *entry, *hbox;
    char *title, *text;
    char *result = NULL;
    int response;

    title = l10n_label("PuTTYgen: Enter Passphrase");
    text = l10n_label("Enter passphrase for key");

    dialog = gtk_dialog_new_with_buttons(
        title, GTK_WINDOW(ks->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        l10n_label("&Cancel"), GTK_RESPONSE_CANCEL,
        l10n_label("&Load private key"), GTK_RESPONSE_OK, NULL);
    sfree(title);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(
                          gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      hbox);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(text), false, false, 0);
    sfree(text);

    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), false);
    gtk_box_pack_start(GTK_BOX(hbox), entry, true, true, 0);
    g_signal_connect_swapped(G_OBJECT(entry), "activate",
                             G_CALLBACK(gtk_dialog_response),
                             G_OBJECT(dialog));

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(entry);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK)
        result = dupstr(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dialog);

    return result;
}

/* ----------------------------------------------------------------------
 * Showing the key we're holding.
 */

static bool key_is_present(struct keygen_state *ks)
{
    return ks->ssh2key || ks->ssh1key;
}

static void set_public_display(struct keygen_state *ks)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(
        GTK_TEXT_VIEW(ks->publicview));
    char *public = NULL;

    if (ks->ssh2key)
        public = ssh2_pubkey_openssh_str(ks->ssh2key);
    else if (ks->ssh1key)
        public = ssh1_pubkey_str(ks->ssh1key);

    if (public) {
        gtk_text_buffer_set_text(buffer, public, -1);
        sfree(public);
    } else {
        gtk_text_buffer_set_text(buffer, "", 0);
    }

    /*
     * An SSH-1 key is a different format of key, and its public part
     * is the one you'd put in an old-style authorized_keys file, so the
     * wording above the display is different too.
     */
    set_label_text(ks->public_label, ks->ssh1key ?
                   "&Public key for pasting into authorized_keys file:" :
                   "&Public key for pasting into OpenSSH authorized_keys "
                   "file:");
}

static void set_key_display(struct keygen_state *ks)
{
    strbuf *pubblob;
    const ssh_keyalg *alg;
    char *algdesc, *fingerprint, *comment;
    int bits;

    if (!key_is_present(ks)) {
        set_label_text(ks->nokey_label, "No key.");
        gtk_widget_show(ks->nokey_label);
        gtk_widget_set_sensitive(ks->key_frame, false);
        gtk_widget_set_sensitive(ks->generate_button, true);
        gtk_widget_set_sensitive(ks->save_private_button, false);
        gtk_widget_set_sensitive(ks->save_public_button, false);
        set_public_display(ks);
        return;
    }

    gtk_widget_hide(ks->nokey_label);
    gtk_widget_set_sensitive(ks->key_frame, true);
    gtk_widget_set_sensitive(ks->save_private_button, true);
    gtk_widget_set_sensitive(ks->save_public_button, true);

    comment = NULL;

    if (ks->ssh1key) {
        /*
         * An SSH-1 key has no algorithm object to ask about itself,
         * and there's only one kind of fingerprint for it.
         */
        gtk_label_set_text(
            GTK_LABEL(ks->keyinfo_label),
            l10n_dupprintf("%s (%d bits)", "RSA", ks->ssh1key->bits));
        gtk_widget_set_sensitive(ks->fptype_combo, false);
        fingerprint = rsa_ssh1_fingerprint(ks->ssh1key);
        comment = ks->ssh1key->comment;
    } else {
        alg = ssh_key_alg(ks->ssh2key->key);
        pubblob = strbuf_new();
        ssh_key_public_blob(ssh_key_base_key(ks->ssh2key->key),
                            BinarySink_UPCAST(pubblob));
        bits = ssh_key_public_bits(alg, ptrlen_from_strbuf(pubblob));
        strbuf_free(pubblob);

        algdesc = alg->alg_desc ? alg->alg_desc(alg) : dupstr(alg->ssh_id);
        if (bits >= 0) {
            gtk_label_set_text(
                GTK_LABEL(ks->keyinfo_label),
                l10n_dupprintf("%s (%d bits)", algdesc, bits));
        } else {
            gtk_label_set_text(GTK_LABEL(ks->keyinfo_label), algdesc);
        }
        sfree(algdesc);

        gtk_widget_set_sensitive(ks->fptype_combo, true);
        fingerprint = ssh2_fingerprint(ks->ssh2key->key, ks->fptype);
        comment = ks->ssh2key->comment;
    }

    gtk_entry_set_text(GTK_ENTRY(ks->fingerprint_entry), fingerprint);
    sfree(fingerprint);

    ks->updating_comment = true;
    gtk_entry_set_text(GTK_ENTRY(ks->comment_entry), comment ? comment : "");
    ks->updating_comment = false;

    set_public_display(ks);
}

static void discard_key(struct keygen_state *ks)
{
    if (ks->ssh2key) {
        ssh_key_free(ks->ssh2key->key);
        sfree(ks->ssh2key->comment);
        sfree(ks->ssh2key);
        ks->ssh2key = NULL;
    }
    if (ks->ssh1key) {
        freersakey(ks->ssh1key);
        sfree(ks->ssh1key);
        ks->ssh1key = NULL;
    }
    ks->key_unsaved = false;

    gtk_entry_set_text(GTK_ENTRY(ks->passphrase_entry), "");
    gtk_entry_set_text(GTK_ENTRY(ks->confirm_entry), "");

    set_key_display(ks);
}

/*
 * Copy the comment from its entry into the key, the way the other
 * front ends do before saving.
 */
static void apply_comment(struct keygen_state *ks)
{
    char *newcomment = dupstr(
        gtk_entry_get_text(GTK_ENTRY(ks->comment_entry)));

    if (ks->ssh2key) {
        sfree(ks->ssh2key->comment);
        ks->ssh2key->comment = newcomment;
    } else if (ks->ssh1key) {
        sfree(ks->ssh1key->comment);
        ks->ssh1key->comment = newcomment;
    } else {
        sfree(newcomment);
    }
}

static void comment_changed(GtkEditable *editable, gpointer vp)
{
    struct keygen_state *ks = vp;

    if (ks->updating_comment)
        return;

    apply_comment(ks);
    set_public_display(ks);
}

static void fingerprint_type_changed(GtkComboBox *combo, gpointer vp)
{
    struct keygen_state *ks = vp;
    int index = gtk_combo_box_get_active(combo);

    ks->fptype = index == 0 ? SSH_FPTYPE_SHA256 : SSH_FPTYPE_MD5;

    if (ks->ssh2key) {
        char *fingerprint = ssh2_fingerprint(ks->ssh2key->key, ks->fptype);
        gtk_entry_set_text(GTK_ENTRY(ks->fingerprint_entry), fingerprint);
        sfree(fingerprint);
    }
}

/* ----------------------------------------------------------------------
 * Key generation, which happens in a separate thread so that the
 * window stays responsive while the arithmetic is going on.
 */

static gboolean progress_idle(gpointer vp)
{
    struct keygen_state *ks = the_state;
    double *fraction = (double *)vp;

    if (!ks->generating) {
        sfree(fraction);
        return false;
    }

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ks->progressbar),
                                 *fraction);
    sfree(fraction);
    return false;
}

static ProgressPhase gtkgen_progress_add_linear(ProgressReceiver *prog,
                                                double c)
{
    ProgressPhase phase = { .n = 0 };
    return phase;
}

static ProgressPhase gtkgen_progress_add_probabilistic(ProgressReceiver *prog,
                                                        double c, double p)
{
    ProgressPhase phase = { .n = 1 };
    return phase;
}

static void gtkgen_progress_report(ProgressReceiver *prog, double progress)
{
    double *fraction = snew(double);
    *fraction = progress;
    g_idle_add(progress_idle, fraction);
}

static const ProgressReceiverVtable gtkgen_progress_vt = {
    .add_linear = gtkgen_progress_add_linear,
    .add_probabilistic = gtkgen_progress_add_probabilistic,
    .ready = null_progress_ready,
    .start_phase = null_progress_start_phase,
    .report = gtkgen_progress_report,
    .report_attempt = null_progress_report_attempt,
    .report_phase_complete = null_progress_report_phase_complete,
};

static ProgressReceiver gtkgen_progress = { .vt = &gtkgen_progress_vt };

struct gen_params {
    enum genkeytype type;
    int bits;
    bool strong_rsa;
    const PrimeGenerationPolicy *primepolicy;
};

/*
 * A key that has just been made, along with which sort of key it is.
 */
struct gen_result {
    ssh2_userkey *ssh2key;
    RSAKey *ssh1key;
};

static void free_gen_result(struct gen_result *result)
{
    if (result->ssh2key) {
        ssh_key_free(result->ssh2key->key);
        sfree(result->ssh2key->comment);
        sfree(result->ssh2key);
    }
    if (result->ssh1key) {
        freersakey(result->ssh1key);
        sfree(result->ssh1key);
    }
    sfree(result);
}

static gboolean key_generated(gpointer vp)
{
    struct keygen_state *ks = the_state;
    struct gen_result *result = (struct gen_result *)vp;

    if (!ks->generating) {
        free_gen_result(result);
        return false;
    }

    ks->generating = false;
    gtk_widget_set_sensitive(ks->generate_button, true);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ks->progressbar), 0.0);
    gtk_widget_hide(ks->progressbar);
    gtk_widget_hide(ks->generating_label);

    discard_key(ks);

    if (result->ssh2key || result->ssh1key) {
        ks->ssh2key = result->ssh2key;
        ks->ssh1key = result->ssh1key;
        result->ssh2key = NULL;
        result->ssh1key = NULL;
        ks->key_unsaved = true;
        set_key_display(ks);
    } else {
        gui_error(ks, "Key generation error");
    }

    free_gen_result(result);

    return false;
}

static gpointer generate_key_thread(gpointer vp)
{
    struct gen_params *params = (struct gen_params *)vp;
    struct gen_result *result = snew(struct gen_result);
    char commentbuf[80];
    char *entropy;
    struct tm tm;
    PrimeGenerationContext *pgc;

    /*
     * Make a default comment for the key, in the same style the other
     * front ends and the command-line version use.
     */
    tm = ltime();
    if (params->type == KEYGEN_DSA)
        strftime(commentbuf, sizeof(commentbuf), "dsa-key-%Y%m%d", &tm);
    else if (params->type == KEYGEN_ECDSA)
        strftime(commentbuf, sizeof(commentbuf), "ecdsa-key-%Y%m%d", &tm);
    else if (params->type == KEYGEN_EDDSA && params->bits == 255)
        strftime(commentbuf, sizeof(commentbuf), "ed25519-key-%Y%m%d", &tm);
    else if (params->type == KEYGEN_EDDSA)
        strftime(commentbuf, sizeof(commentbuf), "eddsa-key-%Y%m%d", &tm);
    else
        strftime(commentbuf, sizeof(commentbuf), "rsa-key-%Y%m%d", &tm);

    entropy = get_random_data(params->bits / 8, NULL);
    if (entropy) {
        /*
         * The generator can only be set up once (in main(), below), so
         * all we do here is put some fresh entropy of this key's own
         * into it.
         */
        random_reseed(make_ptrlen(entropy, params->bits / 8));
        smemclr(entropy, params->bits / 8);
        sfree(entropy);

        pgc = primegen_new_context(params->primepolicy);

        if (params->type == KEYGEN_DSA) {
            struct dsa_key *dsakey = snew(struct dsa_key);
            dsa_generate(dsakey, params->bits, pgc, &gtkgen_progress);
            result->ssh2key = snew(ssh2_userkey);
            result->ssh2key->key = &dsakey->sshk;
        } else if (params->type == KEYGEN_ECDSA) {
            struct ecdsa_key *eckey = snew(struct ecdsa_key);
            ecdsa_generate(eckey, params->bits);
            result->ssh2key = snew(ssh2_userkey);
            result->ssh2key->key = &eckey->sshk;
        } else if (params->type == KEYGEN_EDDSA) {
            struct eddsa_key *edkey = snew(struct eddsa_key);
            eddsa_generate(edkey, params->bits);
            result->ssh2key = snew(ssh2_userkey);
            result->ssh2key->key = &edkey->sshk;
        } else {
            /*
             * RSA, in both of the flavours we can make: an SSH-2 key
             * wrapped up ready for the common handling, or an old-style
             * SSH-1 key which is just the RSA key on its own.
             */
            RSAKey *rsakey = snew(RSAKey);
            rsa_generate(rsakey, params->bits, params->strong_rsa, pgc,
                         &gtkgen_progress);
            rsakey->comment = NULL;
            if (params->type == KEYGEN_RSA1) {
                result->ssh1key = rsakey;
            } else {
                result->ssh2key = snew(ssh2_userkey);
                result->ssh2key->key = &rsakey->sshk;
            }
        }

        primegen_free_context(pgc);
    }

    if (result->ssh2key)
        result->ssh2key->comment = dupstr(commentbuf);
    else if (result->ssh1key)
        result->ssh1key->comment = dupstr(commentbuf);

    sfree(params);

    g_idle_add(key_generated, result);

    return NULL;
}

/*
 * Only the key types that are made out of primes have anything to say
 * about how to find them.
 */
static bool option_needs_primes(enum genkeytype type)
{
    return type == KEYGEN_RSA || type == KEYGEN_DSA || type == KEYGEN_RSA1;
}

static void act_generate(GSimpleAction *action, GVariant *parameter,
                         gpointer vp)
{
    struct keygen_state *ks = vp;
    struct gen_params *params;
    GThread *thread;
    GError *error = NULL;
    const struct keytype_option *option;
    int index;

    if (ks->generating)
        return;

    index = gtk_combo_box_get_active(GTK_COMBO_BOX(ks->keytype_combo));
    if (index < 0 || index >= N_KEYTYPE_OPTIONS)
        return;
    option = &keytype_options[index];

    if (!confirm_discarding_key(ks, "&Generate key pair"))
        return;

    params = snew(struct gen_params);
    params->type = option->type;

    if (option_needs_primes(option->type)) {
        index = gtk_combo_box_get_active(GTK_COMBO_BOX(ks->prime_combo));
        if (index < 0 || index >= N_PRIMEGEN_OPTIONS)
            index = 0;
        params->primepolicy = primegen_options[index].policy;
        params->strong_rsa = option->type == KEYGEN_RSA &&
            gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(ks->strong_check));
    } else {
        params->primepolicy = &primegen_probabilistic;
        params->strong_rsa = false;
    }

    if (option->size_entry) {
        params->bits = gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(ks->size_spin));
    } else if (option->curves) {
        int curve_index = gtk_combo_box_get_active(
            GTK_COMBO_BOX(ks->curve_combo));
        const int *lengths;
        int n;

        if (option->type == KEYGEN_EDDSA) {
            lengths = ec_ed_curve_lengths;
            n = n_ec_ed_curve_lengths;
        } else {
            lengths = ec_nist_curve_lengths;
            n = n_ec_nist_curve_lengths;
        }
        if (curve_index < 0 || curve_index >= n) {
            sfree(params);
            return;
        }
        params->bits = lengths[curve_index];
    } else {
        params->bits = option->bits;
    }

    thread = g_thread_try_new("keygen", generate_key_thread, params, &error);
    if (!thread) {
        gui_error(ks, "Out of thread resources");
        sfree(params);
        if (error)
            g_error_free(error);
        return;
    }

    discard_key(ks);
    ks->generating = true;
    gtk_widget_set_sensitive(ks->generate_button, false);
    set_label_text(ks->generating_label,
                   "Please wait while a key is generated...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ks->progressbar), 0.0);
    gtk_widget_show(ks->progressbar);
    gtk_widget_show(ks->generating_label);
}

/* ----------------------------------------------------------------------
 * Reading a key from a file.
 */

static void load_key_file(struct keygen_state *ks, const char *path)
{
    Filename *fn = filename_from_str(path);
    LoadedFile *lf;
    BinarySource *bs;
    const char *load_error = NULL, *error = NULL;
    ssh2_userkey *key = NULL;
    char *passphrase = NULL;
    bool foreign = false;

    if (!confirm_discarding_key(ks, "&Load private key"))
        goto done;

    lf = lf_load_keyfile(fn, &load_error);
    if (!lf) {
        char *base = l10n_label("Couldn't load private key.");
        char *text = dupcat(base, "\n", load_error);
        gui_error_text(ks, text);
        sfree(text);
        sfree(base);
        goto done;
    }

    bs = BinarySource_UPCAST(lf);
    int type = key_type_s(bs);
    BinarySource_REWIND(bs);
    RSAKey *ssh1key = NULL;

    switch (type) {

      case SSH_KEYTYPE_SSH2: {
        char *comment = NULL;
        bool encrypted = ppk_encrypted_s(bs, &comment);
        sfree(comment);
        BinarySource_REWIND(bs);

        if (encrypted) {
            passphrase = ask_passphrase(ks);
            if (!passphrase)
                goto done_free;
        }

        key = ppk_load_s(bs, passphrase, &error);
        if (key == SSH2_WRONG_PASSPHRASE) {
            key = NULL;
            error = "wrong passphrase";
        }
        break;
      }

      case SSH_KEYTYPE_OPENSSH_PEM:
      case SSH_KEYTYPE_OPENSSH_NEW:
      case SSH_KEYTYPE_SSHCOM:
        foreign = true;
        key = import_ssh2_s(bs, type, NULL, &error);
        if (key == SSH2_WRONG_PASSPHRASE) {
            passphrase = ask_passphrase(ks);
            if (!passphrase)
                goto done_free;
            BinarySource_REWIND(bs);
            key = import_ssh2_s(bs, type, passphrase, &error);
            if (key == SSH2_WRONG_PASSPHRASE) {
                key = NULL;
                error = "wrong passphrase";
            }
        }
        break;

      case SSH_KEYTYPE_SSH1: {
        /*
         * An old-style SSH-1 private key file. Whether it needs a
         * passphrase at all is something we can find out first, so the
         * user isn't asked for one unnecessarily.
         */
        char *comment = NULL;
        bool encrypted = rsa1_encrypted_s(bs, &comment);
        sfree(comment);
        BinarySource_REWIND(bs);

        if (encrypted) {
            passphrase = ask_passphrase(ks);
            if (!passphrase)
                goto done_free;
        }

        RSAKey *rsakey = snew(RSAKey);
        smemclr(rsakey, sizeof(*rsakey));
        if (rsa1_load_f(fn, rsakey, passphrase, &error) == 1) {
            ssh1key = rsakey;
        } else {
            freersakey(rsakey);
            sfree(rsakey);
            error = error ? error : "broken key file";
        }
        break;
      }

      case SSH_KEYTYPE_SSH1_PUBLIC:
      case SSH_KEYTYPE_SSH2_PUBLIC_RFC4716:
      case SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH:
      default:
        error = key_type_to_str(type);
        break;
    }

  done_free:
    lf_free(lf);

    if (!key && !ssh1key) {
        char *base = l10n_label("Couldn't load private key.");
        char *detail = l10n_dupstr(error ? error : "unknown error");
        char *text = dupcat(base, "\n", detail);
        gui_error_text(ks, text);
        sfree(text);
        sfree(detail);
        sfree(base);
    } else {
        discard_key(ks);
        ks->ssh2key = key;
        ks->ssh1key = ssh1key;
        ks->key_unsaved = false;
        set_key_display(ks);

        if (foreign) {
            char *text = l10n_dupprintf(
                "Successfully imported foreign key"
                "\n(%s)."
                "\nTo use this key with PuTTY, you need to"
                "\nuse the \"Save private key\" command to"
                "\nsave it in PuTTY's own format.", path);
            gui_notice(ks, text);
            sfree(text);
        }
    }

  done:
    sfree(passphrase);
    filename_free(fn);
}

static void act_load(GSimpleAction *action, GVariant *parameter, gpointer vp)
{
    struct keygen_state *ks = vp;
    GtkWidget *chooser;
    char *path = NULL;

    if (!confirm_discarding_key(ks, "&Load private key"))
        return;

    chooser = gtk_file_chooser_dialog_new(
        l10n_label("Load private key:"), GTK_WINDOW(ks->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        l10n_label("&Cancel"), GTK_RESPONSE_CANCEL,
        l10n_label("&Load private key"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), false);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    gtk_widget_destroy(chooser);

    if (path) {
        load_key_file(ks, path);
        g_free(path);
    }
}

/* ----------------------------------------------------------------------
 * Writing keys out.
 */

static char *ask_passphrases(struct keygen_state *ks)
{
    const char *pass = gtk_entry_get_text(GTK_ENTRY(ks->passphrase_entry));
    const char *confirm = gtk_entry_get_text(GTK_ENTRY(ks->confirm_entry));

    if (strcmp(pass, confirm)) {
        gui_error(ks, "The two passphrases given do not match.");
        return NULL;
    }

    if (!*pass) {
        if (!gui_confirm(ks, "PuTTYgen Warning",
                         "Are you sure you want to save this key"
                         "\nwithout a passphrase to protect it?",
                         "&Save private key"))
            return NULL;
    }

    return dupstr(pass);
}

/*
 * Ask for a file name, offering a default extension. Returns a newly
 * allocated path, or NULL if the user cancelled.
 */
static char *ask_filename(struct keygen_state *ks, const char *title,
                          const char *accept_button, const char *default_name,
                          const char *extension)
{
    GtkWidget *chooser;
    char *path = NULL;

    chooser = gtk_file_chooser_dialog_new(
        l10n_label(title), GTK_WINDOW(ks->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        l10n_label("&Cancel"), GTK_RESPONSE_CANCEL,
        l10n_label(accept_button), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(
        GTK_FILE_CHOOSER(chooser), true);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser),
                                      default_name);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), false);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    gtk_widget_destroy(chooser);

    if (path && extension && extension[0]) {
        char *basename = g_path_get_basename(path);
        bool no_dot = !strrchr(basename, '.');
        g_free(basename);

        if (no_dot) {
            char *with_extension = dupcat(path, extension);
            g_free(path);
            path = with_extension;
        }
    }

    return path;
}

static void act_save_private(GSimpleAction *action, GVariant *parameter,
                             gpointer vp)
{
    struct keygen_state *ks = vp;
    char *path, *passphrase;
    Filename *fn;

    if (!key_is_present(ks)) {
        gui_notice(ks, "No key.");
        return;
    }

    apply_comment(ks);
    passphrase = ask_passphrases(ks);
    if (!passphrase)
        return;

    path = ask_filename(ks, "Save private key as:", "&Save private key",
                        "new_key.ppk", ".ppk");
    if (path) {
        fn = filename_from_str(path);
        bool ok;
        if (ks->ssh1key) {
            ok = rsa1_save_f(fn, ks->ssh1key,
                             *passphrase ? passphrase : NULL);
        } else {
            ok = ppk_save_f(fn, ks->ssh2key,
                            *passphrase ? passphrase : NULL,
                            &ks->save_params);
        }
        if (!ok) {
            gui_error(ks, "Unable to save key file");
        } else {
            ks->key_unsaved = false;
        }
        filename_free(fn);
        g_free(path);
    }

    sfree(passphrase);
}

static void act_save_public(GSimpleAction *action, GVariant *parameter,
                            gpointer vp)
{
    struct keygen_state *ks = vp;
    char *path, *public;
    Filename *fn;
    FILE *fp;

    if (!key_is_present(ks)) {
        gui_notice(ks, "No key.");
        return;
    }

    apply_comment(ks);

    path = ask_filename(ks, "Save public key as:", "Save p&ublic key",
                        "new_key.pub", ".pub");
    if (!path)
        return;

    fn = filename_from_str(path);
    fp = f_open(fn, "w", false);
    if (!fp) {
        gui_error(ks, "Unable to open key file for writing");
    } else {
        public = ks->ssh1key ? ssh1_pubkey_str(ks->ssh1key) :
            ssh2_pubkey_openssh_str(ks->ssh2key);
        fprintf(fp, "%s\n", public);
        sfree(public);
        fclose(fp);
    }
    filename_free(fn);
    g_free(path);
}

static void export_key(struct keygen_state *ks, int type)
{
    char *path, *passphrase;
    Filename *fn;

    if (!key_is_present(ks)) {
        gui_notice(ks, "No key.");
        return;
    }

    if (ks->ssh1key) {
        /*
         * The two formats of key file are quite different things, so
         * there's no way to turn an SSH-1 key into an SSH-2 one.
         */
        char *text = l10n_dupprintf(
            "Cannot export an SSH%d key in an SSH%d format", 1, 2);
        gui_error_text(ks, text);
        sfree(text);
        return;
    }

    apply_comment(ks);
    passphrase = ask_passphrases(ks);
    if (!passphrase)
        return;

    path = ask_filename(ks, "Save private key as:", "&Save private key",
                        "new_key", NULL);
    if (path) {
        fn = filename_from_str(path);
        if (!export_ssh2(fn, type, ks->ssh2key,
                         *passphrase ? passphrase : NULL))
            gui_error(ks, "Unable to save key file");
        filename_free(fn);
        g_free(path);
    }

    sfree(passphrase);
}

static void act_export_openssh(GSimpleAction *action, GVariant *parameter,
                               gpointer vp)
{
    export_key(vp, SSH_KEYTYPE_OPENSSH_AUTO);
}

static void act_export_openssh_new(GSimpleAction *action,
                                   GVariant *parameter, gpointer vp)
{
    export_key(vp, SSH_KEYTYPE_OPENSSH_NEW);
}

static void act_export_sshcom(GSimpleAction *action, GVariant *parameter,
                              gpointer vp)
{
    export_key(vp, SSH_KEYTYPE_SSHCOM);
}

/* ----------------------------------------------------------------------
 * About box.
 */

/* ----------------------------------------------------------------------
 * Choosing the parameters that go into a PuTTY-format private key file.
 */

static const struct {
    const char *name;
    Argon2Flavour flavour;
} argon2_flavours[] = {
    { "Argon2id", Argon2id },
    { "Argon2i",  Argon2i  },
    { "Argon2d",  Argon2d  },
};
#define N_ARGON2_FLAVOURS ((int)lenof(argon2_flavours))

struct params_controls {
    GtkWidget *version_combo, *flavour_combo;
    GtkWidget *mem_spin, *time_spin, *parallelism_spin;
};

static void ppk_version_changed(GtkComboBox *combo, gpointer cp)
{
    struct params_controls *c = (struct params_controls *)cp;
    gboolean v3 = gtk_combo_box_get_active(combo) == 1;

    /*
     * Version 2 of the file format uses a different kind of key
     * derivation, so none of the Argon2 settings mean anything for it.
     */
    gtk_widget_set_sensitive(c->flavour_combo, v3);
    gtk_widget_set_sensitive(c->mem_spin, v3);
    gtk_widget_set_sensitive(c->time_spin, v3);
    gtk_widget_set_sensitive(c->parallelism_spin, v3);
}

static GtkWidget *make_spin(double value, double min, double max)
{
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    return spin;
}

static void act_params(GSimpleAction *action, GVariant *parameter,
                       gpointer vp)
{
    struct keygen_state *ks = vp;
    struct params_controls control;
    ppk_save_parameters params = ks->save_params;
    GtkWidget *dialog, *content, *grid, *label;
    char *error;
    int response, row, i;

    dialog = gtk_dialog_new_with_buttons(
        l10n_label("PuTTYgen: Private Key File Parameters"),
        GTK_WINDOW(ks->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        l10n_label("&Cancel"), GTK_RESPONSE_CANCEL,
        l10n_label("&Close"), GTK_RESPONSE_ACCEPT,
        NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_box_pack_start(GTK_BOX(content), grid, false, false, 0);

    row = 0;
    label = make_label("PPK file version:");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    control.version_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(control.version_combo),
                                   "2");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(control.version_combo),
                                   "3");
    gtk_combo_box_set_active(GTK_COMBO_BOX(control.version_combo),
                             params.fmt_version == 3 ? 1 : 0);
    g_signal_connect(G_OBJECT(control.version_combo), "changed",
                     G_CALLBACK(ppk_version_changed), &control);
    gtk_grid_attach(GTK_GRID(grid), control.version_combo, 1, row, 1, 1);
    row++;

    label = make_label("Key derivation function:");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    control.flavour_combo = gtk_combo_box_text_new();
    i = 0;
    for (int j = 0; j < N_ARGON2_FLAVOURS; j++) {
        gtk_combo_box_text_append_text(
            GTK_COMBO_BOX_TEXT(control.flavour_combo),
            argon2_flavours[j].name);
        if (argon2_flavours[j].flavour == params.argon2_flavour)
            i = j;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(control.flavour_combo), i);
    gtk_grid_attach(GTK_GRID(grid), control.flavour_combo, 1, row, 1, 1);
    row++;

    label = make_label("Memory to use for passphrase hash:");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    control.mem_spin = make_spin(params.argon2_mem, 1024, 1 << 20);
    gtk_grid_attach(GTK_GRID(grid), control.mem_spin, 1, row, 1, 1);
    row++;

    /*
     * The number of passes is decided automatically so that the key
     * derivation takes this long, which is what the format's own
     * default does too.
     */
    label = make_label("Time to use for passphrase hash:");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    control.time_spin = make_spin(params.argon2_passes_auto ?
                                  params.argon2_milliseconds : 500,
                                  1, 60000);
    gtk_grid_attach(GTK_GRID(grid), control.time_spin, 1, row, 1, 1);
    row++;

    label = make_label("Parallelism for passphrase hash:");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    control.parallelism_spin = make_spin(params.argon2_parallelism, 1, 256);
    gtk_grid_attach(GTK_GRID(grid), control.parallelism_spin, 1, row, 1, 1);
    row++;

    ppk_version_changed(NULL, &control);
    gtk_widget_show_all(dialog);

    while (true) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response != GTK_RESPONSE_ACCEPT)
            break;

        params.fmt_version =
            gtk_combo_box_get_active(GTK_COMBO_BOX(control.version_combo)) == 1
            ? 3 : 2;
        i = gtk_combo_box_get_active(GTK_COMBO_BOX(control.flavour_combo));
        if (i < 0 || i >= N_ARGON2_FLAVOURS)
            i = 0;
        params.argon2_flavour = argon2_flavours[i].flavour;
        params.argon2_mem = gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(control.mem_spin));
        params.argon2_passes_auto = true;
        params.argon2_milliseconds = gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(control.time_spin));
        params.argon2_parallelism = gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(control.parallelism_spin));

        error = ppk_params_bad(&params, false, 0);
        if (!error) {
            ks->save_params = params;
            break;
        }

        {
            char *base = l10n_label("Save parameters invalid");
            char *detail = l10n_dupstr(error);
            char *text = dupcat(base, "\n", detail);
            gui_error_text(ks, text);
            sfree(text);
            sfree(detail);
            sfree(base);
        }
    }

    gtk_widget_destroy(dialog);
}

static void act_about(GSimpleAction *action, GVariant *parameter, gpointer vp)
{
    struct keygen_state *ks = vp;
    GtkWidget *dialog;
    char *name = l10n_label("PuTTY Key Generator");
    char *build = buildinfo("\n");

    dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), name);
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), ver);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), build);
    gtk_about_dialog_set_website(
        GTK_ABOUT_DIALOG(dialog),
        "https://www.chiark.greenend.org.uk/~sgtatham/putty/");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(ks->window));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    sfree(name);
    sfree(build);
}

/* ----------------------------------------------------------------------
 * Window construction.
 */

static void keytype_changed(GtkComboBox *combo, gpointer vp)
{
    struct keygen_state *ks = vp;
    int index = gtk_combo_box_get_active(combo);
    const struct keytype_option *option;
    bool eddsa;
    const int *lengths;
    int n, i;

    if (index < 0 || index >= N_KEYTYPE_OPTIONS)
        return;
    option = &keytype_options[index];

    gtk_widget_hide(ks->size_row);
    gtk_widget_hide(ks->curve_row);
    gtk_widget_hide(ks->nothing_label);
    gtk_widget_hide(ks->prime_row);

    if (option_needs_primes(option->type)) {
        /*
         * The choice of 'strong' primes only means anything for RSA,
         * but the choice of how to prove a prime is good applies to all
         * three of these key types.
         */
        gtk_widget_set_sensitive(ks->strong_check,
                                 option->type == KEYGEN_RSA);
        gtk_widget_show(ks->prime_row);
    }

    if (option->size_entry) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ks->size_spin),
                                  option->bits);
        gtk_widget_show(ks->size_row);
    } else if (option->curves) {
        eddsa = option->type == KEYGEN_EDDSA;
        lengths = eddsa ? ec_ed_curve_lengths : ec_nist_curve_lengths;
        n = eddsa ? n_ec_ed_curve_lengths : n_ec_nist_curve_lengths;

        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ks->curve_combo));
        for (i = 0; i < n; i++) {
            const struct ec_curve *curve;
            const ssh_keyalg *alg;
            char *desc, *label;

            if (eddsa)
                ec_ed_alg_and_curve_by_bits(lengths[i], &curve, &alg);
            else
                ec_nist_alg_and_curve_by_bits(lengths[i], &curve, &alg);

            desc = dupprintf("%s (%d bits)", curve->textname, lengths[i]);
            label = l10n_label(desc);
            sfree(desc);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ks->curve_combo),
                                           label);
            sfree(label);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(ks->curve_combo), 0);
        gtk_widget_show(ks->curve_row);
    } else {
        set_label_text(ks->nothing_label,
                       "(nothing to configure for this key type)");
        gtk_widget_show(ks->nothing_label);
    }
}

static void setup_menubar(GtkApplication *app)
{
    GMenu *menubar, *menu, *section;

    menubar = g_menu_new();

    menu = g_menu_new();
    section = g_menu_new();
    append_menu_item(section, "&Load private key", "win.load");
    append_menu_item(section, "Save p&ublic key", "win.save-public");
    append_menu_item(section, "&Save private key", "win.save-private");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    section = g_menu_new();
    append_menu_item(section, "E&xit", "app.quit");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append_submenu(menubar, l10n_label("&File"), G_MENU_MODEL(menu));

    menu = g_menu_new();
    section = g_menu_new();
    append_menu_item(section, "&Generate key pair", "win.generate");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append_submenu(menubar, l10n_label("&Key"), G_MENU_MODEL(menu));

    menu = g_menu_new();
    section = g_menu_new();
    append_menu_item(section, "&Import key", "win.load");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    section = g_menu_new();
    append_menu_item(section, "Export &OpenSSH key", "win.export-openssh");
    append_menu_item(section, "Export &OpenSSH key (force new file format)",
                     "win.export-openssh-new");
    append_menu_item(section, "Export &ssh.com key", "win.export-sshcom");
    append_menu_item(section, "Parameters for saving key files...",
                     "win.params");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append_submenu(menubar, l10n_label("Con&versions"),
                          G_MENU_MODEL(menu));

    menu = g_menu_new();
    section = g_menu_new();
    append_menu_item(section, "About PuTTYgen", "win.about");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append_submenu(menubar, l10n_label("&Help"), G_MENU_MODEL(menu));

    gtk_application_set_menubar(GTK_APPLICATION(app), G_MENU_MODEL(menubar));
    g_object_unref(menubar);
}

static void act_quit(GSimpleAction *action, GVariant *parameter, gpointer vp)
{
    struct keygen_state *ks = vp;

    g_application_quit(G_APPLICATION(ks->app));
}

static const GActionEntry win_actions[] = {
    { "generate",            act_generate,          NULL, NULL, NULL },
    { "load",                act_load,              NULL, NULL, NULL },
    { "save-private",        act_save_private,      NULL, NULL, NULL },
    { "save-public",         act_save_public,       NULL, NULL, NULL },
    { "export-openssh",      act_export_openssh,    NULL, NULL, NULL },
    { "export-openssh-new",  act_export_openssh_new,NULL, NULL, NULL },
    { "export-sshcom",       act_export_sshcom,     NULL, NULL, NULL },
    { "params",              act_params,            NULL, NULL, NULL },
    { "about",               act_about,             NULL, NULL, NULL },
};
#define N_WIN_ACTIONS ((int)lenof(win_actions))

static void set_accels(GtkApplication *app)
{
    static const char *const generate[] = { "<Primary>g", NULL };
    static const char *const load[] = { "<Primary>o", NULL };
    static const char *const save_private[] = { "<Primary>s", NULL };
    static const char *const save_public[] = { "<Primary><Shift>s", NULL };
    static const char *const quit[] = { "<Primary>q", NULL };

    gtk_application_set_accels_for_action(app, "win.generate", generate);
    gtk_application_set_accels_for_action(app, "win.load", load);
    gtk_application_set_accels_for_action(app, "win.save-private",
                                          save_private);
    gtk_application_set_accels_for_action(app, "win.save-public",
                                          save_public);
    gtk_application_set_accels_for_action(app, "app.quit", quit);
}

static gboolean window_close_request(GtkWidget *widget, GdkEvent *event,
                                     gpointer vp)
{
    struct keygen_state *ks = vp;

    if (ks->key_unsaved) {
        if (!gui_confirm(ks, "PuTTYgen Warning",
                         "The key you have generated will be discarded."
                         "\nAre you sure you want to exit without saving?",
                         "E&xit"))
            return true;
    }

    return false;
}

static void build_window(GtkApplication *app, struct keygen_state *ks)
{
    GtkWidget *window, *hbox, *vbox, *grid, *frame, *label, *scroller,
        *textview, *combo;
    int row;

    window = gtk_application_window_new(app);
    ks->window = window;
    /*
     * "delete-event", not "close-request": this GTK doesn't have the
     * latter at all (it's not in the library's signal list), and
     * connecting to a signal that doesn't exist does nothing but print
     * a critical warning. unix/askpass.c uses delete-event too.
     */
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(window_close_request), ks);

    setup_menubar(app);
    set_accels(app);
    g_action_map_add_action_entries(G_ACTION_MAP(window), win_actions,
                                    N_WIN_ACTIONS, ks);

    /*
     * GtkApplication on this platform doesn't provide an "app.quit"
     * action itself (we checked), so the Quit item would sit inactive
     * unless we supply one. The application menu's Quit is bound to the
     * same name, so this makes that work too.
     */
    {
        static const GActionEntry app_actions[] = {
            { "quit", act_quit },
        };
        g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions,
                                        lenof(app_actions), ks);
    }

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    /* Left-hand column: everything about making or taking a key. */

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, false, false, 0);

    frame = gtk_frame_new(NULL);
    label = make_label("Generate a public/private key pair");
    gtk_frame_set_label_widget(GTK_FRAME(frame), label);
    gtk_box_pack_start(GTK_BOX(vbox), frame, false, false, 0);

    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    row = 0;
    gtk_grid_attach(GTK_GRID(grid), make_label("Type of key to generate:"),
                    0, row, 2, 1);
    row++;

    ks->keytype_combo = gtk_combo_box_text_new();
    for (int i = 0; i < N_KEYTYPE_OPTIONS; i++) {
        char *string = l10n_label(keytype_options[i].name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ks->keytype_combo),
                                       string);
        sfree(string);
    }
    g_signal_connect(G_OBJECT(ks->keytype_combo), "changed",
                     G_CALLBACK(keytype_changed), ks);
    gtk_grid_attach(GTK_GRID(grid), ks->keytype_combo, 0, row, 2, 1);
    row++;

    ks->size_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = make_label("Number of &bits in a generated key:");
    gtk_box_pack_start(GTK_BOX(ks->size_row), label, false, false, 0);
    ks->size_spin = gtk_spin_button_new_with_range(256, 16384, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(ks->size_spin), 0);
    gtk_box_pack_start(GTK_BOX(ks->size_row), ks->size_spin, false, false, 0);
    gtk_widget_set_no_show_all(ks->size_row, true);
    gtk_grid_attach(GTK_GRID(grid), ks->size_row, 0, row, 2, 1);
    row++;

    ks->curve_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = make_label("Cur&ve to use for generating this key:");
    gtk_box_pack_start(GTK_BOX(ks->curve_row), label, false, false, 0);
    ks->curve_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(ks->curve_row), ks->curve_combo, false,
                       false, 0);
    gtk_widget_set_no_show_all(ks->curve_row, true);
    gtk_grid_attach(GTK_GRID(grid), ks->curve_row, 0, row, 2, 1);
    row++;

    /*
     * How to look for primes, for the key types that are made out of
     * them. Each option in the list is a complete sentence in its own
     * right, so this row doesn't need a label of its own.
     */
    ks->prime_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    ks->prime_combo = gtk_combo_box_text_new();
    for (int i = 0; i < N_PRIMEGEN_OPTIONS; i++) {
        char *string = l10n_label(primegen_options[i].name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ks->prime_combo),
                                       string);
        sfree(string);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ks->prime_combo), 0);
    gtk_box_pack_start(GTK_BOX(ks->prime_row), ks->prime_combo, false,
                       false, 0);
    ks->strong_check = gtk_check_button_new_with_label(
        l10n_label("Use \"strong\" primes as RSA key factors"));
    gtk_box_pack_start(GTK_BOX(ks->prime_row), ks->strong_check, false,
                       false, 0);
    gtk_widget_set_no_show_all(ks->prime_row, true);
    gtk_grid_attach(GTK_GRID(grid), ks->prime_row, 0, row, 2, 1);
    row++;

    ks->nothing_label = gtk_label_new("");
    gtk_widget_set_no_show_all(ks->nothing_label, true);
    gtk_grid_attach(GTK_GRID(grid), ks->nothing_label, 0, row, 2, 1);
    row++;

    ks->generate_button = make_button("&Generate", "win.generate");
    gtk_grid_attach(GTK_GRID(grid), ks->generate_button, 0, row, 2, 1);
    row++;

    ks->progressbar = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(ks->progressbar, true);
    gtk_grid_attach(GTK_GRID(grid), ks->progressbar, 0, row, 2, 1);
    row++;

    ks->generating_label = gtk_label_new("");
    gtk_widget_set_no_show_all(ks->generating_label, true);
    gtk_grid_attach(GTK_GRID(grid), ks->generating_label, 0, row, 2, 1);
    row++;

    frame = gtk_frame_new(NULL);
    label = make_label("Load an existing private key file");
    gtk_frame_set_label_widget(GTK_FRAME(frame), label);
    gtk_box_pack_start(GTK_BOX(vbox), frame, false, false, 0);
    ks->save_public_button = NULL;      /* silence gcc, set below */
    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
    gtk_container_add(GTK_CONTAINER(frame), grid);
    gtk_grid_attach(GTK_GRID(grid), make_button("&Load", "win.load"),
                    0, 0, 1, 1);

    frame = gtk_frame_new(NULL);
    label = make_label("Save the generated key");
    gtk_frame_set_label_widget(GTK_FRAME(frame), label);
    gtk_box_pack_start(GTK_BOX(vbox), frame, false, false, 0);
    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_container_add(GTK_CONTAINER(frame), grid);
    ks->save_private_button = make_button("&Save private key",
                                          "win.save-private");
    gtk_grid_attach(GTK_GRID(grid), ks->save_private_button, 0, 0, 1, 1);
    ks->save_public_button = make_button("Save p&ublic key",
                                         "win.save-public");
    gtk_grid_attach(GTK_GRID(grid), ks->save_public_button, 1, 0, 1, 1);

    /* Right-hand column: the key itself. */

    ks->key_frame = gtk_frame_new(NULL);
    label = make_label("Key");
    gtk_frame_set_label_widget(GTK_FRAME(ks->key_frame), label);
    gtk_box_pack_start(GTK_BOX(hbox), ks->key_frame, true, true, 0);

    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_container_add(GTK_CONTAINER(ks->key_frame), grid);

    ks->nokey_label = make_label("No key.");
    gtk_grid_attach(GTK_GRID(grid), ks->nokey_label, 0, 0, 2, 1);

    ks->keyinfo_label = gtk_label_new("");
    gtk_grid_attach(GTK_GRID(grid), ks->keyinfo_label, 0, 0, 2, 1);

    row = 1;
    gtk_grid_attach(GTK_GRID(grid), make_label("Key f&ingerprint:"),
                    0, row, 1, 1);
    ks->fingerprint_entry = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(ks->fingerprint_entry), false);
    gtk_entry_set_width_chars(GTK_ENTRY(ks->fingerprint_entry), 40);
    gtk_grid_attach(GTK_GRID(grid), ks->fingerprint_entry, 1, row, 1, 1);
    row++;

    ks->fptype_combo = gtk_combo_box_text_new();
    combo = ks->fptype_combo;
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                   l10n_label("Show fingerprint as SHA256"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                   l10n_label("Show fingerprint as MD5"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    g_signal_connect(G_OBJECT(combo), "changed",
                     G_CALLBACK(fingerprint_type_changed), ks);
    gtk_grid_attach(GTK_GRID(grid), combo, 1, row, 1, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_label("Key &comment:"),
                    0, row, 1, 1);
    ks->comment_entry = gtk_entry_new();
    g_signal_connect(G_OBJECT(ks->comment_entry), "changed",
                     G_CALLBACK(comment_changed), ks);
    gtk_grid_attach(GTK_GRID(grid), ks->comment_entry, 1, row, 1, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_label("Key p&assphrase:"),
                    0, row, 1, 1);
    ks->passphrase_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(ks->passphrase_entry), false);
    gtk_grid_attach(GTK_GRID(grid), ks->passphrase_entry, 1, row, 1, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_label("C&onfirm passphrase:"),
                    0, row, 1, 1);
    ks->confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(ks->confirm_entry), false);
    gtk_grid_attach(GTK_GRID(grid), ks->confirm_entry, 1, row, 1, 1);
    row++;

    ks->public_label = make_label("&Public key for pasting into OpenSSH "
                                  "authorized_keys file:");
    gtk_grid_attach(GTK_GRID(grid), ks->public_label, 0, row, 2, 1);
    row++;

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroller, 420, 72);
    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview),
                                GTK_WRAP_WORD_CHAR);
    ks->publicview = textview;
    gtk_container_add(GTK_CONTAINER(scroller), textview);
    gtk_grid_attach(GTK_GRID(grid), scroller, 0, row, 2, 1);

    /* Nothing to act on until the user does something. */
    gtk_combo_box_set_active(GTK_COMBO_BOX(ks->keytype_combo), 0);
    set_key_display(ks);

    gtk_widget_show_all(window);
}

static void activate(GApplication *gapplication, gpointer vp)
{
    struct keygen_state *ks = vp;

    if (!ks->window)
        build_window(GTK_APPLICATION(gapplication), ks);

    gtk_window_present(GTK_WINDOW(ks->window));
}

/* ----------------------------------------------------------------------
 * Things that every main() file is expected to provide. The
 * command-line front end has its own versions of all of these; ours
 * talk to the user through dialogs instead of through a terminal.
 */

const bool buildinfo_gtk_relevant = true;

char *x_get_default(const char *key)
{
    /*
     * We don't use X resources, and on this platform there aren't any.
     */
    return NULL;
}

void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *text;

    va_start(ap, fmt);
    text = l10n_dupvprintf(fmt, ap);
    va_end(ap);

    if (the_state && the_state->window)
        gui_message_raw(the_state, GTK_MESSAGE_ERROR,
                        "PuTTYgen Fatal Error", text);
    else
        fprintf(stderr, "%s\n", text);
    sfree(text);

    exit(1);
}

void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char *text;

    va_start(ap, fmt);
    text = l10n_dupvprintf(fmt, ap);
    va_end(ap);

    if (the_state && the_state->window)
        gui_message_raw(the_state, GTK_MESSAGE_WARNING,
                        "PuTTYgen Warning", text);
    else
        fprintf(stderr, "%s\n", text);
    sfree(text);
}

/*
 * Warn about the obsolescent key file format. The wording is the same
 * as the other front ends' so that it's translated too.
 */
void old_keyfile_warning(void)
{
    if (!the_state || !the_state->window)
        return;

    gui_message(the_state, GTK_MESSAGE_WARNING, "PuTTY Key File Warning",
                "You are loading an SSH-2 private key which has an"
                "\nold version of the file format. This means your key"
                "\nfile is not fully tamperproof. Future versions of"
                "\nPuTTY may stop supporting this private key format,"
                "\nso we recommend you convert your key to the new"
                "\nformat."
                "\n"
                "\nOnce the key is loaded into PuTTYgen, you can perform"
                "\nthis conversion simply by saving it again.");
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    enable_dit();
    setup_macos_bundle_environment();

    /*
     * Set up the random number generator right here, because it can
     * only be created once per process. Key generation puts extra
     * entropy into it each time, but never re-creates it.
     */
    random_setup_special();

    the_state = snew(struct keygen_state);
    the_state->fptype = SSH_FPTYPE_SHA256;
    the_state->save_params = ppk_save_default_parameters;

    app = gtk_application_new(KEYGEN_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    the_state->app = app;
    g_signal_connect(G_OBJECT(app), "activate", G_CALLBACK(activate),
                     the_state);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

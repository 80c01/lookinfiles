/* Help cards in editor tabs (see code_help.h) */
#include "code_help.h"
#include "text_tags.h"
#include "code_intel.h"
#include "code_scan.h"
#include "i18n.h"
#include "markdown.h"
#include "theme.h"
#include <string.h>

#define HOVER_DELAY_MS 800
#define SIGNATURE_DELAY_MS 120
#define CARD_MAX_WIDTH 620
#define CARD_MIN_WIDTH 160
#define CARD_MAX_HEIGHT 320
#define HELP_KEY "code-help"
#define OCCURRENCE_DELAY_MS 250
#define OCCURRENCE_MAX_BYTES (2 * 1024 * 1024)
#define OCCURRENCE_TAG "code-occurrence"

typedef enum {
    CARD_NONE = 0,
    CARD_HELP,        /* asked from the keyboard */
    CARD_MOUSE,       /* mouse rest */
    CARD_SIGNATURE
} CardKind;

typedef struct {
    AppData* app;
    EditorTab* tab;
    CodeNavMessageFunc message;
    GtkWidget* popover;
    GtkWidget* scroll;
    GtkWidget* text;
    CardKind showing;      /* card on screen */
    CardKind asked;        /* kind of the latest request */
    guint serial;          /* of the latest request: older answers are dropped */
    gint anchor_line;      /* where the card points (word start) */
    gint anchor_index;
    gchar* word;           /* identifier of a help request */
    gchar* call;           /* function of a signature request */
    gint argument;         /* argument under the cursor in that call */
    GdkRectangle mouse_rect; /* word under the mouse (widget coordinates) */
    gdouble mouse_x, mouse_y;
    guint hover_timer;
    guint signature_timer;
    guint occurrence_timer;
    gchar* occurrence;     /* identifier highlighted, or NULL */
    gchar* diagnostics;    /* diagnostics at the place of the help request (Markdown), or NULL */
} CodeHelp;

static CodeHelp* help_of(EditorTab* tab) {
    return tab != NULL && tab->view != NULL ? g_object_get_data(G_OBJECT(tab->view), HELP_KEY) : NULL;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css("popover.code-help > contents { background-color: @bg_raised; padding: 0;"
                  " border: 1px solid @border_strong; border-radius: 4px; }"
                  "popover.code-help textview, popover.code-help textview text {"
                  " background-color: @bg_raised; color: @fg; }");
}

/* ------------------------------------------------------------------------- */
/* The card                                                                  */
/* ------------------------------------------------------------------------- */

void code_help_hide(EditorTab* tab) {
    CodeHelp* h = help_of(tab);
    if (h == NULL) return;
    h->asked = CARD_NONE;
    h->serial++;
    if (h->popover != NULL && h->showing != CARD_NONE) gtk_popover_popdown(GTK_POPOVER(h->popover));
    h->showing = CARD_NONE;
}

static void hide_kind(CodeHelp* h, CardKind kind) {
    if (h->showing == kind || h->asked == kind) code_help_hide(h->tab);
}

static void ensure_popover(CodeHelp* h) {
    if (h->popover != NULL) return;
    ensure_css();
    h->popover = gtk_popover_new();
    gtk_widget_add_css_class(h->popover, "code-help");
    gtk_popover_set_autohide(GTK_POPOVER(h->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(h->popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(h->popover), GTK_POS_TOP);
    gtk_widget_set_can_focus(h->popover, FALSE);
    /* Not the text view: GtkTextView only accepts its own children */
    gtk_widget_set_parent(h->popover, h->tab->container);
    h->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(h->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(h->scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(h->scroll), CARD_MAX_HEIGHT);
    h->text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(h->text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(h->text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(h->text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(h->text), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(h->text), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(h->text), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(h->text), 6);
    gtk_widget_set_can_focus(h->text, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(h->scroll), h->text);
    gtk_popover_set_child(GTK_POPOVER(h->popover), h->scroll);
}

/* Width for the longest rendered line of the card (the text wraps beyond) */
static gint card_width(CodeHelp* h, GtkTextBuffer* buf) {
    gint longest = 0;
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(buf, &it);
    do {
        GtkTextIter e = it;
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
        longest = MAX(longest, gtk_text_iter_get_line_offset(&e));
    } while (gtk_text_iter_forward_line(&it));
    PangoContext* ctx = gtk_widget_get_pango_context(h->text);
    PangoFontMetrics* m = pango_context_get_metrics(ctx, NULL, NULL);
    gint char_w = pango_font_metrics_get_approximate_char_width(m) / PANGO_SCALE;
    pango_font_metrics_unref(m);
    /* Code blocks use a wider monospace font and an indent */
    return CLAMP(longest * MAX(char_w, 6) * 13 / 10 + 48, CARD_MIN_WIDTH, CARD_MAX_WIDTH);
}

/* Headings of server documentation as bold lines: a card is small */
static gchar* flatten_headings(const gchar* markdown) {
    GString* out = g_string_new(NULL);
    gchar** lines = g_strsplit(markdown, "\n", -1);
    gboolean fence = FALSE;
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* l = lines[i];
        if (g_str_has_prefix(l, "```")) fence = !fence;
        if (!fence && l[0] == '#') {
            const gchar* t = l;
            while (*t == '#') t++;
            if (*t == ' ') {
                gchar* text = g_strstrip(g_strdup(t));
                g_string_append_printf(out, "**%s**", text);
                g_free(text);
            } else {
                g_string_append(out, l);
            }
        } else {
            g_string_append(out, l);
        }
        if (lines[i + 1] != NULL) g_string_append_c(out, '\n');
    }
    g_strfreev(lines);
    return g_string_free(out, FALSE);
}

static void show_card(CodeHelp* h, CardKind kind, const gchar* markdown_in) {
    if (markdown_in == NULL || markdown_in[0] == '\0' || h->tab->view == NULL || !gtk_widget_get_mapped(h->tab->view)) {
        return;
    }
    gchar* markdown = flatten_headings(markdown_in);
    if ((kind == CARD_HELP || kind == CARD_MOUSE) && h->diagnostics != NULL && markdown_in != h->diagnostics) {
        /* Problems first, like VS Code */
        gchar* both = g_strdup_printf("%s\n\n---\n\n%s", h->diagnostics, markdown);
        g_free(markdown);
        markdown = both;
    }
    ensure_popover(h);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(h->text));
    ensure_markdown_tags(buf);
    gtk_text_buffer_set_text(buf, "", 0);
    render_markdown_to_buffer(buf, NULL, markdown, NULL, FALSE, NULL, 0);
    /* No empty line at the end */
    GtkTextIter end, back;
    gtk_text_buffer_get_end_iter(buf, &end);
    back = end;
    while (gtk_text_iter_backward_char(&back) && g_unichar_isspace(gtk_text_iter_get_char(&back))) {
    }
    if (!gtk_text_iter_is_start(&back) || !g_unichar_isspace(gtk_text_iter_get_char(&back))) {
        gtk_text_iter_forward_char(&back);
    }
    gtk_text_buffer_delete(buf, &back, &end);
    /* Nor at the start (block gaps) */
    GtkTextIter start, first;
    gtk_text_buffer_get_start_iter(buf, &start);
    first = start;
    while (!gtk_text_iter_is_end(&first) && gtk_text_iter_get_char(&first) == '\n') gtk_text_iter_forward_char(&first);
    gtk_text_buffer_delete(buf, &start, &first);
    gtk_widget_set_size_request(h->scroll, card_width(h, buf), -1);

    GtkTextView* view = GTK_TEXT_VIEW(h->tab->view);
    GtkTextIter it;
    gint lines = gtk_text_buffer_get_line_count(h->tab->buffer);
    gtk_text_buffer_get_iter_at_line(h->tab->buffer, &it, CLAMP(h->anchor_line, 0, lines - 1));
    GtkTextIter eol = it;
    if (!gtk_text_iter_ends_line(&eol)) gtk_text_iter_forward_to_line_end(&eol);
    gtk_text_iter_set_line_index(&it, CLAMP(h->anchor_index, 0, gtk_text_iter_get_line_index(&eol)));
    GdkRectangle r;
    gtk_text_view_get_iter_location(view, &it, &r);
    gint wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(view, GTK_TEXT_WINDOW_WIDGET, r.x, r.y, &wx, &wy);
    graphene_point_t in_view = GRAPHENE_POINT_INIT((float)wx, (float)wy), in_parent;
    if (!gtk_widget_compute_point(h->tab->view, h->tab->container, &in_view, &in_parent)) {
        g_free(markdown);
        return;
    }
    GdkRectangle at = {(gint)in_parent.x, (gint)in_parent.y, MAX(r.width, 1), MAX(r.height, 1)};
    gtk_popover_set_pointing_to(GTK_POPOVER(h->popover), &at);
    h->showing = kind;
    gtk_popover_popup(GTK_POPOVER(h->popover));
    g_free(markdown);
}

/* Markdown special characters escaped */
static void append_escaped(GString* out, const gchar* s, gssize len) {
    if (len < 0) len = (gssize)strlen(s);
    for (gssize i = 0; i < len; i++) {
        if (strchr("\\`*_[]<>#|", s[i]) != NULL) g_string_append_c(out, '\\');
        g_string_append_c(out, s[i]);
    }
}

/* ------------------------------------------------------------------------- */
/* Help card                                                                 */
/* ------------------------------------------------------------------------- */

/* A declaration line as a signature: no body or terminator */
static gchar* clean_declaration(const gchar* text) {
    gchar* s = g_strdup(text != NULL ? text : "");
    g_strstrip(s);
    gsize n = strlen(s);
    while (n > 0 && (s[n - 1] == '{' || s[n - 1] == ';' || s[n - 1] == ' ' || s[n - 1] == ':')) s[--n] = '\0';
    return s;
}

static const gchar* shown_path(const gchar* path, const gchar* root) {
    gsize n = root != NULL ? strlen(root) : 0;
    if (n > 0 && strncmp(path, root, n) == 0 && path[n] == '/') return path + n + 1;
    return path;
}

static void on_found(AppData* app, EditorTab* tab, const CodeMatch* match, const gchar* doc, const gchar* root,
                     gpointer data) {
    (void)app;
    CodeHelp* h = help_of(tab);
    if (h == NULL || GPOINTER_TO_UINT(data) != h->serial) return;
    if (h->asked == CARD_SIGNATURE) {
        if (match == NULL) {
            hide_kind(h, CARD_SIGNATURE);
            return;
        }
        gchar* sig = match->symbol->signature != NULL ? g_strdup(match->symbol->signature)
                                                      : clean_declaration(match->symbol->text);
        gint s = -1, e = -1;
        code_scan_parameter_range(sig, h->argument, &s, &e);
        if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
            gchar* line = s >= 0 ? g_strdup_printf("%.*s[%.*s]%s", s, sig, e - s, sig + s, sig + e) : g_strdup(sig);
            if (h->message != NULL) h->message(tab, line);
            g_free(line);
        } else {
            GString* md = g_string_new(NULL);
            if (s >= 0) {
                append_escaped(md, sig, s);
                g_string_append(md, "**");
                append_escaped(md, sig + s, e - s);
                g_string_append(md, "**");
                append_escaped(md, sig + e, -1);
            } else {
                append_escaped(md, sig, -1);
            }
            if (doc != NULL) {
                g_string_append(md, "\n\n");
                append_escaped(md, doc, -1);
            }
            show_card(h, CARD_SIGNATURE, md->str);
            g_string_free(md, TRUE);
        }
        g_free(sig);
        return;
    }
    CardKind kind = h->asked;
    if (match == NULL && h->diagnostics != NULL) {
        show_card(h, kind, h->diagnostics);
        return;
    }
    if (match == NULL) {
        if (kind == CARD_HELP && h->message != NULL) {
            gchar* msg = g_strdup_printf(_("No information found for %s"), h->word);
            h->message(tab, msg);
            g_free(msg);
        }
        return;
    }
    /* Like VS Code: the declaration, its documentation, then what and where */
    const CodeSymbol* sym = match->symbol;
    GString* md = g_string_new("```\n");
    gchar* decl = sym->signature != NULL ? g_strdup(sym->signature) : clean_declaration(sym->text);
    g_string_append(md, decl);
    g_free(decl);
    g_string_append(md, "\n```\n\n");
    if (doc != NULL) {
        append_escaped(md, doc, -1);
        g_string_append(md, "\n\n");
    }
    const gchar* kind_name = code_symbol_kind_name(sym->kind);
    /* TRANSLATORS: %s is a symbol kind, e.g. "function" */
    gchar* what = sym->declaration ? g_strdup_printf(_("%s declaration"), kind_name) : g_strdup(kind_name);
    g_string_append_printf(md, "*%s* · %s:%d", what, shown_path(match->path, root), sym->line + 1);
    g_free(what);
    show_card(h, kind, md->str);
    g_string_free(md, TRUE);
}

static void index_lookup(CodeHelp* h, const gchar* name) {
    if (!code_nav_find(h->app, h->tab, name, on_found, GUINT_TO_POINTER(h->serial)) && h->asked == CARD_HELP &&
        h->message != NULL) {
        h->message(h->tab, _("No code information for this file type"));
    }
}

static void on_hover(AppData* app, EditorTab* tab, const gchar* markdown, gpointer data) {
    (void)app;
    CodeHelp* h = help_of(tab);
    if (h == NULL || GPOINTER_TO_UINT(data) != h->serial) return;
    if (markdown != NULL) show_card(h, h->asked, markdown);
    else index_lookup(h, h->word);
}

/* Help for the identifier at it (from the keyboard, or under the mouse) */
static void request_help(CodeHelp* h, const GtkTextIter* it, CardKind kind) {
    if (!code_nav_is_code(h->tab)) {
        if (kind == CARD_HELP && h->message != NULL) h->message(h->tab, _("No code information for this file type"));
        return;
    }
    GtkTextIter s = *it;
    gtk_text_iter_set_line_offset(&s, 0);
    GtkTextIter e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    gchar* line = gtk_text_iter_get_slice(&s, &e);
    gint ws = 0, we = 0;
    CodeLang lang = code_lang_from_name(h->tab->path);
    gchar* diagnostics = code_intel_diagnostics_at(h->app, h->tab, gtk_text_iter_get_line(it),
                                                   gtk_text_iter_get_line_index(it));
    if (!code_word_at(line, -1, gtk_text_iter_get_line_index(it), lang, &ws, &we)) {
        g_free(line);
        if (diagnostics != NULL) {
            /* Only a problem here (punctuation, end of line) */
            code_help_hide(h->tab);
            g_free(h->diagnostics);
            h->diagnostics = diagnostics;
            h->asked = kind;
            h->anchor_line = gtk_text_iter_get_line(it);
            h->anchor_index = gtk_text_iter_get_line_index(it);
            show_card(h, kind, diagnostics);
            return;
        }
        if (kind == CARD_HELP && h->message != NULL) h->message(h->tab, _("No identifier under the cursor"));
        return;
    }
    code_help_hide(h->tab);
    g_free(h->diagnostics);
    h->diagnostics = diagnostics;
    h->asked = kind;
    g_free(h->word);
    h->word = g_strndup(line + ws, (gsize)(we - ws));
    h->anchor_line = gtk_text_iter_get_line(it);
    h->anchor_index = ws;
    g_free(line);
    if (!code_intel_hover(h->app, h->tab, h->anchor_line, ws, on_hover, GUINT_TO_POINTER(h->serial))) {
        index_lookup(h, h->word);
    }
}

void code_help_show(AppData* app, EditorTab* tab) {
    (void)app;
    CodeHelp* h = help_of(tab);
    if (h == NULL) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    request_help(h, &it, CARD_HELP);
}

/* ------------------------------------------------------------------------- */
/* Signature help                                                            */
/* ------------------------------------------------------------------------- */

static void on_signature(AppData* app, EditorTab* tab, const LspSignature* sig, gpointer data) {
    (void)app;
    CodeHelp* h = help_of(tab);
    if (h == NULL || GPOINTER_TO_UINT(data) != h->serial) return;
    if (sig == NULL || sig->label[0] == '\0') {
        index_lookup(h, h->call);
        return;
    }
    gint s = sig->param_start, e = sig->param_end;
    gboolean range = s >= 0 && e >= s && e <= (gint)strlen(sig->label);
    if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
        gchar* line = range ? g_strdup_printf("%.*s[%.*s]%s", s, sig->label, e - s, sig->label + s, sig->label + e)
                            : g_strdup(sig->label);
        if (h->message != NULL) h->message(tab, line);
        g_free(line);
        return;
    }
    GString* md = g_string_new(NULL);
    if (range) {
        append_escaped(md, sig->label, s);
        g_string_append(md, "**");
        append_escaped(md, sig->label + s, e - s);
        g_string_append(md, "**");
        append_escaped(md, sig->label + e, -1);
    } else {
        append_escaped(md, sig->label, -1);
    }
    if (sig->count > 1) g_string_append_printf(md, "  (1 of %u)", sig->count);
    if (sig->documentation != NULL) {
        g_string_append(md, "\n\n");
        g_string_append(md, sig->documentation);
    }
    show_card(h, CARD_SIGNATURE, md->str);
    g_string_free(md, TRUE);
}

/* Signature of the call at the cursor; explicit: asked by a key */
static void request_signature(CodeHelp* h, gboolean explicit_) {
    EditorTab* tab = h->tab;
    if (!code_nav_is_code(tab)) {
        if (explicit_ && h->message != NULL) h->message(tab, _("No code information for this file type"));
        return;
    }
    GtkTextIter it, s, e;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    s = it;
    gtk_text_iter_set_line_offset(&s, 0);
    e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    gchar* line = gtk_text_iter_get_slice(&s, &e);
    gint index = gtk_text_iter_get_line_index(&it);
    gint ns = 0, ne = 0, arg = 0;
    CodeLang lang = code_lang_from_name(tab->path);
    if (!code_scan_call_at(line, -1, index, lang, &ns, &ne, &arg)) {
        g_free(line);
        hide_kind(h, CARD_SIGNATURE);
        if (explicit_ && h->message != NULL) h->message(tab, _("The cursor is not in a function call"));
        return;
    }
    if (h->showing != CARD_SIGNATURE) code_help_hide(tab);
    h->serial++;
    h->asked = CARD_SIGNATURE;
    g_free(h->call);
    h->call = g_strndup(line + ns, (gsize)(ne - ns));
    h->argument = arg;
    h->anchor_line = gtk_text_iter_get_line(&it);
    h->anchor_index = ns;
    g_free(line);
    if (!code_intel_signature(h->app, tab, h->anchor_line, index, on_signature, GUINT_TO_POINTER(h->serial))) {
        index_lookup(h, h->call);
    }
}

void code_help_signature(AppData* app, EditorTab* tab) {
    (void)app;
    CodeHelp* h = help_of(tab);
    if (h != NULL) request_signature(h, TRUE);
}

static gboolean on_signature_timer(gpointer data) {
    CodeHelp* h = data;
    h->signature_timer = 0;
    request_signature(h, FALSE);
    return G_SOURCE_REMOVE;
}

static void schedule_signature(CodeHelp* h) {
    if (h->signature_timer != 0) g_source_remove(h->signature_timer);
    h->signature_timer = g_timeout_add(SIGNATURE_DELAY_MS, on_signature_timer, h);
}

static void clear_occurrences(CodeHelp* h);
static void schedule_occurrences(CodeHelp* h);

/* The edit may have made or broken occurrences: highlight again */
static void reset_occurrences(CodeHelp* h) {
    if (h->occurrence == NULL) return;
    clear_occurrences(h);
    schedule_occurrences(h);
}

static void on_insert_text(GtkTextBuffer* buffer, GtkTextIter* at, gchar* text, gint len, gpointer data) {
    (void)buffer;
    (void)at;
    CodeHelp* h = data;
    reset_occurrences(h);
    if (h->tab->view == NULL || !gtk_widget_has_focus(h->tab->view)) return;
    gboolean open_card = h->showing == CARD_SIGNATURE || h->asked == CARD_SIGNATURE;
    if (len == 1 && (strchr(code_intel_signature_triggers(h->app, h->tab), text[0]) != NULL || text[0] == ')')) {
        if (text[0] != ')' || open_card) schedule_signature(h);
    } else if (open_card) {
        schedule_signature(h);
    }
}

static void on_delete_range(GtkTextBuffer* buffer, GtkTextIter* s, GtkTextIter* e, gpointer data) {
    (void)buffer;
    (void)s;
    (void)e;
    CodeHelp* h = data;
    reset_occurrences(h);
    if (h->showing == CARD_SIGNATURE) schedule_signature(h);
}

/* ------------------------------------------------------------------------- */
/* Other occurrences of the identifier at the cursor                         */
/* ------------------------------------------------------------------------- */

static void clear_occurrences(CodeHelp* h) {
    if (h->occurrence == NULL) return;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(h->tab->buffer, &s, &e);
    gtk_text_buffer_remove_tag_by_name(h->tab->buffer, OCCURRENCE_TAG, &s, &e);
    g_free(h->occurrence);
    h->occurrence = NULL;
}

static gboolean on_occurrence_timer(gpointer data) {
    CodeHelp* h = data;
    h->occurrence_timer = 0;
    GtkTextBuffer* buf = h->tab->buffer;
    CodeLang lang = code_lang_from_name(h->tab->path);
    GtkTextIter it, s, e;
    gtk_text_buffer_get_iter_at_mark(buf, &it, gtk_text_buffer_get_insert(buf));
    gchar* word = NULL;
    if (!gtk_text_buffer_get_has_selection(buf) && gtk_text_buffer_get_char_count(buf) < OCCURRENCE_MAX_BYTES &&
        code_nav_is_code(h->tab)) {
        s = it;
        gtk_text_iter_set_line_offset(&s, 0);
        e = s;
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
        gchar* line = gtk_text_iter_get_slice(&s, &e);
        gint ws = 0, we = 0;
        if (code_word_at(line, -1, gtk_text_iter_get_line_index(&it), lang, &ws, &we)) {
            word = g_strndup(line + ws, (gsize)(we - ws));
        }
        g_free(line);
    }
    if (g_strcmp0(word, h->occurrence) == 0) {
        g_free(word);
        return G_SOURCE_REMOVE;
    }
    clear_occurrences(h);
    if (word == NULL) return G_SOURCE_REMOVE;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    if (gtk_text_tag_table_lookup(table, OCCURRENCE_TAG) == NULL) {
        text_tag_create(buf, OCCURRENCE_TAG, "background", "@editor_occurrence", NULL);
    }
    /* Whole words only */
    GtkTextIter from, ms, me;
    gtk_text_buffer_get_start_iter(buf, &from);
    while (gtk_text_iter_forward_search(&from, word, GTK_TEXT_SEARCH_TEXT_ONLY, &ms, &me, NULL)) {
        GtkTextIter before = ms;
        gboolean start_ok = !gtk_text_iter_backward_char(&before) || !code_is_word_char(lang, gtk_text_iter_get_char(&before));
        gboolean end_ok = gtk_text_iter_is_end(&me) || !code_is_word_char(lang, gtk_text_iter_get_char(&me));
        if (start_ok && end_ok) {
            gtk_text_buffer_apply_tag_by_name(buf, OCCURRENCE_TAG, &ms, &me);
        }
        from = me;
    }
    h->occurrence = word;
    return G_SOURCE_REMOVE;
}

static void schedule_occurrences(CodeHelp* h) {
    if (h->occurrence_timer != 0) g_source_remove(h->occurrence_timer);
    h->occurrence_timer = g_timeout_add(OCCURRENCE_DELAY_MS, on_occurrence_timer, h);
}

static void on_cursor_moved(GObject* buffer, GParamSpec* pspec, gpointer data) {
    (void)buffer;
    (void)pspec;
    CodeHelp* h = data;
    if (h->tab->view_passive) return; /* another view of the split file moved it */
    schedule_occurrences(h);
    if (h->showing == CARD_SIGNATURE) {
        schedule_signature(h);
    } else if (h->showing == CARD_HELP) {
        /* The keyboard card belongs to the place it was asked */
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(h->tab->buffer, &it, gtk_text_buffer_get_insert(h->tab->buffer));
        if (gtk_text_iter_get_line(&it) != h->anchor_line) code_help_hide(h->tab);
    }
}

/* ------------------------------------------------------------------------- */
/* Mouse, keys, focus                                                        */
/* ------------------------------------------------------------------------- */

static void cancel_hover(CodeHelp* h) {
    if (h->hover_timer != 0) {
        g_source_remove(h->hover_timer);
        h->hover_timer = 0;
    }
}

static gboolean on_hover_timer(gpointer data) {
    CodeHelp* h = data;
    h->hover_timer = 0;
    if (!code_nav_is_code(h->tab) || h->showing == CARD_SIGNATURE) return G_SOURCE_REMOVE;
    GtkTextView* view = GTK_TEXT_VIEW(h->tab->view);
    gint bx = 0, by = 0;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)h->mouse_x, (gint)h->mouse_y, &bx, &by);
    GtkTextIter it;
    if (!gtk_text_view_get_iter_at_location(view, &it, bx, by)) return G_SOURCE_REMOVE;
    /* Only over the text itself, not the empty space after a line */
    GdkRectangle r;
    gtk_text_view_get_iter_location(view, &it, &r);
    if (bx < r.x - 1 || bx > r.x + MAX(r.width, 8) + 1) return G_SOURCE_REMOVE;
    gint wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(view, GTK_TEXT_WINDOW_WIDGET, r.x, r.y, &wx, &wy);
    h->mouse_rect = (GdkRectangle){wx - 40, wy - 4, r.width + 80, r.height + 8};
    request_help(h, &it, CARD_MOUSE);
    return G_SOURCE_REMOVE;
}

static void on_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    CodeHelp* h = data;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (h->showing == CARD_MOUSE || h->asked == CARD_MOUSE) {
        GdkRectangle* r = &h->mouse_rect;
        if (x < r->x || x > r->x + r->width || y < r->y || y > r->y + r->height) code_help_hide(h->tab);
    }
    cancel_hover(h);
    h->mouse_x = x;
    h->mouse_y = y;
    if ((mods & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) == 0 && h->showing != CARD_MOUSE) {
        h->hover_timer = g_timeout_add(HOVER_DELAY_MS, on_hover_timer, h);
    }
}

static void on_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    CodeHelp* h = data;
    cancel_hover(h);
    hide_kind(h, CARD_MOUSE);
}

static void on_press(GtkGestureClick* gesture, gint n, gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n;
    (void)x;
    (void)y;
    CodeHelp* h = data;
    cancel_hover(h);
    hide_kind(h, CARD_MOUSE);
    hide_kind(h, CARD_HELP);
}

static gboolean on_scroll(GtkEventControllerScroll* ctrl, gdouble dx, gdouble dy, gpointer data) {
    (void)ctrl;
    (void)dx;
    (void)dy;
    CodeHelp* h = data;
    cancel_hover(h);
    if (h->showing != CARD_NONE) code_help_hide(h->tab);
    return FALSE;
}

static gboolean is_modifier(guint keyval) {
    switch (keyval) {
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: case GDK_KEY_Control_L: case GDK_KEY_Control_R:
        case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
        case GDK_KEY_Super_L: case GDK_KEY_Super_R: case GDK_KEY_ISO_Level3_Shift: case GDK_KEY_Caps_Lock:
            return TRUE;
        default:
            return FALSE;
    }
}

void code_help_on_key(EditorTab* tab, guint keyval) {
    CodeHelp* h = help_of(tab);
    if (h == NULL) return;
    cancel_hover(h);
    if (is_modifier(keyval)) return;
    if (keyval == GDK_KEY_Escape || h->showing == CARD_HELP || h->showing == CARD_MOUSE ||
        h->asked == CARD_HELP || h->asked == CARD_MOUSE) {
        if (h->showing != CARD_NONE || h->asked != CARD_NONE) code_help_hide(h->tab);
    }
}

static void on_focus_leave(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    CodeHelp* h = data;
    cancel_hover(h);
    code_help_hide(h->tab);
}

static void on_view_destroy(GtkWidget* view, gpointer data) {
    (void)view;
    CodeHelp* h = data;
    cancel_hover(h);
    if (h->signature_timer != 0) g_source_remove(h->signature_timer);
    h->signature_timer = 0;
    if (h->occurrence_timer != 0) g_source_remove(h->occurrence_timer);
    h->occurrence_timer = 0;
    if (h->popover != NULL) {
        gtk_widget_unparent(h->popover);
        h->popover = NULL;
    }
    g_signal_handlers_disconnect_by_data(h->tab->buffer, h);
}

void code_help_detach(EditorTab* tab) {
    CodeHelp* h = help_of(tab);
    if (h == NULL) return;
    g_signal_handlers_disconnect_by_func(tab->view, (gpointer)on_view_destroy, h);
    on_view_destroy(tab->view, h);
}

static void help_free(gpointer p) {
    CodeHelp* h = p;
    g_free(h->word);
    g_free(h->call);
    g_free(h->occurrence);
    g_free(h->diagnostics);
    g_free(h);
}

void code_help_attach(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (tab == NULL || tab->view == NULL || help_of(tab) != NULL) return;
    CodeHelp* h = g_new0(CodeHelp, 1);
    h->app = app;
    h->tab = tab;
    h->message = message;
    g_object_set_data_full(G_OBJECT(tab->view), HELP_KEY, h, help_free);
    g_signal_connect(tab->view, "destroy", G_CALLBACK(on_view_destroy), h);

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), h);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), h);
    gtk_widget_add_controller(tab->view, motion);

    GtkGesture* press = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(press), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(press), GTK_PHASE_CAPTURE);
    g_signal_connect(press, "pressed", G_CALLBACK(on_press), h);
    gtk_widget_add_controller(tab->view, GTK_EVENT_CONTROLLER(press));

    GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), h);
    gtk_widget_add_controller(tab->view, scroll);

    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), h);
    gtk_widget_add_controller(tab->view, focus);

    g_signal_connect_after(tab->buffer, "insert-text", G_CALLBACK(on_insert_text), h);
    g_signal_connect_after(tab->buffer, "delete-range", G_CALLBACK(on_delete_range), h);
    g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), h);
}

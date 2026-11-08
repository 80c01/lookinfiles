/* Search bars of the explorer and the preview. Each panel has its own bar, at
 * its bottom, drawn like the search of an editor tab in the global mode:
 * Notepad find bar (Find Next / Find Prev), Emacs I-search minibuffer (match
 * count, Failing / Wrapped prompt), Vim "/" command line (E486 message). */
#include "search/search.h"
#include "core/i18n.h"
#include "explorer/tree_nav.h"
#include "widgets/panels.h"
#include "theme/icons.h"
#include "editor/editor_modes.h"
#include "theme/text_tags.h"

#define SEARCH_MAX_COUNT 1000 /* matches counted for the Emacs [n/total] */

/* Search bar of the explorer or the preview, drawn like the search of an
 * editor tab in the global mode: Notepad find bar, Emacs I-search
 * minibuffer, Vim "/" command line */
typedef struct {
    GtkWidget* bar;
    GtkWidget* prompt;  /* "Find:", "I-search:", "/" */
    GtkWidget* entry;
    GtkWidget* echo;    /* match count, wrap or failure message */
    GtkWidget* buttons; /* Notepad: Find Next, Find Prev, close */
} AppSearchBar;

/* Both search bars and the open or last search, private to this file */
struct Search {
    AppSearchBar explorer_bar; /* at the bottom of the explorer */
    AppSearchBar viewer_bar;   /* at the bottom of the preview */
    gchar* term;               /* text of the open or last search, NULL if none */
    gboolean in_viewer;        /* '/' opened from the viewer, not the list */
    gboolean backward;         /* bar opened backward (Emacs C-r) */
    gint origin_row;           /* explorer row position when the bar opened (C-g goes back) */
    gdouble origin_scroll;     /* viewer scroll when the bar opened */
    gint viewer_match;         /* last viewer match (char offset), -1 = none */
    gint viewer_match_len;     /* its length in characters */
};

void search_init(AppData* app) {
    app->search = g_new0(struct Search, 1);
    app->search->viewer_match = -1;
}

void search_free(AppData* app) {
    if (app->search == NULL) {
        return;
    }
    g_free(app->search->term);
    g_clear_pointer(&app->search, g_free);
}

void search_forget_viewer_match(AppData* app) {
    if (app->search != NULL) {
        app->search->viewer_match = -1;
    }
}

typedef enum {
    SEARCH_FOUND,
    SEARCH_WRAPPED, /* found after going around the end */
    SEARCH_FAILED
} SearchResult;

static AppSearchBar* active_bar(AppData* app) {
    return app->search->in_viewer ? &app->search->viewer_bar : &app->search->explorer_bar;
}

/* Vertical scroll of the preview */
static GtkAdjustment* viewer_vadj(AppData* app) {
    return app->text_view != NULL ? gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view)) : NULL;
}

static gboolean bar_open(AppData* app) {
    AppSearchBar* sb = active_bar(app);
    return sb->bar != NULL && gtk_widget_get_visible(sb->bar);
}

/* ------------------------------------------------------------------------- */
/* Case-insensitive matching (char-wise folding)                             */
/* ------------------------------------------------------------------------- */

/* Lower-cased code points of s; *len receives their number */
static gunichar* fold_chars(const gchar* s, gint* len) {
    gint n = g_utf8_strlen(s, -1);
    gunichar* cv = g_new(gunichar, n > 0 ? n : 1);
    const gchar* p = s;
    for (gint i = 0; i < n; i++) {
        cv[i] = g_unichar_tolower(g_utf8_get_char(p));
        p = g_utf8_next_char(p);
    }
    *len = n;
    return cv;
}

static gint folded_find_from(const gunichar* hcv, gint hlen, const gunichar* ncv, gint nlen, gint start) {
    if (nlen == 0 || hlen == 0 || nlen > hlen || start < 0) {
        return -1;
    }
    for (gint k = start; k <= hlen - nlen; k++) {
        if (hcv[k] != ncv[0]) {
            continue;
        }
        gint j = 1;
        while (j < nlen && hcv[k + j] == ncv[j]) {
            j++;
        }
        if (j == nlen) {
            return k;
        }
    }
    return -1;
}

/* Case-insensitive search (char-wise folding) from a char-offset anchor */
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start) {
    gint hlen = 0, nlen = 0;
    gunichar* hcv = fold_chars(hay, &hlen);
    gunichar* ncv = fold_chars(needle, &nlen);
    gint match = folded_find_from(hcv, hlen, ncv, nlen, start);
    g_free(hcv);
    g_free(ncv);
    return match;
}

/* ------------------------------------------------------------------------- */
/* Bar look: the search of an editor tab in the global mode                  */
/* ------------------------------------------------------------------------- */

static const gchar* const BAR_CLASSES[] = {"notepad-bar", "emacs-minibuffer", "vim-commandline"};
static const gchar* const PROMPT_CLASSES[] = {"emacs-minibuffer-prompt", "vim-commandline-prompt"};
static const gchar* const ENTRY_CLASSES[] = {"emacs-minibuffer-entry", "vim-commandline-entry"};
static const gchar* const ECHO_CLASSES[] = {"emacs-minibuffer-echo", "vim-message-error", "dim-label"};

static void remove_classes(GtkWidget* w, const gchar* const* classes, gsize n) {
    for (gsize i = 0; i < n; i++) {
        gtk_widget_remove_css_class(w, classes[i]);
    }
}

static void set_echo(AppSearchBar* sb, const gchar* text, const gchar* css_class) {
    remove_classes(sb->echo, ECHO_CLASSES, G_N_ELEMENTS(ECHO_CLASSES));
    if (css_class != NULL) {
        gtk_widget_add_css_class(sb->echo, css_class);
    }
    gtk_label_set_text(GTK_LABEL(sb->echo), text != NULL ? text : "");
    gtk_widget_set_visible(sb->echo, text != NULL && text[0] != '\0');
}

/* Emacs prompt: "I-search: ", "Failing wrapped I-search backward: "... */
static void set_emacs_prompt(AppData* app, AppSearchBar* sb, gboolean failing, gboolean wrapped) {
    gchar* prompt = g_strdup_printf("%s%sI-search%s: ", failing ? "Failing " : "",
                                    wrapped ? (failing ? "wrapped " : "Wrapped ") : "",
                                    app->search->backward ? " backward" : "");
    gtk_label_set_text(GTK_LABEL(sb->prompt), prompt);
    g_free(prompt);
}

/* The message goes after the entry (Emacs count, Vim message), or on a row
 * of its own below the Notepad buttons, which keep the width of the panel */
static void place_echo(AppSearchBar* sb, gboolean own_row) {
    GtkWidget* row = g_object_get_data(G_OBJECT(sb->bar), "search-row");
    GtkWidget* parent = gtk_widget_get_parent(sb->echo);
    if (parent == (own_row ? sb->bar : row)) {
        return;
    }
    g_object_ref(sb->echo);
    gtk_box_remove(GTK_BOX(parent), sb->echo);
    if (own_row) {
        gtk_box_append(GTK_BOX(sb->bar), sb->echo);
    } else {
        gtk_box_insert_child_after(GTK_BOX(row), sb->echo, sb->entry);
    }
    g_object_unref(sb->echo);
}

/* Give the bar the look of the global mode (it may change between searches) */
static void style_bar(AppData* app, AppSearchBar* sb) {
    NavStyle style = app_nav_style(app);
    place_echo(sb, style == NAV_STYLE_NOTEPAD);
    remove_classes(sb->bar, BAR_CLASSES, G_N_ELEMENTS(BAR_CLASSES));
    remove_classes(sb->prompt, PROMPT_CLASSES, G_N_ELEMENTS(PROMPT_CLASSES));
    remove_classes(sb->entry, ENTRY_CLASSES, G_N_ELEMENTS(ENTRY_CLASSES));
    gtk_widget_set_visible(sb->buttons, style == NAV_STYLE_NOTEPAD);
    /* Emacs and Vim: the typed text follows the prompt, with no field frame */
    gtk_widget_set_margin_end(sb->prompt, style == NAV_STYLE_NOTEPAD ? 6 : 0);
    if (style == NAV_STYLE_NOTEPAD) {
        gtk_widget_remove_css_class(sb->entry, "flat");
    } else {
        gtk_widget_add_css_class(sb->entry, "flat");
    }
    set_echo(sb, NULL, NULL);
    switch (style) {
    case NAV_STYLE_EMACS:
        gtk_widget_add_css_class(sb->bar, "emacs-minibuffer");
        gtk_widget_add_css_class(sb->prompt, "emacs-minibuffer-prompt");
        gtk_widget_add_css_class(sb->entry, "emacs-minibuffer-entry");
        set_emacs_prompt(app, sb, FALSE, FALSE);
        break;
    case NAV_STYLE_VIM:
        gtk_widget_add_css_class(sb->bar, "vim-commandline");
        gtk_widget_add_css_class(sb->prompt, "vim-commandline-prompt");
        gtk_widget_add_css_class(sb->entry, "vim-commandline-entry");
        gtk_label_set_text(GTK_LABEL(sb->prompt), app->search->backward ? "?" : "/");
        break;
    default:
        gtk_widget_add_css_class(sb->bar, "notepad-bar");
        gtk_label_set_text(GTK_LABEL(sb->prompt), _("Find:"));
        break;
    }
}

/* Show the outcome of a search the way the mode's editor does */
static void report(AppData* app, SearchResult result, gint index, gint total) {
    if (!bar_open(app)) {
        return;
    }
    AppSearchBar* sb = active_bar(app);
    const gchar* term = app->search->term != NULL ? app->search->term : "";
    gboolean empty = term[0] == '\0';
    gchar* msg = NULL;
    switch (app_nav_style(app)) {
    case NAV_STYLE_EMACS: {
        gboolean failing = !empty && result == SEARCH_FAILED;
        set_emacs_prompt(app, sb, failing, !empty && result == SEARCH_WRAPPED);
        const gchar* more = total >= SEARCH_MAX_COUNT ? "+" : "";
        if (!empty) {
            msg = failing ? g_strdup_printf("[?/%d%s]", total, more)
                          : g_strdup_printf("[%d/%d%s]", index, total, more);
        }
        set_echo(sb, msg, "emacs-minibuffer-echo");
        break;
    }
    case NAV_STYLE_VIM:
        if (!empty && result == SEARCH_FAILED) {
            msg = g_strdup_printf("E486: Pattern not found: %s", term);
            set_echo(sb, msg, "vim-message-error");
        } else if (!empty && result == SEARCH_WRAPPED) {
            set_echo(sb, app->search->backward ? "search hit TOP, continuing at BOTTOM"
                                              : "search hit BOTTOM, continuing at TOP",
                     "vim-message-error");
        } else {
            set_echo(sb, NULL, NULL);
        }
        break;
    default:
        if (!empty && result == SEARCH_FAILED) {
            msg = g_strdup_printf(_("Cannot find \"%s\""), term);
        }
        set_echo(sb, msg, "dim-label");
        break;
    }
    g_free(msg);
}

/* ------------------------------------------------------------------------- */
/* Explorer search: row names                                                */
/* ------------------------------------------------------------------------- */

static gboolean row_matches(AppData* app, ExplorerRow* row) {
    if (row == NULL || app->search->term == NULL || app->search->term[0] == '\0') {
        return FALSE;
    }
    gchar* hay = g_utf8_casefold(row->name, -1);
    gchar* needle = g_utf8_casefold(app->search->term, -1);
    gboolean res = (strstr(hay, needle) != NULL);
    g_free(hay);
    g_free(needle);
    return res;
}

/* Matching visible rows, and the 1-based index of the selected one */
static void report_rows(AppData* app, SearchResult result) {
    if (app->search->in_viewer || !bar_open(app)) {
        return;
    }
    gint total = 0, index = 0;
    ExplorerRow* selected = tree_nav_selected(app);
    guint n = explorer_tree_n_visible(app->tree);
    for (guint i = 0; i < n && total < SEARCH_MAX_COUNT; i++) {
        ExplorerRow* row = explorer_tree_visible_row(app->tree, i);
        if (row_matches(app, row)) {
            total++;
            if (row == selected) index = total;
        }
    }
    report(app, result, index, total);
}

static SearchResult explorer_search(AppData* app, gint direction) {
    gint n = (gint)explorer_tree_n_visible(app->tree);
    if (n == 0) {
        return SEARCH_FAILED;
    }
    ExplorerRow* selected = tree_nav_selected(app);
    gint cur = (selected != NULL) ? explorer_tree_position(app->tree, selected) : -1;
    for (gint k = 1; k <= n; k++) {
        gint idx = (direction > 0) ? ((cur + k) % n + n) % n
                                   : ((cur - k) % n + n) % n;
        ExplorerRow* row = explorer_tree_visible_row(app->tree, (guint)idx);
        if (row_matches(app, row)) {
            select_row(app, row);
            gboolean wrapped = cur >= 0 && (direction > 0 ? idx <= cur : idx >= cur);
            return wrapped ? SEARCH_WRAPPED : SEARCH_FOUND;
        }
    }
    return SEARCH_FAILED;
}

/* Select the next (direction > 0) or previous visible row whose name matches */
void do_search(AppData* app, gint direction) {
    if (app->search->term == NULL || app->search->term[0] == '\0') {
        return;
    }
    report_rows(app, explorer_search(app, direction));
}

/* ------------------------------------------------------------------------- */
/* Preview search: the text of the viewer                                    */
/* ------------------------------------------------------------------------- */

/* Matches in the text (capped), and the 1-based index of the one at match */
static gint count_matches(const gunichar* hcv, gint hlen, const gunichar* ncv, gint nlen,
                          gint match, gint* index) {
    gint total = 0;
    *index = 0;
    gint m = folded_find_from(hcv, hlen, ncv, nlen, 0);
    while (m >= 0 && total < SEARCH_MAX_COUNT) {
        total++;
        if (m == match) *index = total;
        m = folded_find_from(hcv, hlen, ncv, nlen, m + 1);
    }
    return total;
}

/* Viewer matches are tagged (palette roles editor_match and
 * editor_match_current) rather than selected while the bar is open: the
 * unfocused selection is too faint to see. The bar closing selects the
 * current match. */
#define VIEWER_MATCH_TAG "viewer-search-match"
#define VIEWER_CURRENT_TAG "viewer-search-current"
#define VIEWER_MAX_TAGGED 20000


static GtkTextTag* viewer_tag(GtkTextBuffer* buf, const gchar* name, const gchar* bg) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    GtkTextTag* tag = text_tag_ensure(buf, name, "background", bg, NULL);
    /* Above the syntax highlighting */
    gtk_text_tag_set_priority(tag, gtk_text_tag_table_get_size(table) - 1);
    return tag;
}

static void viewer_clear_tags(AppData* app) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(app->buffer);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(app->buffer, &s, &e);
    const gchar* names[] = {VIEWER_MATCH_TAG, VIEWER_CURRENT_TAG};
    for (gsize i = 0; i < G_N_ELEMENTS(names); i++) {
        GtkTextTag* tag = gtk_text_tag_table_lookup(table, names[i]);
        if (tag != NULL) gtk_text_buffer_remove_tag(app->buffer, tag, &s, &e);
    }
}

static void viewer_tag_range(AppData* app, GtkTextTag* tag, gint offset, gint len) {
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &s, offset);
    gtk_text_buffer_get_iter_at_offset(app->buffer, &e, offset + len);
    gtk_text_buffer_apply_tag(app->buffer, tag, &s, &e);
}

static void viewer_tag_matches(AppData* app, const gunichar* hcv, gint hlen, const gunichar* ncv, gint nlen,
                               gint match) {
    GtkTextTag* all = viewer_tag(app->buffer, VIEWER_MATCH_TAG, "@editor_match");
    GtkTextTag* current = viewer_tag(app->buffer, VIEWER_CURRENT_TAG, "@editor_match_current");
    gint count = 0;
    for (gint m = folded_find_from(hcv, hlen, ncv, nlen, 0); m >= 0 && count < VIEWER_MAX_TAGGED;
         m = folded_find_from(hcv, hlen, ncv, nlen, m + nlen)) {
        viewer_tag_range(app, all, m, nlen);
        count++;
    }
    if (match >= 0) viewer_tag_range(app, current, match, nlen);
}

void do_viewer_search(AppData* app, gint direction) {
    if (app->search->term == NULL || app->search->term[0] == '\0') {
        return;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(app->buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(app->buffer, &start, &end, FALSE);
    if (text == NULL || text[0] == '\0') {
        g_free(text);
        if (app->search->in_viewer) report(app, SEARCH_FAILED, 0, 0);
        return;
    }

    gint hlen = 0, nlen = 0;
    gunichar* hcv = fold_chars(text, &hlen);
    gunichar* ncv = fold_chars(app->search->term, &nlen);
    gint anchor = (app->search->viewer_match >= 0 && app->search->viewer_match < hlen) ? app->search->viewer_match : -1;

    gint match = -1;
    gboolean wrapped = FALSE;
    if (direction > 0) {
        gint from = anchor >= 0 ? anchor + 1 : 0;
        match = folded_find_from(hcv, hlen, ncv, nlen, from);
        if (match < 0 && from > 0) {
            match = folded_find_from(hcv, hlen, ncv, nlen, 0);
            wrapped = match >= 0;
        }
    } else {
        /* Last match before the anchor, else the last one of the text */
        gint limit = anchor >= 0 ? anchor : hlen;
        gint last = -1;
        for (gint m = folded_find_from(hcv, hlen, ncv, nlen, 0); m >= 0;
             m = folded_find_from(hcv, hlen, ncv, nlen, m + 1)) {
            if (m < limit) match = m;
            last = m;
        }
        if (match < 0 && anchor >= 0 && last >= 0) {
            match = last;
            wrapped = TRUE;
        }
    }

    gint index = 0, total = 0;
    gboolean tagged = app->search->in_viewer && bar_open(app);
    viewer_clear_tags(app);
    if (tagged) viewer_tag_matches(app, hcv, hlen, ncv, nlen, match);
    if (match >= 0) {
        GtkTextIter ms, me;
        gtk_text_buffer_get_iter_at_offset(app->buffer, &ms, match);
        gtk_text_buffer_get_iter_at_offset(app->buffer, &me, match + nlen);
        gtk_text_buffer_select_range(app->buffer, &ms, tagged ? &ms : &me);
        app->search->viewer_match_len = nlen;
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &ms, 0.0, TRUE, 0.0, 0.5);
        app->search->viewer_match = match;
    }
    if (app->search->in_viewer && bar_open(app)) {
        if (app_nav_style(app) == NAV_STYLE_EMACS) {
            total = count_matches(hcv, hlen, ncv, nlen, match, &index);
        }
        report(app, match < 0 ? SEARCH_FAILED : wrapped ? SEARCH_WRAPPED : SEARCH_FOUND, index, total);
    }
    g_free(hcv);
    g_free(ncv);
    g_free(text);
}

/* ------------------------------------------------------------------------- */
/* Opening, closing and keys                                                 */
/* ------------------------------------------------------------------------- */

void show_search(AppData* app, gboolean from_viewer, gboolean backward) {
    /* One bar at a time: the other panel's one closes */
    AppSearchBar* other = from_viewer ? &app->search->explorer_bar : &app->search->viewer_bar;
    if (other->bar != NULL) {
        gtk_widget_set_visible(other->bar, FALSE);
    }
    app->search->in_viewer = from_viewer;
    app->search->backward = backward;
    /* Where C-g (Emacs) goes back to */
    ExplorerRow* selected = tree_nav_selected(app);
    app->search->origin_row = selected != NULL ? (gint)explorer_tree_position(app->tree, selected) : -1;
    GtkAdjustment* vadj = viewer_vadj(app);
    app->search->origin_scroll = vadj != NULL ? gtk_adjustment_get_value(vadj) : 0.0;
    AppSearchBar* sb = active_bar(app);
    style_bar(app, sb);
    gtk_widget_set_visible(sb->bar, TRUE);
    gtk_widget_grab_focus(sb->entry);
    /* The last text stays selected: typing starts a new search, Emacs C-s /
     * Notepad F3 search it again */
    gtk_editable_select_region(GTK_EDITABLE(sb->entry), 0, -1);
}

void hide_search(AppData* app) {
    gtk_widget_set_visible(active_bar(app)->bar, FALSE);
    if (app->search->in_viewer && app->buffer != NULL) {
        viewer_clear_tags(app);
        /* Select the match the bar left (cancel_search resets it) */
        GtkTextIter s, e;
        gtk_text_buffer_get_end_iter(app->buffer, &e);
        if (app->search->viewer_match >= 0 && app->search->viewer_match + app->search->viewer_match_len <= gtk_text_iter_get_offset(&e)) {
            gtk_text_buffer_get_iter_at_offset(app->buffer, &s, app->search->viewer_match);
            gtk_text_buffer_get_iter_at_offset(app->buffer, &e, app->search->viewer_match + app->search->viewer_match_len);
            gtk_text_buffer_select_range(app->buffer, &s, &e);
        }
    }
    /* Return to the panel that opened the bar */
    gtk_widget_grab_focus(app->search->in_viewer ? app->text_view
                                                : GTK_WIDGET(app->tree_view));
}

/* Next / previous match from the bar or the panel that opened it */
void search_again(AppData* app, gint direction) {
    if (app->search->in_viewer) {
        do_viewer_search(app, direction);
    } else {
        do_search(app, direction);
    }
}

void search_mode_changed(AppData* app) {
    if (bar_open(app)) {
        hide_search(app);
    }
}

void search_clear_explorer(AppData* app) {
    g_free(app->search->term);
    app->search->term = NULL;
    if (app->search->explorer_bar.entry != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(app->search->explorer_bar.entry), "");
    }
}

/* Close the bar and go back where the search started */
static void cancel_search(AppData* app) {
    if (app->search->in_viewer) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(app->buffer, &it, gtk_text_buffer_get_insert(app->buffer));
        gtk_text_buffer_select_range(app->buffer, &it, &it);
        GtkAdjustment* vadj = viewer_vadj(app);
        if (vadj != NULL) {
            gtk_adjustment_set_value(vadj, app->search->origin_scroll);
        }
        app->search->viewer_match = -1;
    } else if (app->search->origin_row >= 0 &&
               (guint)app->search->origin_row < explorer_tree_n_visible(app->tree)) {
        select_row(app, explorer_tree_visible_row(app->tree, (guint)app->search->origin_row));
    }
    hide_search(app);
}

static void on_search_changed(GtkEditable* entry, gpointer data) {
    AppData* app = (AppData*)data;
    if (!bar_open(app) || active_bar(app)->entry != GTK_WIDGET(entry)) {
        return; /* text set by the program (cleared on directory change) */
    }
    g_free(app->search->term);
    app->search->term = g_strdup(gtk_editable_get_text(entry));
    gint direction = app->search->backward ? -1 : +1;
    if (app->search->term[0] == '\0') {
        report(app, SEARCH_FOUND, 0, 0);
    } else if (app->search->in_viewer) {
        do_viewer_search(app, direction);
    } else if (row_matches(app, tree_nav_selected(app))) {
        /* The selected row stays while it still matches the longer text */
        report_rows(app, SEARCH_FOUND);
    } else {
        do_search(app, direction);
    }
}

/* Keys of the global mode (nav_keys.h): Emacs C-s / C-r / C-g, Notepad
 * Enter / F3 / Shift+F3; Escape closes the bar in every mode */
static gboolean on_search_key_press(GtkEventControllerKey* controller, guint keyval,
                                    guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    switch (nav_keys_search_lookup(app_nav_style(app), keyval, state)) {
    case NAV_SEARCH_KEY_NEXT:
        search_again(app, +1);
        return TRUE;
    case NAV_SEARCH_KEY_PREV:
        search_again(app, -1);
        return TRUE;
    case NAV_SEARCH_KEY_ACCEPT:
        hide_search(app);
        return TRUE;
    case NAV_SEARCH_KEY_CANCEL:
        cancel_search(app);
        return TRUE;
    default:
        return FALSE;
    }
}

static void on_search_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    hide_search((AppData*)data);
}

static void on_find_next_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    search_again(app, +1);
    gtk_widget_grab_focus(active_bar(app)->entry);
}

static void on_find_prev_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    search_again(app, -1);
    gtk_widget_grab_focus(active_bar(app)->entry);
}

static void on_close_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    hide_search((AppData*)data);
}

/* A Notepad bar button whose label shrinks with the panel */
static GtkWidget* bar_button_new(const gchar* text, const gchar* tooltip) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), label);
    gtk_widget_set_tooltip_text(btn, tooltip);
    return btn;
}

GtkWidget* search_bar_new(AppData* app, gboolean in_viewer) {
    AppSearchBar* sb = in_viewer ? &app->search->viewer_bar : &app->search->explorer_bar;
    editor_modes_load_css();

    sb->bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_visible(sb->bar, FALSE);
    /* The prompt's margin (style_bar()) sets the gap before the entry */
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_set_data(G_OBJECT(sb->bar), "search-row", row);
    gtk_box_append(GTK_BOX(sb->bar), row);

    sb->prompt = gtk_label_new(_("Find:"));
    sb->entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(sb->entry), 5);
    gtk_widget_set_hexpand(sb->entry, TRUE);
    gtk_widget_set_margin_end(sb->entry, 6);

    sb->echo = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(sb->echo), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(sb->echo), 0.0f);
    gtk_widget_set_visible(sb->echo, FALSE);

    sb->buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* next = bar_button_new(_("Find Next"), _("Find Next (Enter / F3)"));
    GtkWidget* prev = bar_button_new(_("Find Prev"), _("Find Previous (Shift+Enter / Shift+F3)"));
    GtkWidget* close = icon_button_new("window-close", _("Close Find Bar (Esc)"));
    g_signal_connect(next, "clicked", G_CALLBACK(on_find_next_clicked), app);
    g_signal_connect(prev, "clicked", G_CALLBACK(on_find_prev_clicked), app);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), app);
    gtk_box_append(GTK_BOX(sb->buttons), next);
    gtk_box_append(GTK_BOX(sb->buttons), prev);
    gtk_box_append(GTK_BOX(sb->buttons), close);

    gtk_box_append(GTK_BOX(row), sb->prompt);
    gtk_box_append(GTK_BOX(row), sb->entry);
    gtk_box_append(GTK_BOX(row), sb->echo);
    gtk_box_append(GTK_BOX(row), sb->buttons);

    g_signal_connect(sb->entry, "changed", G_CALLBACK(on_search_changed), app);
    g_signal_connect(sb->entry, "activate", G_CALLBACK(on_search_activate), app);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_search_key_press), app);
    gtk_widget_add_controller(sb->entry, keys);
    return sb->bar;
}

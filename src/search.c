/* Search bars of the explorer and the preview. Each panel has its own bar, at
 * its bottom, drawn like the search of an editor tab in the global mode:
 * Notepad find bar (Find Next / Find Prev), Emacs I-search minibuffer (match
 * count, Failing / Wrapped prompt), Vim "/" command line (E486 message). */
#include "search.h"
#include "tree_nav.h"
#include "panels.h"
#include "icons.h"
#include "editor_modes.h"

#define SEARCH_MAX_COUNT 1000 /* matches counted for the Emacs [n/total] */

typedef enum {
    SEARCH_FOUND,
    SEARCH_WRAPPED, /* found after going around the end */
    SEARCH_FAILED
} SearchResult;

static AppSearchBar* active_bar(AppData* app) {
    return app->search_in_viewer ? &app->viewer_search : &app->explorer_search;
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

/* Emacs prompt: "I-search:", "Failing wrapped I-search backward:"... */
static void set_emacs_prompt(AppData* app, AppSearchBar* sb, gboolean failing, gboolean wrapped) {
    gchar* prompt = g_strdup_printf("%s%sI-search%s:", failing ? "Failing " : "",
                                    wrapped ? (failing ? "wrapped " : "Wrapped ") : "",
                                    app->search_backward ? " backward" : "");
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
        gtk_label_set_text(GTK_LABEL(sb->prompt), app->search_backward ? "?" : "/");
        break;
    default:
        gtk_widget_add_css_class(sb->bar, "notepad-bar");
        gtk_label_set_text(GTK_LABEL(sb->prompt), "Find:");
        break;
    }
}

/* Show the outcome of a search the way the mode's editor does */
static void report(AppData* app, SearchResult result, gint index, gint total) {
    if (!bar_open(app)) {
        return;
    }
    AppSearchBar* sb = active_bar(app);
    const gchar* term = app->search_term != NULL ? app->search_term : "";
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
            set_echo(sb, app->search_backward ? "search hit TOP, continuing at BOTTOM"
                                              : "search hit BOTTOM, continuing at TOP",
                     "vim-message-error");
        } else {
            set_echo(sb, NULL, NULL);
        }
        break;
    default:
        if (!empty && result == SEARCH_FAILED) {
            msg = g_strdup_printf("Cannot find \"%s\"", term);
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
    if (row == NULL || app->search_term == NULL || app->search_term[0] == '\0') {
        return FALSE;
    }
    gchar* hay = g_utf8_casefold(row->name, -1);
    gchar* needle = g_utf8_casefold(app->search_term, -1);
    gboolean res = (strstr(hay, needle) != NULL);
    g_free(hay);
    g_free(needle);
    return res;
}

/* Matching visible rows, and the 1-based index of the selected one */
static void report_rows(AppData* app, SearchResult result) {
    if (app->search_in_viewer || !bar_open(app)) {
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
    if (app->search_term == NULL || app->search_term[0] == '\0') {
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

void do_viewer_search(AppData* app, gint direction) {
    if (app->search_term == NULL || app->search_term[0] == '\0') {
        return;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(app->buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(app->buffer, &start, &end, FALSE);
    if (text == NULL || text[0] == '\0') {
        g_free(text);
        if (app->search_in_viewer) report(app, SEARCH_FAILED, 0, 0);
        return;
    }

    gint hlen = 0, nlen = 0;
    gunichar* hcv = fold_chars(text, &hlen);
    gunichar* ncv = fold_chars(app->search_term, &nlen);
    gint anchor = (app->viewer_match >= 0 && app->viewer_match < hlen) ? app->viewer_match : -1;

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
    if (match >= 0) {
        GtkTextIter ms, me;
        gtk_text_buffer_get_iter_at_offset(app->buffer, &ms, match);
        gtk_text_buffer_get_iter_at_offset(app->buffer, &me, match + nlen);
        gtk_text_buffer_select_range(app->buffer, &ms, &me);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &ms, 0.0, TRUE, 0.0, 0.5);
        app->viewer_match = match;
    }
    if (app->search_in_viewer && bar_open(app)) {
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
    AppSearchBar* other = from_viewer ? &app->explorer_search : &app->viewer_search;
    if (other->bar != NULL) {
        gtk_widget_set_visible(other->bar, FALSE);
    }
    app->search_in_viewer = from_viewer;
    app->search_backward = backward;
    /* Where C-g (Emacs) goes back to */
    ExplorerRow* selected = tree_nav_selected(app);
    app->search_origin_row = selected != NULL ? (gint)explorer_tree_position(app->tree, selected) : -1;
    app->search_origin_scroll = app->viewer_vadj != NULL ? gtk_adjustment_get_value(app->viewer_vadj) : 0.0;
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
    /* Return to the panel that opened the bar */
    gtk_widget_grab_focus(app->search_in_viewer ? app->text_view
                                                : GTK_WIDGET(app->tree_view));
}

/* Next / previous match from the bar or the panel that opened it */
void search_again(AppData* app, gint direction) {
    if (app->search_in_viewer) {
        do_viewer_search(app, direction);
    } else {
        do_search(app, direction);
    }
}

void search_clear_explorer(AppData* app) {
    g_free(app->search_term);
    app->search_term = NULL;
    if (app->explorer_search.entry != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(app->explorer_search.entry), "");
    }
}

/* Close the bar and go back where the search started */
static void cancel_search(AppData* app) {
    if (app->search_in_viewer) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(app->buffer, &it, gtk_text_buffer_get_insert(app->buffer));
        gtk_text_buffer_select_range(app->buffer, &it, &it);
        if (app->viewer_vadj != NULL) {
            gtk_adjustment_set_value(app->viewer_vadj, app->search_origin_scroll);
        }
        app->viewer_match = -1;
    } else if (app->search_origin_row >= 0 &&
               (guint)app->search_origin_row < explorer_tree_n_visible(app->tree)) {
        select_row(app, explorer_tree_visible_row(app->tree, (guint)app->search_origin_row));
    }
    hide_search(app);
}

static void on_search_changed(GtkEditable* entry, gpointer data) {
    AppData* app = (AppData*)data;
    if (!bar_open(app) || active_bar(app)->entry != GTK_WIDGET(entry)) {
        return; /* text set by the program (cleared on directory change) */
    }
    g_free(app->search_term);
    app->search_term = g_strdup(gtk_editable_get_text(entry));
    gint direction = app->search_backward ? -1 : +1;
    if (app->search_term[0] == '\0') {
        report(app, SEARCH_FOUND, 0, 0);
    } else if (app->search_in_viewer) {
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

GtkWidget* search_bar_new(AppData* app, AppSearchBar* sb) {
    editor_modes_load_css();

    sb->bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_visible(sb->bar, FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_object_set_data(G_OBJECT(sb->bar), "search-row", row);
    gtk_box_append(GTK_BOX(sb->bar), row);

    sb->prompt = gtk_label_new("Find:");
    sb->entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(sb->entry), 5);
    gtk_widget_set_hexpand(sb->entry, TRUE);

    sb->echo = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(sb->echo), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(sb->echo), 0.0f);
    gtk_widget_set_visible(sb->echo, FALSE);

    sb->buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* next = bar_button_new("Find Next", "Find Next (Enter / F3)");
    GtkWidget* prev = bar_button_new("Find Prev", "Find Previous (Shift+Enter / Shift+F3)");
    GtkWidget* close = icon_button_new("window-close", "Close Find Bar (Esc)");
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

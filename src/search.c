#include "search.h"
#include "tree_nav.h"
#include "file_list.h"

static gboolean path_matches(AppData* app, GtkTreePath* path) {
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(app->model, &it, path)) {
        return FALSE;
    }
    gchar* name = NULL;
    gtk_tree_model_get(app->model, &it, COL_NAME, &name, -1);
    gboolean res = FALSE;
    if (name != NULL && app->search_term != NULL) {
        gchar* hay = g_utf8_casefold(name, -1);
        gchar* needle = g_utf8_casefold(app->search_term, -1);
        res = (hay != NULL && needle != NULL && strstr(hay, needle) != NULL);
        g_free(hay);
        g_free(needle);
    }
    g_free(name);
    return res;
}

void do_search(AppData* app, gint direction) {
    if (app->search_term == NULL || app->search_term[0] == '\0') {
        return;
    }
    GPtrArray* paths = g_ptr_array_new_with_free_func((GDestroyNotify)gtk_tree_path_free);
    GtkTreePath* p = get_first_visible_path(app->tree_view);
    while (p != NULL) {
        g_ptr_array_add(paths, p);
        p = get_next_visible_path(app->tree_view, p);
    }
    if (paths->len == 0) {
        g_ptr_array_unref(paths);
        return;
    }

    gint cur = -1;
    GtkTreeIter sel_it;
    if (gtk_tree_selection_get_selected(app->selection, NULL, &sel_it)) {
        GtkTreePath* sel_path = gtk_tree_model_get_path(app->model, &sel_it);
        for (guint i = 0; i < paths->len; i++) {
            if (gtk_tree_path_compare((GtkTreePath*)g_ptr_array_index(paths, i), sel_path) == 0) {
                cur = (gint)i;
                break;
            }
        }
        gtk_tree_path_free(sel_path);
    }

    gint n = (gint)paths->len;
    for (gint k = 1; k <= n; k++) {
        gint idx = (direction > 0) ? ((cur + k) % n + n) % n
                                   : ((cur - k) % n + n) % n;
        GtkTreePath* target = (GtkTreePath*)g_ptr_array_index(paths, idx);
        if (path_matches(app, target)) {
            select_path(app, target);
            break;
        }
    }
    g_ptr_array_unref(paths);
}

void show_search(AppData* app, gboolean from_viewer) {
    app->search_in_viewer = from_viewer;
    gtk_widget_set_visible(app->search_entry, TRUE);
    gtk_widget_grab_focus(app->search_entry);
}

void hide_search(AppData* app) {
    gtk_widget_set_visible(app->search_entry, FALSE);
    /* Return to the panel that opened the bar */
    gtk_widget_grab_focus(app->search_in_viewer ? app->text_view
                                                : GTK_WIDGET(app->tree_view));
}

void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_free(app->search_term);
    app->search_term = g_strdup(text);
    if (app->search_in_viewer) {
        do_viewer_search(app, +1);
    } else {
        do_search(app, +1);
    }
}

void on_search_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    hide_search((AppData*)data);
}

gboolean on_search_key_press(GtkEventControllerKey* controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    if (keyval == GDK_KEY_Escape) {
        hide_search((AppData*)data);
        return TRUE;
    }
    return FALSE;
}

/* Case-insensitive search (char-wise folding) from a char-offset anchor */
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start) {
    const gint hlen = g_utf8_strlen(hay, -1);
    const gint nlen = g_utf8_strlen(needle, -1);
    if (nlen == 0 || hlen == 0 || nlen > hlen) {
        return -1;
    }

    gunichar* hcv = g_new(gunichar, hlen);
    gunichar* ncv = g_new(gunichar, nlen);
    const gchar* p = hay;
    for (gint i = 0; i < hlen; i++) {
        hcv[i] = g_unichar_tolower(g_utf8_get_char(p));
        p = g_utf8_next_char(p);
    }
    p = needle;
    for (gint i = 0; i < nlen; i++) {
        ncv[i] = g_unichar_tolower(g_utf8_get_char(p));
        p = g_utf8_next_char(p);
    }

    gint max_start = hlen - nlen;
    gint match = -1;
    const gint kmax = (start <= max_start) ? max_start : -1;
    for (gint k = start; k <= kmax; k++) {
        if (hcv[k] != ncv[0]) {
            continue;
        }
        gint j = 1;
        while (j < nlen && hcv[k + j] == ncv[j]) {
            j++;
        }
        if (j == nlen) {
            match = k;
            break;
        }
    }
    g_free(hcv);
    g_free(ncv);
    return match;
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
        return;
    }

    const gint hlen = g_utf8_strlen(text, -1);
    gint anchor = ((app->viewer_match >= 0 && app->viewer_match < hlen))
                      ? app->viewer_match : -1;

    gint match = -1;
    if (direction > 0) {
        gint from = (anchor >= 0) ? anchor : 0;
        const gchar* ap = (anchor >= 0)
            ? g_utf8_offset_to_pointer(text, anchor) : text;
        from = g_utf8_pointer_to_offset(text, g_utf8_next_char(ap));
        match = utf8_ci_find_from(text, app->search_term, from);
        if (match < 0 && from > 0) {
            match = utf8_ci_find_from(text, app->search_term, 0);
        }
    } else {
        const gint limit = (anchor >= 0) ? anchor : hlen;
        gint probe = 0;
        while (probe <= limit) {
            gint m = utf8_ci_find_from(text, app->search_term, probe);
            if (m < 0 || m >= limit) {
                break;
            }
            match = m;
            probe = m + 1;
        }
        if (match < 0 && anchor >= 0) {
            gint probe2 = 0;
            for (;;) {
                gint m = utf8_ci_find_from(text, app->search_term, probe2);
                if (m < 0) {
                    break;
                }
                match = m;
                probe2 = m + 1;
            }
        }
    }

    if (match >= 0) {
        gint nlen = g_utf8_strlen(app->search_term, -1);
        GtkTextIter ms, me;
        gtk_text_buffer_get_start_iter(app->buffer, &ms);
        gtk_text_iter_set_offset(&ms, match);
        gtk_text_buffer_get_start_iter(app->buffer, &me);
        gtk_text_iter_set_offset(&me, match + nlen);
        gtk_text_buffer_select_range(app->buffer, &ms, &me);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &ms,
                                     0.0, TRUE, 0.0, 0.5);
        app->viewer_match = match;
    }
    g_free(text);
}

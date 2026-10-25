#include "search.h"
#include "file_list.h"

static gboolean name_matches(AppData* app, gint idx) {
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(app->model, &it, NULL, idx)) {
        return FALSE;
    }
    gchar* name = NULL;
    gtk_tree_model_get(app->model, &it, 0, &name, -1);
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
    gint n = list_count(app);
    if (n <= 0) {
        return;
    }
    gint cur = current_index(app);
    for (gint k = 1; k <= n; k++) {
        gint idx = (direction > 0) ? ((cur + k) % n + n) % n
                                   : ((cur - k) % n + n) % n;
        if (name_matches(app, idx)) {
            select_index(app, idx);
            return;
        }
    }
}

void show_search(AppData* app, gboolean from_viewer) {
    app->search_in_viewer = from_viewer;
    gtk_widget_show(app->search_entry);
    gtk_widget_grab_focus(app->search_entry);
}

void hide_search(AppData* app) {
    gtk_widget_hide(app->search_entry);
    /* Return to the panel that opened the bar */
    gtk_widget_grab_focus(app->search_in_viewer ? app->text_view
                                                : GTK_WIDGET(app->tree_view));
}

void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(entry));
    g_free(app->search_term);
    app->search_term = g_strdup(text);
    if (app->search_in_viewer) {
        do_viewer_search(app, +1);
    } else {
        do_search(app, +1);
    }
}

void on_search_activate(GtkWidget* entry, gpointer data) {
    (void)entry;
    hide_search((AppData*)data);
}

gboolean on_search_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    if (event->keyval == GDK_KEY_Escape) {
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

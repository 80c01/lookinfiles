/* Folding of structured previews (see preview_folding.h). */
#include "preview/preview_folding.h"
#include "preview/preview_state.h"
#include "core/app_events.h"
#include "theme/text_tags.h"

struct PreviewFolding {
    GHashTable* files;   /* path -> PreviewFold* changed by hand */
    gchar* shown_path;   /* file whose blocks are in shown */
    PreviewDoc* shown;   /* fold points of the preview on screen (no text) */
    gchar* focus_path;   /* file of the fold change being built */
    GPtrArray* focus;    /* ids of the blocks at the cursor, innermost first */
    gdouble focus_align; /* height of the cursor line in the view, 0 (top) to 1 */
    GtkTextBuffer* tracked; /* buffer whose cursor is followed */
    gboolean marked;     /* the cursor line and its block are highlighted */
};

static struct PreviewFolding* folding_get(AppData* app) {
    if (app->preview_folding == NULL) {
        app->preview_folding = g_new0(struct PreviewFolding, 1);
        app->preview_folding->files =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)preview_fold_free);
    }
    return app->preview_folding;
}

PreviewFold* preview_folding_state(AppData* app, const gchar* path) {
    if (app == NULL || app->preview_folding == NULL || path == NULL) {
        return NULL;
    }
    return preview_fold_copy(g_hash_table_lookup(app->preview_folding->files, path));
}

/* Put the cursor on the first of the focus blocks still shown */
static void restore_focus(AppData* app, struct PreviewFolding* f) {
    for (guint i = 0; i < f->focus->len; i++) {
        const PreviewFoldPoint* point = preview_doc_fold_find(f->shown, g_ptr_array_index(f->focus, i));
        if (point == NULL) {
            continue;
        }
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, point->start);
        gtk_text_buffer_place_cursor(app->buffer, &iter);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app->text_view), gtk_text_buffer_get_insert(app->buffer),
                                     0.0, TRUE, 0.0, f->focus_align);
        return;
    }
}

/* Highlight the line of the cursor and the block Tab acts on from it */
static void update_marks(AppData* app) {
    struct PreviewFolding* f = app->preview_folding;
    if (f == NULL || (f->shown == NULL && !f->marked)) {
        return;
    }
    GtkTextTag* scope = text_tag_ensure(app->buffer, "fold_scope", "paragraph-background", "@bg_block", NULL);
    GtkTextTag* line = text_tag_ensure(app->buffer, "fold_cursor_line", "paragraph-background", "@bg_hover", NULL);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(app->buffer, &start, &end);
    gtk_text_buffer_remove_tag(app->buffer, scope, &start, &end);
    gtk_text_buffer_remove_tag(app->buffer, line, &start, &end);
    f->marked = FALSE;
    if (f->shown == NULL) {
        return;
    }
    gtk_text_buffer_get_iter_at_mark(app->buffer, &start, gtk_text_buffer_get_insert(app->buffer));
    gtk_text_iter_set_line_offset(&start, 0);
    const PreviewFoldPoint* point = preview_doc_fold_at(f->shown, gtk_text_iter_get_offset(&start));
    if (point != NULL) {
        GtkTextIter first, last;
        gtk_text_buffer_get_iter_at_offset(app->buffer, &first, point->start);
        gtk_text_buffer_get_iter_at_offset(app->buffer, &last, point->end);
        gtk_text_buffer_apply_tag(app->buffer, scope, &first, &last);
    }
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    gtk_text_tag_set_priority(line, gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(app->buffer)) - 1);
    gtk_text_buffer_apply_tag(app->buffer, line, &start, &end);
    f->marked = TRUE;
}

static void on_cursor_moved(GObject* buffer, GParamSpec* pspec, gpointer data) {
    (void)buffer;
    (void)pspec;
    update_marks((AppData*)data);
}

static void untrack(AppData* app, struct PreviewFolding* f) {
    if (f->tracked != NULL) {
        g_signal_handlers_disconnect_by_func(f->tracked, G_CALLBACK(on_cursor_moved), app);
        g_object_remove_weak_pointer(G_OBJECT(f->tracked), (gpointer*)&f->tracked);
        f->tracked = NULL;
    }
}

void preview_folding_shown(AppData* app, const PreviewDoc* doc, const gchar* path) {
    struct PreviewFolding* f = folding_get(app);
    g_clear_pointer(&f->shown, preview_doc_free);
    g_clear_pointer(&f->shown_path, g_free);
    if (doc == NULL || doc->folds->len == 0 || path == NULL) {
        if (app->buffer != NULL) {
            update_marks(app);
        }
        return;
    }
    f->shown_path = g_strdup(path);
    f->shown = preview_doc_new();
    for (guint i = 0; i < doc->folds->len; i++) {
        const PreviewFoldPoint* point = &g_array_index(doc->folds, PreviewFoldPoint, i);
        preview_doc_add_fold(f->shown, point->start, point->end, point->id, point->folded);
    }
    if (f->focus != NULL && g_strcmp0(f->focus_path, f->shown_path) == 0) {
        restore_focus(app, f);
    }
    g_clear_pointer(&f->focus, g_ptr_array_unref);
    g_clear_pointer(&f->focus_path, g_free);
    if (f->tracked != app->buffer) {
        untrack(app, f);
        f->tracked = app->buffer;
        g_object_add_weak_pointer(G_OBJECT(f->tracked), (gpointer*)&f->tracked);
        g_signal_connect(app->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), app);
    }
    update_marks(app);
}

gboolean preview_folding_restoring(AppData* app, const gchar* path) {
    struct PreviewFolding* f = app != NULL ? app->preview_folding : NULL;
    return f != NULL && f->focus != NULL && g_strcmp0(f->focus_path, path) == 0;
}

/* Remember the blocks at the cursor and its height, for after the rebuild */
static void keep_focus(AppData* app, struct PreviewFolding* f, gint offset) {
    g_clear_pointer(&f->focus, g_ptr_array_unref);
    f->focus = g_ptr_array_new_with_free_func(g_free);
    for (const PreviewFoldPoint* point = preview_doc_fold_at(f->shown, offset); point != NULL;) {
        g_ptr_array_add(f->focus, g_strdup(point->id));
        /* The next enclosing block starts before this one */
        const PreviewFoldPoint* outer = NULL;
        for (guint i = 0; i < f->shown->folds->len; i++) {
            const PreviewFoldPoint* p = &g_array_index(f->shown->folds, PreviewFoldPoint, i);
            if (p->start < point->start && p->end >= point->end && (outer == NULL || p->start > outer->start)) {
                outer = p;
            }
        }
        point = outer;
    }
    g_free(f->focus_path);
    f->focus_path = g_strdup(f->shown_path);

    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, offset);
    GdkRectangle visible, line;
    gtk_text_view_get_visible_rect(view, &visible);
    gtk_text_view_get_iter_location(view, &iter, &line);
    f->focus_align = visible.height > 0 ? CLAMP((gdouble)(line.y - visible.y) / visible.height, 0.0, 1.0) : 0.0;
}

/* Shift+Tab: outer members only, two levels, everything open */
static gint next_level(gint level) {
    switch (level) {
    case 1: return 2;
    case 2: return PREVIEW_FOLD_OPEN;
    default: return 1;
    }
}

/* The fold state of the file shown, created on the first change */
static PreviewFold* shown_fold(struct PreviewFolding* f) {
    PreviewFold* fold = g_hash_table_lookup(f->files, f->shown_path);
    if (fold == NULL) {
        fold = preview_fold_new(PREVIEW_FOLD_AUTO);
        g_hash_table_insert(f->files, g_strdup(f->shown_path), fold);
    }
    return fold;
}

/* The fold points of the preview on screen, or NULL */
static struct PreviewFolding* shown_folding(AppData* app) {
    struct PreviewFolding* f = app != NULL ? app->preview_folding : NULL;
    if (f == NULL || f->shown == NULL || g_strcmp0(f->shown_path, preview_state_path(app)) != 0) {
        return NULL;
    }
    return f;
}

/* Fold or open point, keeping the cursor on it */
static void toggle_point(AppData* app, struct PreviewFolding* f, const PreviewFoldPoint* point) {
    preview_fold_toggle(shown_fold(f), point->id);
    keep_focus(app, f, point->start);
    app_emit(app, APP_EVENT_PREVIEW_STALE);
}

gboolean preview_folding_click(AppData* app, gdouble x, gdouble y) {
    struct PreviewFolding* f = shown_folding(app);
    if (f == NULL) {
        return FALSE;
    }
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    gint bx, by;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y, &bx, &by);
    GtkTextIter iter;
    if (!gtk_text_view_get_iter_at_location(view, &iter, bx, by)) {
        return FALSE;
    }
    gunichar c = gtk_text_iter_get_char(&iter);
    if (c != g_utf8_get_char(PREVIEW_FOLD_MARK_OPEN) && c != g_utf8_get_char(PREVIEW_FOLD_MARK_CLOSED)) {
        return FALSE;
    }
    gtk_text_iter_set_line_offset(&iter, 0);
    gint offset = gtk_text_iter_get_offset(&iter);
    const PreviewFoldPoint* point = preview_doc_fold_at(f->shown, offset);
    if (point == NULL || point->start != offset) {
        return FALSE; /* a twisty in the text, not on the first line of a block */
    }
    toggle_point(app, f, point);
    return TRUE;
}

gboolean preview_folding_key(AppData* app, guint keyval, GdkModifierType state) {
    struct PreviewFolding* f = shown_folding(app);
    if (f == NULL || (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) {
        return FALSE;
    }
    gboolean back = keyval == GDK_KEY_ISO_Left_Tab ||
                    ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_KP_Tab) && (state & GDK_SHIFT_MASK));
    if (!back && keyval != GDK_KEY_Tab && keyval != GDK_KEY_KP_Tab) {
        return FALSE;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, gtk_text_buffer_get_insert(app->buffer));
    gtk_text_iter_set_line_offset(&iter, 0); /* blocks start at the start of their line */
    gint offset = gtk_text_iter_get_offset(&iter);
    const PreviewFoldPoint* point = preview_doc_fold_at(f->shown, offset);
    if (!back && point == NULL) {
        return TRUE; /* not in a block (the header line) */
    }
    if (!back) {
        toggle_point(app, f, point);
        return TRUE;
    }
    PreviewFold* fold = shown_fold(f);
    fold->level = next_level(fold->level);
    g_hash_table_remove_all(fold->toggled);
    keep_focus(app, f, offset);
    app_emit(app, APP_EVENT_PREVIEW_STALE);
    return TRUE;
}

void preview_folding_free(AppData* app) {
    struct PreviewFolding* f = app != NULL ? app->preview_folding : NULL;
    if (f == NULL) {
        return;
    }
    untrack(app, f);
    g_hash_table_destroy(f->files);
    preview_doc_free(f->shown);
    g_free(f->shown_path);
    g_free(f->focus_path);
    if (f->focus != NULL) {
        g_ptr_array_unref(f->focus);
    }
    g_clear_pointer(&app->preview_folding, g_free);
}

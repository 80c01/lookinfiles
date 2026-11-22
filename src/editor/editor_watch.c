/* External file change detection and safe reload for open editor tabs
 * (see editor_watch.h). */
#include "editor/editor_watch.h"
#include "editor/editor_stamp.h"
#include "editor/editor_remote.h"
#include "core/vfs.h"
#include "core/vfs_watch.h"
#include "core/app_events.h"
#include "core/file_types.h"
#include "core/i18n.h"
#include "core/text_encoding.h"
#include "widgets/dialog.h"
#include "widgets/diff_gutter.h"
#include "preview/diff_tab.h"

#include <string.h>

#define WATCH_MAX_BYTES   (2 * 1024 * 1024) /* matches EDIT_MAX_BYTES */
#define WATCH_DEBOUNCE_MS 150
#define BUFFER_WATCH_KEY  "editor-buffer-watch"
#define BANNER_KEY        "editor-watch-banner"

/* App-wide debounce state (app->editor_watch) */
struct EditorWatch {
    guint check_source; /* pending debounced check timeout, 0 = idle */
    gboolean checking;  /* re-entrancy guard */
};

/* Per-buffer watch state (attached to GtkTextBuffer via g_object_set_data_full) */
typedef struct {
    AppData* app;
    EditorStamp stamp;          /* last synced disk metadata */
    EditorStamp notified_stamp; /* last disk metadata already reported to Vim/Emacs */
    gchar* monitored_path;    /* path currently monitored by monitor */
    GFileMonitor* monitor;    /* per-file monitor (catches unexpanded folders) */
    VfsWatch* remote;         /* a remote machine's file: polled over SFTP instead */
    gboolean conflict;        /* disk differs from stamp */
    gboolean removed;         /* file missing on disk */
    gboolean reloading;       /* TRUE during programmatic reload */
    gboolean prompt_open;     /* re-entrancy guard for save confirmation dialog */
} BufferWatch;

static EditorWatchHooks watch_hooks = {NULL, NULL};

void editor_watch_set_hooks(const EditorWatchHooks* hooks) {
    if (hooks != NULL) {
        watch_hooks = *hooks;
    } else {
        memset(&watch_hooks, 0, sizeof(watch_hooks));
    }
}

static struct EditorWatch* watch_state(AppData* app) {
    if (app == NULL) return NULL;
    if (app->editor_watch == NULL) {
        app->editor_watch = g_new0(struct EditorWatch, 1);
    }
    return app->editor_watch;
}

static void cancel_monitor(BufferWatch* bw) {
    g_clear_pointer(&bw->remote, vfs_watch_free);
    if (bw->monitor != NULL) {
        g_file_monitor_cancel(bw->monitor);
        g_object_unref(bw->monitor);
        bw->monitor = NULL;
    }
    g_clear_pointer(&bw->monitored_path, g_free);
}

static void buffer_watch_free(gpointer data) {
    BufferWatch* bw = data;
    if (bw == NULL) return;
    cancel_monitor(bw);
    g_free(bw);
}

static BufferWatch* buffer_watch_get(GtkTextBuffer* buffer) {
    if (buffer == NULL) return NULL;
    return g_object_get_data(G_OBJECT(buffer), BUFFER_WATCH_KEY);
}

static BufferWatch* buffer_watch_ensure(AppData* app, GtkTextBuffer* buffer) {
    if (buffer == NULL) return NULL;
    BufferWatch* bw = buffer_watch_get(buffer);
    if (bw == NULL) {
        bw = g_new0(BufferWatch, 1);
        bw->app = app;
        g_object_set_data_full(G_OBJECT(buffer), BUFFER_WATCH_KEY, bw, buffer_watch_free);
    } else if (app != NULL) {
        bw->app = app;
    }
    return bw;
}

static gboolean debounced_check_cb(gpointer data) {
    AppData* app = data;
    struct EditorWatch* ew = watch_state(app);
    if (ew != NULL) ew->check_source = 0;
    editor_watch_check_all(app);
    return G_SOURCE_REMOVE;
}

static void schedule_check(AppData* app) {
    struct EditorWatch* ew = watch_state(app);
    if (ew == NULL) return;
    if (ew->check_source == 0) {
        ew->check_source = g_timeout_add(WATCH_DEBOUNCE_MS, debounced_check_cb, app);
    }
}

static void on_file_monitor_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                    GFileMonitorEvent event_type, gpointer data) {
    (void)monitor;
    (void)file;
    (void)other_file;
    BufferWatch* bw = data;
    if (bw == NULL || bw->app == NULL) return;
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED:
        schedule_check(bw->app);
        break;
    default:
        break;
    }
}

static void on_remote_changed(gpointer data) {
    BufferWatch* bw = data;
    if (bw->app != NULL) schedule_check(bw->app);
}

static void rearm_monitor(BufferWatch* bw, const gchar* path) {
    cancel_monitor(bw);
    if (path == NULL || path[0] == '\0') return;
    if (vfs_is_remote(path)) { /* no file monitor sees a remote change */
        bw->remote = vfs_watch_new(path, on_remote_changed, bw);
        bw->monitored_path = g_strdup(path);
        return;
    }
    GFile* file = g_file_new_for_path(path);
    GFileMonitor* mon = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(file);
    if (mon != NULL) {
        g_signal_connect(mon, "changed", G_CALLBACK(on_file_monitor_changed), bw);
        bw->monitor = mon;
        bw->monitored_path = g_strdup(path);
    }
}

/* ------------------------------------------------------------------------- */
/* Notepad conflict banner                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GtkWidget* bar;
    GtkWidget* label;
    GtkWidget* compare_btn;
    GtkWidget* reload_btn;
    GtkWidget* keep_btn;
    EditorTab* tab;
} WatchBanner;

static void sync_banners_for_buffer(AppData* app, GtkTextBuffer* buffer);

static void dismiss_conflict(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    if (bw == NULL) return;
    EditorStamp cur;
    editor_stamp_read(tab->path, &cur);
    bw->stamp = cur;
    memset(&bw->notified_stamp, 0, sizeof(bw->notified_stamp));
    bw->conflict = FALSE;
    bw->removed = FALSE;
    rearm_monitor(bw, tab->path);
    sync_banners_for_buffer(tab->app, tab->buffer);
    if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
}

static void on_banner_compare_clicked(GtkButton* button, gpointer data) {
    (void)button;
    WatchBanner* wb = data;
    if (wb == NULL || wb->tab == NULL || wb->tab->app == NULL ||
        wb->tab->path == NULL || wb->tab->buffer == NULL) return;
    EditorTab* tab = wb->tab;
    gchar* disk = NULL;
    gsize dlen = 0;
    if (!editor_remote_get_contents(tab->path, &disk, &dlen, NULL)) return;
    if (!g_utf8_validate(disk, (gssize)dlen, NULL)) {
        gchar* utf8 = text_encoding_to_utf8(disk, dlen, NULL);
        g_free(disk);
        disk = utf8;
    }
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    gchar* buf_txt = gtk_text_buffer_get_text(tab->buffer, &s, &e, TRUE);
    const gchar* name = tab->name != NULL ? tab->name : tab->path;
    gchar* title = g_strdup_printf(_("%s (Disk ↔ Buffer)"), name);
    gchar* llabel = g_strdup_printf(_("%s (On Disk)"), name);
    gchar* rlabel = g_strdup_printf(_("%s (Current Buffer)"), name);
    diff_tab_open_texts(tab->app, title, llabel, tab->path, disk, rlabel, tab->path, buf_txt, name);
    g_free(rlabel);
    g_free(llabel);
    g_free(title);
    g_free(buf_txt);
    g_free(disk);
}

static void on_banner_reload_clicked(GtkButton* button, gpointer data) {
    (void)button;
    WatchBanner* wb = data;
    if (wb == NULL || wb->tab == NULL || wb->tab->app == NULL) return;
    gchar* err = NULL;
    if (!editor_watch_reload_tab(wb->tab->app, wb->tab, TRUE, &err) && err != NULL) {
        app_notice(wb->tab->app, err, FALSE);
    }
    g_free(err);
    if (wb->tab->view != NULL) {
        gtk_widget_grab_focus(wb->tab->view);
    }
}

static void on_banner_keep_clicked(GtkButton* button, gpointer data) {
    (void)button;
    WatchBanner* wb = data;
    if (wb == NULL || wb->tab == NULL) return;
    dismiss_conflict(wb->tab);
}

static WatchBanner* ensure_banner(EditorTab* tab) {
    if (tab == NULL || tab->container == NULL) return NULL;
    WatchBanner* wb = g_object_get_data(G_OBJECT(tab->container), BANNER_KEY);
    if (wb != NULL) return wb;

    wb = g_new0(WatchBanner, 1);
    wb->tab = tab;
    wb->bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(wb->bar, "editor-watch-bar");

    wb->label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(wb->label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(wb->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(wb->label, TRUE);
    gtk_box_append(GTK_BOX(wb->bar), wb->label);

    wb->compare_btn = gtk_button_new_with_label(_("Compare"));
    gtk_widget_set_focusable(wb->compare_btn, FALSE);
    gtk_widget_set_tooltip_text(wb->compare_btn, _("Compare File on Disk with Current Buffer"));
    g_signal_connect(wb->compare_btn, "clicked", G_CALLBACK(on_banner_compare_clicked), wb);
    gtk_box_append(GTK_BOX(wb->bar), wb->compare_btn);

    wb->reload_btn = gtk_button_new_with_label(_("Reload"));
    gtk_widget_set_focusable(wb->reload_btn, FALSE);
    gtk_widget_set_tooltip_text(wb->reload_btn, _("Reload File from Disk"));
    g_signal_connect(wb->reload_btn, "clicked", G_CALLBACK(on_banner_reload_clicked), wb);
    gtk_box_append(GTK_BOX(wb->bar), wb->reload_btn);

    wb->keep_btn = gtk_button_new_with_label(_("Keep Mine"));
    gtk_widget_set_focusable(wb->keep_btn, FALSE);
    gtk_widget_set_tooltip_text(wb->keep_btn, _("Keep Current Buffer and Dismiss"));
    g_signal_connect(wb->keep_btn, "clicked", G_CALLBACK(on_banner_keep_clicked), wb);
    gtk_box_append(GTK_BOX(wb->bar), wb->keep_btn);

    gtk_widget_set_visible(wb->bar, FALSE);
    gtk_box_prepend(GTK_BOX(tab->container), wb->bar);
    g_object_set_data_full(G_OBJECT(tab->container), BANNER_KEY, wb, g_free);
    return wb;
}

void editor_watch_sync_view(EditorTab* tab) {
    if (tab == NULL || tab->container == NULL || tab->buffer == NULL) return;
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    /* Purist per-mode: the top banner is shown only in Notepad mode */
    gboolean show = (bw != NULL && bw->conflict && tab->mode == EDITOR_MODE_NOTEPAD);
    WatchBanner* wb = g_object_get_data(G_OBJECT(tab->container), BANNER_KEY);
    if (!show) {
        if (wb != NULL) {
            gtk_widget_set_visible(wb->bar, FALSE);
        }
        return;
    }
    wb = ensure_banner(tab);
    if (wb == NULL) return;
    if (bw->removed) {
        gtk_label_set_text(GTK_LABEL(wb->label), _("This file was removed from disk."));
        gtk_widget_set_visible(wb->compare_btn, FALSE);
        gtk_widget_set_visible(wb->reload_btn, FALSE);
        gtk_button_set_label(GTK_BUTTON(wb->keep_btn), _("Keep"));
    } else {
        gtk_label_set_text(GTK_LABEL(wb->label), _("This file changed on disk."));
        gtk_widget_set_visible(wb->compare_btn, TRUE);
        gtk_widget_set_visible(wb->reload_btn, TRUE);
        gtk_button_set_label(GTK_BUTTON(wb->keep_btn), _("Keep Mine"));
    }
    gtk_widget_set_visible(wb->bar, TRUE);
}

static void sync_banners_for_buffer(AppData* app, GtkTextBuffer* buffer) {
    if (app == NULL || buffer == NULL) return;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == buffer) {
            editor_watch_sync_view(t);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static gboolean mode_auto_reload(EditorTab* tab) {
    if (tab == NULL) return FALSE;
    if (watch_hooks.auto_reload != NULL) {
        return watch_hooks.auto_reload(tab);
    }
    return tab->mode == EDITOR_MODE_NOTEPAD;
}

static void mode_notify(EditorTab* tab, const gchar* msg, gboolean is_error) {
    if (tab == NULL || msg == NULL) return;
    if (watch_hooks.notify != NULL) {
        watch_hooks.notify(tab, msg, is_error);
        return;
    }
    if (tab->app != NULL) {
        app_notice(tab->app, msg, FALSE);
    }
}

/* TRUE when the raw bytes on disk equal the current buffer text + final_break
 * (e.g. after `touch` or an identical rewrite). */
static gboolean disk_matches_buffer(EditorTab* tab, const EditorStamp* cur) {
    if (tab == NULL || tab->path == NULL || tab->buffer == NULL || cur == NULL || !cur->exists) {
        return FALSE;
    }
    if (cur->file.size > WATCH_MAX_BYTES) {
        return FALSE;
    }
    gchar* disk = NULL;
    gsize disk_len = 0;
    if (!editor_remote_get_contents(tab->path, &disk, &disk_len, NULL)) {
        return FALSE;
    }
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    gchar* lines = gtk_text_buffer_get_text(tab->buffer, &s, &e, TRUE);
    const gchar* fb = tab->final_break != NULL ? tab->final_break : "";
    gsize lines_len = strlen(lines);
    gsize fb_len = strlen(fb);
    gboolean same = (disk_len == lines_len + fb_len &&
                     memcmp(disk, lines, lines_len) == 0 &&
                     memcmp(disk + lines_len, fb, fb_len) == 0);
    g_free(lines);
    g_free(disk);
    return same;
}

/* ------------------------------------------------------------------------- */
/* Recording, reloading and checking                                         */
/* ------------------------------------------------------------------------- */

void editor_watch_record(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL || tab->path == NULL || tab->path[0] == '\0') {
        return;
    }
    BufferWatch* bw = buffer_watch_ensure(tab->app, tab->buffer);
    EditorStamp cur;
    vfs_watch_forget(tab->path); /* a remote file: read now, not the reading before a save */
    editor_stamp_read(tab->path, &cur);
    gboolean need_rearm = ((bw->monitor == NULL && bw->remote == NULL) ||
                           g_strcmp0(bw->monitored_path, tab->path) != 0 ||
                           cur.file.inode != bw->stamp.file.inode);
    bw->stamp = cur;
    memset(&bw->notified_stamp, 0, sizeof(bw->notified_stamp));
    bw->conflict = FALSE;
    bw->removed = FALSE;
    if (need_rearm) {
        rearm_monitor(bw, tab->path);
    }
    sync_banners_for_buffer(tab->app, tab->buffer);
}

void editor_watch_retarget(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    if (bw != NULL) {
        cancel_monitor(bw);
    }
    editor_watch_record(tab);
}

gboolean editor_watch_disk_changed(EditorTab* tab, gboolean* removed_out) {
    if (removed_out != NULL) *removed_out = FALSE;
    if (tab == NULL || tab->buffer == NULL || tab->path == NULL || tab->path[0] == '\0') {
        return FALSE;
    }
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    if (bw == NULL || !bw->stamp.valid) {
        return FALSE;
    }
    EditorStamp cur;
    if (!editor_stamp_changed(tab->path, &bw->stamp, &cur)) {
        return FALSE;
    }
    if (!cur.exists) {
        if (removed_out != NULL) *removed_out = TRUE;
        return TRUE;
    }
    if (disk_matches_buffer(tab, &cur)) {
        bw->stamp = cur;
        bw->conflict = FALSE;
        bw->removed = FALSE;
        rearm_monitor(bw, tab->path);
        sync_banners_for_buffer(tab->app, tab->buffer);
        return FALSE;
    }
    return TRUE;
}

gboolean editor_watch_is_reloading(GtkTextBuffer* buffer) {
    BufferWatch* bw = buffer_watch_get(buffer);
    return bw != NULL && bw->reloading;
}

gboolean editor_watch_reload_tab(AppData* app, EditorTab* tab, gboolean show_notice,
                                 gchar** error_out) {
    if (tab == NULL || tab->buffer == NULL || tab->path == NULL || tab->path[0] == '\0') {
        if (error_out != NULL) *error_out = g_strdup(_("No file to reload"));
        return FALSE;
    }
    const gchar* name = tab->name != NULL ? tab->name : tab->path;
    EditorStamp cur;
    if (!editor_stamp_read(tab->path, &cur) || !cur.exists) {
        if (error_out != NULL) {
            *error_out = g_strdup_printf(_("Cannot read %s: file does not exist"), name);
        }
        return FALSE;
    }
    if (cur.file.size > WATCH_MAX_BYTES) {
        if (error_out != NULL) {
            *error_out = g_strdup_printf(_("%s is too large to reload"), name);
        }
        return FALSE;
    }
    gchar* contents = NULL;
    gsize len = 0;
    GError* gerr = NULL;
    if (!editor_remote_get_contents(tab->path, &contents, &len, &gerr)) {
        if (error_out != NULL) {
            *error_out = g_strdup_printf(_("Cannot read %s: %s"), name,
                                         gerr != NULL ? gerr->message : _("error"));
        }
        g_clear_error(&gerr);
        return FALSE;
    }
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        if (!file_content_is_text((const guchar*)contents, MIN(len, FILE_SNIFF_BYTES))) {
            g_free(contents);
            if (error_out != NULL) {
                *error_out = g_strdup_printf(_("%s is now a binary file: not reloaded"), name);
            }
            return FALSE;
        }
        gchar* utf8 = text_encoding_to_utf8(contents, len, NULL);
        g_free(contents);
        contents = utf8;
        len = strlen(contents);
    }

    const gchar* line_ending = text_line_ending_detect(contents, len);
    const gchar* final_break = text_final_break(contents, len);
    contents[len - strlen(final_break)] = '\0';

    GtkTextBuffer* buf = tab->buffer;
    /* Remember cursor (line, char offset) and vertical scroll per view */
    GtkTextIter cur_it;
    gtk_text_buffer_get_iter_at_mark(buf, &cur_it, gtk_text_buffer_get_insert(buf));
    gint saved_line = gtk_text_iter_get_line(&cur_it);
    gint saved_col = gtk_text_iter_get_line_offset(&cur_it);

    GPtrArray* views = g_ptr_array_new();
    GArray* vscrolls = g_array_new(FALSE, FALSE, sizeof(gdouble));
    if (app != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = l->data;
            if (t != NULL && t->buffer == buf) {
                t->line_ending = line_ending;
                t->final_break = final_break;
                g_ptr_array_add(views, t);
                gdouble v = 0.0;
                if (t->scroll != NULL) {
                    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->scroll));
                    if (adj != NULL) v = gtk_adjustment_get_value(adj);
                }
                g_array_append_val(vscrolls, v);
            }
        }
    } else {
        tab->line_ending = line_ending;
        tab->final_break = final_break;
    }

    BufferWatch* bw = buffer_watch_ensure(app, buf);
    bw->reloading = TRUE;
    gtk_text_buffer_begin_user_action(buf);
    gtk_text_buffer_set_text(buf, contents, -1);
    gtk_text_buffer_end_user_action(buf);
    gtk_text_buffer_set_modified(buf, FALSE);
    bw->reloading = FALSE;
    g_free(contents);

    /* Restore cursor clamped to the new buffer dimensions */
    gint n_lines = gtk_text_buffer_get_line_count(buf);
    gint line = CLAMP(saved_line, 0, MAX(n_lines - 1, 0));
    GtkTextIter dest;
    gtk_text_buffer_get_iter_at_line(buf, &dest, line);
    gint max_col = gtk_text_iter_get_chars_in_line(&dest);
    if (max_col > 0 && !gtk_text_iter_ends_line(&dest)) {
        gint target_col = MIN(saved_col, max_col);
        /* Avoid stepping past a trailing newline character on the line */
        gtk_text_iter_set_line_offset(&dest, target_col);
        if (gtk_text_iter_get_line(&dest) != line) {
            gtk_text_buffer_get_iter_at_line(buf, &dest, line);
            if (!gtk_text_iter_ends_line(&dest)) gtk_text_iter_forward_to_line_end(&dest);
        }
    }
    gtk_text_buffer_place_cursor(buf, &dest);

    /* Restore scroll and refresh diff gutters across all split views */
    for (guint i = 0; i < views->len; i++) {
        EditorTab* t = g_ptr_array_index(views, i);
        if (t->scroll != NULL) {
            GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->scroll));
            if (adj != NULL) {
                gdouble v = g_array_index(vscrolls, gdouble, i);
                gdouble lower = gtk_adjustment_get_lower(adj);
                gdouble max_v = MAX(lower, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
                gtk_adjustment_set_value(adj, CLAMP(v, lower, max_v));
            }
        }
        if (t->view != NULL && t->path != NULL) {
            diff_gutter_set_file(t->view, t->path);
        }
    }
    g_ptr_array_unref(views);
    g_array_unref(vscrolls);

    /* Update stamp, re-arm monitor and hide any conflict banner */
    bw->stamp = cur;
    memset(&bw->notified_stamp, 0, sizeof(bw->notified_stamp));
    bw->conflict = FALSE;
    bw->removed = FALSE;
    rearm_monitor(bw, tab->path);
    sync_banners_for_buffer(app, buf);

    if (app != NULL) {
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }
    if (show_notice) {
        gchar* msg = NULL;
        if (tab->mode == EDITOR_MODE_VIM) {
            msg = g_strdup_printf(_("\"%s\" reloaded (changed on disk)"), name);
        } else if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
            msg = g_strdup_printf(_("Reverted buffer '%s'"), name);
        } else {
            msg = g_strdup_printf(_("Reloaded %s (changed on disk)"), name);
        }
        mode_notify(tab, msg, FALSE);
        g_free(msg);
    }
    return TRUE;
}

static void report_external_conflict(EditorTab* tab, BufferWatch* bw, const EditorStamp* cur,
                                     gboolean force_notify) {
    if (!force_notify && editor_stamp_equal(&bw->notified_stamp, cur)) {
        return;
    }
    bw->notified_stamp = *cur;
    const gchar* name = tab->name != NULL ? tab->name : tab->path;
    if (tab->mode == EDITOR_MODE_VIM) {
        gchar* msg = bw->removed
            ? g_strdup_printf(_("W12: Warning: File \"%s\" no longer exists"), name)
            : g_strdup_printf(_("W11: Warning: File \"%s\" has changed since editing started (use :e! to reload)"),
                              name);
        mode_notify(tab, msg, TRUE);
        g_free(msg);
    } else if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
        gchar* msg = bw->removed
            ? g_strdup_printf(_("%s no longer exists on disk"), name)
            : g_strdup_printf(_("%s changed on disk; type M-x revert-buffer to reload"), name);
        mode_notify(tab, msg, FALSE);
        g_free(msg);
    }
}

void editor_watch_check_tab(AppData* app, EditorTab* tab, gboolean force_notify) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->path == NULL || tab->path[0] == '\0') {
        return;
    }
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    if (bw == NULL || !bw->stamp.valid) {
        editor_watch_record(tab);
        return;
    }
    if (bw->reloading || bw->prompt_open) {
        return;
    }

    EditorStamp cur;
    if (!editor_stamp_changed(tab->path, &bw->stamp, &cur)) {
        if (bw->conflict) {
            bw->conflict = FALSE;
            bw->removed = FALSE;
            memset(&bw->notified_stamp, 0, sizeof(bw->notified_stamp));
            sync_banners_for_buffer(app, tab->buffer);
        }
        return;
    }

    if (!cur.exists) {
        bw->conflict = TRUE;
        bw->removed = TRUE;
        sync_banners_for_buffer(app, tab->buffer);
        report_external_conflict(tab, bw, &cur, force_notify);
        return;
    }

    if (cur.file.inode != bw->stamp.file.inode) {
        rearm_monitor(bw, tab->path);
    }

    /* Ignore no-op mtime bumps (e.g. touch) when bytes on disk match the buffer */
    if (disk_matches_buffer(tab, &cur)) {
        bw->stamp = cur;
        bw->conflict = FALSE;
        bw->removed = FALSE;
        memset(&bw->notified_stamp, 0, sizeof(bw->notified_stamp));
        sync_banners_for_buffer(app, tab->buffer);
        return;
    }

    /* Clean buffer + auto-reload enabled for this mode: reload silently in place */
    if (!gtk_text_buffer_get_modified(tab->buffer) && mode_auto_reload(tab)) {
        gchar* err = NULL;
        if (!editor_watch_reload_tab(app, tab, TRUE, &err)) {
            bw->conflict = TRUE;
            bw->removed = FALSE;
            sync_banners_for_buffer(app, tab->buffer);
            if (err != NULL) {
                mode_notify(tab, err, TRUE);
                g_free(err);
            }
        }
        return;
    }

    bw->conflict = TRUE;
    bw->removed = FALSE;
    sync_banners_for_buffer(app, tab->buffer);
    report_external_conflict(tab, bw, &cur, force_notify);
}

void editor_watch_check_all(AppData* app) {
    struct EditorWatch* ew = watch_state(app);
    if (ew == NULL || ew->checking || app->editors == NULL) {
        return;
    }
    ew->checking = TRUE;

    /* Pick one representative tab per distinct buffer (prefer the active view) */
    GHashTable* by_buf = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t->buffer == NULL || t->path == NULL || t->path[0] == '\0') continue;
        EditorTab* prev = g_hash_table_lookup(by_buf, t->buffer);
        if (prev == NULL || !t->view_passive) {
            g_hash_table_insert(by_buf, t->buffer, t);
        }
    }

    GHashTableIter iter;
    gpointer key, val;
    g_hash_table_iter_init(&iter, by_buf);
    while (g_hash_table_iter_next(&iter, &key, &val)) {
        editor_watch_check_tab(app, (EditorTab*)val, FALSE);
    }
    g_hash_table_destroy(by_buf);
    ew->checking = FALSE;
}

/* ------------------------------------------------------------------------- */
/* Notepad dialogs: overwrite confirmation & manual reload                   */
/* ------------------------------------------------------------------------- */

enum {
    RESP_OVERWRITE = 1,
    RESP_RELOAD = 2
};

EditorWatchSaveAction editor_watch_before_save(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->path == NULL) {
        return EDITOR_WATCH_SAVE_OK;
    }
    gboolean removed = FALSE;
    if (!editor_watch_disk_changed(tab, &removed)) {
        return EDITOR_WATCH_SAVE_OK;
    }
    BufferWatch* bw = buffer_watch_get(tab->buffer);
    if (bw != NULL && bw->prompt_open) {
        return EDITOR_WATCH_SAVE_CANCEL;
    }
    if (bw != NULL) bw->prompt_open = TRUE;

    const gchar* name = tab->name != NULL ? tab->name : tab->path;
    GtkWidget* dlg;
    gchar* msg;
    if (removed) {
        dlg = app_dialog_new(_("File Removed on Disk"), app->window,
                             _("_Cancel"), APP_RESPONSE_CANCEL,
                             _("_Save"), RESP_OVERWRITE,
                             NULL);
        msg = g_strdup_printf(_("“%s” was removed from disk. Save the current buffer anyway?"), name);
    } else {
        dlg = app_dialog_new(_("File Changed on Disk"), app->window,
                             _("_Cancel"), APP_RESPONSE_CANCEL,
                             _("_Reload"), RESP_RELOAD,
                             _("_Overwrite"), RESP_OVERWRITE,
                             NULL);
        msg = g_strdup_printf(_("“%s” has changed on disk since it was opened. "
                                "Overwrite it with your changes, or reload from disk?"), name);
    }
    GtkWidget* lbl = gtk_label_new(msg);
    g_free(msg);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 58);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_top(lbl, 14);
    gtk_widget_set_margin_bottom(lbl, 14);
    gtk_widget_set_margin_start(lbl, 16);
    gtk_widget_set_margin_end(lbl, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), lbl);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint resp = app_dialog_run(APP_DIALOG(dlg),
                               app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    if (bw != NULL) bw->prompt_open = FALSE;

    if (resp == RESP_OVERWRITE) {
        return EDITOR_WATCH_SAVE_OK;
    }
    if (resp == RESP_RELOAD) {
        gchar* err = NULL;
        if (!editor_watch_reload_tab(app, tab, TRUE, &err) && err != NULL) {
            app_notice(app, err, FALSE);
        }
        g_free(err);
        return EDITOR_WATCH_SAVE_RELOADED;
    }
    return EDITOR_WATCH_SAVE_CANCEL;
}

void editor_watch_reload_interactive(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->path == NULL) return;
    if (gtk_text_buffer_get_modified(tab->buffer) && !tab->is_buffer) {
        const gchar* name = tab->name != NULL ? tab->name : tab->path;
        GtkWidget* dlg = app_dialog_new(_("Reload from Disk"), app->window,
                                        _("_Cancel"), APP_RESPONSE_CANCEL,
                                        _("_Reload"), APP_RESPONSE_OK,
                                        NULL);
        gchar* msg = g_strdup_printf(_("Discard unsaved changes in “%s” and reload from disk?"), name);
        GtkWidget* lbl = gtk_label_new(msg);
        g_free(msg);
        gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 58);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_margin_top(lbl, 14);
        gtk_widget_set_margin_bottom(lbl, 14);
        gtk_widget_set_margin_start(lbl, 16);
        gtk_widget_set_margin_end(lbl, 16);
        gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), lbl);
        app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
        gint resp = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
        app_dialog_destroy(APP_DIALOG(dlg));
        if (resp != APP_RESPONSE_OK) return;
    }
    gchar* err = NULL;
    if (!editor_watch_reload_tab(app, tab, TRUE, &err) && err != NULL) {
        mode_notify(tab, err, TRUE);
    }
    g_free(err);
}

static void detach_editors_list(GList* editors) {
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t->buffer == NULL) continue;
        BufferWatch* bw = buffer_watch_get(t->buffer);
        if (bw != NULL) {
            bw->app = NULL;
            cancel_monitor(bw);
        }
    }
}

void editor_watch_free(AppData* app) {
    if (app == NULL) return;
    detach_editors_list(app->editors);
    for (gint i = 0; i < app->num_workspaces; i++) {
        detach_editors_list(app->workspaces[i].editors);
    }
    if (app->editor_watch != NULL) {
        if (app->editor_watch->check_source != 0) {
            g_source_remove(app->editor_watch->check_source);
            app->editor_watch->check_source = 0;
        }
        g_clear_pointer(&app->editor_watch, g_free);
    }
}

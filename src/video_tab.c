/* Video tabs of the editor area (see video_tab.h) */
#include "video_tab.h"
#include "i18n.h"
#include "editor.h"
#include "icons.h"
#include "media_player.h"
#include "theme.h"
#include "track_menu.h"
#include "video.h"

#define VIDEO_TAB_KEY "lookin-video-tab"
#define TAB_MAX_CHARS 24
#define SEEK_STEP_US (5 * G_USEC_PER_SEC)

typedef struct {
    AppData* app;
    GtkWidget* root;          /* the notebook page */
    GtkWidget* tab_label;     /* owned ref: kept while the page moves */
    GtkWidget* video;         /* GtkVideo */
    GtkWidget* status;        /* error line of the bar */
    GtkMediaStream* stream;   /* owned ref */
    gchar* path;
    gboolean play_when_ready; /* just opened: plays once prepared */
    gboolean closed;
} VideoTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".video-tab { background-color: @bg_view; }"
                  ".video-tab .video-tab-bar { padding: 2px 12px; color: @fg_muted;"
                  " background-color: @bg_bar; border-top: 1px solid @border; font-size: @size_bar; }"
                  ".video-tab .video-tab-bar menubutton > button { padding: 0 6px; min-height: 0; }");
}

static VideoTab* tab_of(GtkWidget* root) {
    return root != NULL ? g_object_get_data(G_OBJECT(root), VIDEO_TAB_KEY) : NULL;
}

/* The notebook holding page, or NULL */
static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* parent = gtk_widget_get_parent(page);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

/* In the window: a tab of the workspace shown */
static gboolean tab_shown(VideoTab* vt) {
    return vt->app->window != NULL && gtk_widget_get_root(vt->root) == GTK_ROOT(vt->app->window);
}

/* The open tab of the video at path (any workspace), or NULL */
static VideoTab* find_tab(AppData* app, const gchar* path) {
    for (guint i = 0; app->video_pages != NULL && i < app->video_pages->len; i++) {
        VideoTab* vt = tab_of(g_ptr_array_index(app->video_pages, i));
        if (vt != NULL && !vt->closed && g_strcmp0(vt->path, path) == 0) return vt;
    }
    return NULL;
}

/* The tab holding the keyboard focus, or NULL */
static VideoTab* focused_tab(AppData* app) {
    GtkWidget* f = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    for (guint i = 0; f != NULL && app->video_pages != NULL && i < app->video_pages->len; i++) {
        GtkWidget* root = g_ptr_array_index(app->video_pages, i);
        if (f == root || gtk_widget_is_ancestor(f, root)) return tab_of(root);
    }
    return NULL;
}

/* Stop the pipeline: GStreamer frees its threads and files only then */
static void stop_stream(VideoTab* vt) {
    if (vt->stream == NULL) return;
    if (gtk_media_stream_get_playing(vt->stream)) gtk_media_stream_pause(vt->stream);
    gtk_media_file_clear(GTK_MEDIA_FILE(vt->stream));
}

/* Take the tab out of its notebook, or of the hidden workspace keeping it */
static void close_tab(VideoTab* vt) {
    AppData* app = vt->app;
    vt->closed = TRUE;
    stop_stream(vt);
    GtkWidget* root = g_object_ref(vt->root);
    if (app->video_pages != NULL) g_ptr_array_remove(app->video_pages, root);
    GtkNotebook* nb = notebook_of(root);
    if (nb != NULL && tab_shown(vt)) {
        editor_remove_page(app, root);
    } else if (nb != NULL) {
        /* In the editor panel of a hidden workspace: a notebook left empty
         * goes when the workspace shows again */
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, root));
    } else {
        for (gint i = 0; i < app->num_workspaces; i++) {
            GPtrArray* kept = app->workspaces[i].video_pages;
            if (kept != NULL) g_ptr_array_remove(kept, root);
        }
    }
    g_object_unref(root);
}

static void toggle_play(VideoTab* vt) {
    if (gtk_media_stream_get_playing(vt->stream)) {
        gtk_media_stream_pause(vt->stream);
    } else {
        if (gtk_media_stream_get_ended(vt->stream)) gtk_media_stream_seek(vt->stream, 0);
        gtk_media_stream_play(vt->stream);
    }
}

static void seek_by(VideoTab* vt, gint64 step_us) {
    if (!gtk_media_stream_is_seekable(vt->stream)) return;
    gint64 to = gtk_media_stream_get_timestamp(vt->stream) + step_us;
    gint64 duration = gtk_media_stream_get_duration(vt->stream);
    if (duration > 0 && to > duration) to = duration;
    gtk_media_stream_seek(vt->stream, MAX(to, 0));
}

/* Plain keys only: the window keeps its shortcuts (Ctrl+W, Ctrl+Q...) */
static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)ctrl;
    (void)keycode;
    VideoTab* vt = data;
    if (vt->closed || (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_SHIFT_MASK)) != 0) {
        return FALSE;
    }
    switch (keyval) {
    case GDK_KEY_space:
    case GDK_KEY_k:
        toggle_play(vt);
        return TRUE;
    case GDK_KEY_Left:
        seek_by(vt, -SEEK_STEP_US);
        return TRUE;
    case GDK_KEY_Right:
        seek_by(vt, SEEK_STEP_US);
        return TRUE;
    case GDK_KEY_Home:
        if (gtk_media_stream_is_seekable(vt->stream)) gtk_media_stream_seek(vt->stream, 0);
        return TRUE;
    case GDK_KEY_a:
        media_player_cycle_track(vt->stream, MEDIA_TRACK_AUDIO);
        return TRUE;
    case GDK_KEY_s:
        media_player_cycle_track(vt->stream, MEDIA_TRACK_SUBTITLE);
        return TRUE;
    default:
        return FALSE;
    }
}

static void on_error(GtkMediaStream* stream, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    VideoTab* vt = data;
    const GError* err = gtk_media_stream_get_error(stream);
    if (err == NULL || vt->closed) return;
    gchar* text = g_strdup_printf(_("Playback error: %s"),
                                  err->message != NULL && err->message[0] != '\0' ? err->message : _("unknown"));
    gtk_label_set_text(GTK_LABEL(vt->status), text);
    gtk_widget_set_visible(vt->status, TRUE);
    g_free(text);
}

static void on_prepared(GtkMediaStream* stream, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    VideoTab* vt = data;
    if (!vt->play_when_ready || vt->closed || !gtk_media_stream_is_prepared(stream)) return;
    vt->play_when_ready = FALSE;
    if (gtk_widget_get_mapped(vt->root)) gtk_media_stream_play(stream);
}

/* Out of sight (another tab, another workspace): pause */
static void on_root_unmap(GtkWidget* widget, gpointer data) {
    (void)widget;
    VideoTab* vt = data;
    if (vt->stream != NULL && gtk_media_stream_get_playing(vt->stream)) gtk_media_stream_pause(vt->stream);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    VideoTab* vt = data;
    if (!vt->closed) close_tab(vt);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)data;
    VideoTab* vt = tab_of(child);
    if (vt != NULL && !vt->closed) close_tab(vt);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    VideoTab* vt = data;
    if (!vt->closed) editor_page_show_context_menu(vt->app, vt->root, vt->tab_label, on_menu_close, NULL);
}

static void on_root_destroy(GtkWidget* widget, gpointer data) {
    VideoTab* vt = data;
    vt->closed = TRUE;
    stop_stream(vt);
    if (vt->app->video_pages != NULL) g_ptr_array_remove(vt->app->video_pages, widget);
}

static void tab_free(gpointer data) {
    VideoTab* vt = data;
    if (vt->stream != NULL) g_signal_handlers_disconnect_by_data(vt->stream, vt);
    g_clear_object(&vt->stream);
    g_clear_object(&vt->tab_label);
    g_free(vt->path);
    g_free(vt);
}

static GtkWidget* tab_label_new(VideoTab* vt, const gchar* name) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* image = file_type_image_new();
    file_type_image_set(image, name, FALSE);
    gtk_box_append(GTK_BOX(box), image);
    GtkWidget* text = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(text), TAB_MAX_CHARS);
    /* As wide as the name (up to TAB_MAX_CHARS) even when tabs are crowded */
    gtk_label_set_width_chars(GTK_LABEL(text), (gint)MIN(g_utf8_strlen(name, -1), TAB_MAX_CHARS));
    gtk_box_append(GTK_BOX(box), text);
    gtk_widget_set_tooltip_text(box, vt->path);

    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), vt);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), vt);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(rclick));
    return g_object_ref_sink(box);
}

/* A new tab playing path, in no notebook yet */
static VideoTab* tab_new(AppData* app, const gchar* name, const gchar* path) {
    ensure_css();
    VideoTab* vt = g_new0(VideoTab, 1);
    vt->app = app;
    vt->path = g_strdup(path);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "video-tab");
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    vt->root = root;

    /* Its own stream: the preview player keeps its file */
    vt->stream = media_player_new();
    gtk_media_file_set_filename(GTK_MEDIA_FILE(vt->stream), path);
    g_signal_connect(vt->stream, "notify::error", G_CALLBACK(on_error), vt);
    g_signal_connect(vt->stream, "notify::prepared", G_CALLBACK(on_prepared), vt);
    vt->video = gtk_video_new();
    gtk_widget_set_hexpand(vt->video, TRUE);
    gtk_widget_set_vexpand(vt->video, TRUE);
    gtk_widget_set_focusable(vt->video, TRUE);
    gtk_video_set_autoplay(GTK_VIDEO(vt->video), FALSE);
    gtk_video_set_media_stream(GTK_VIDEO(vt->video), vt->stream);
    gtk_box_append(GTK_BOX(root), vt->video);

    /* Bar (bottom): the file's path, the track menus, a playback error */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "video-tab-bar");
    GtkWidget* where = gtk_label_new(path);
    gtk_label_set_ellipsize(GTK_LABEL(where), PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(where), 0.0f);
    gtk_widget_set_hexpand(where, TRUE);
    gtk_widget_set_tooltip_text(where, _("Space: play / pause; Left / Right: 5 s back / forward; Home: start; "
                                         "a: next audio track; s: next subtitles"));
    gtk_box_append(GTK_BOX(bar), where);
    gtk_box_append(GTK_BOX(bar), track_menu_new(vt->stream, TRUE));
    vt->status = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(vt->status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_visible(vt->status, FALSE);
    gtk_box_append(GTK_BOX(bar), vt->status);
    gtk_box_append(GTK_BOX(root), bar);

    /* Capture: the player's buttons would take Space and the arrows first */
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), vt);
    gtk_widget_add_controller(root, keys);

    vt->tab_label = tab_label_new(vt, name);
    g_object_set_data_full(G_OBJECT(root), VIDEO_TAB_KEY, vt, tab_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), vt);
    g_signal_connect(root, "unmap", G_CALLBACK(on_root_unmap), vt);
    if (app->video_pages == NULL) app->video_pages = g_ptr_array_new();
    g_ptr_array_add(app->video_pages, root);
    return vt;
}

void video_tab_open(AppData* app, const gchar* name, const gchar* path) {
    if (app == NULL || name == NULL || path == NULL || app->notebook == NULL) return;
    VideoTab* vt = find_tab(app, path);
    if (vt != NULL && !tab_shown(vt)) {
        /* Open in another workspace: it moves to this one */
        close_tab(vt);
        vt = NULL;
    }
    gboolean opened = vt == NULL;
    if (opened) {
        vt = tab_new(app, name, path);
        editor_add_page(app, vt->root, vt->tab_label);
    }
    GtkNotebook* nb = notebook_of(vt->root);
    if (nb != NULL) gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, vt->root));
    /* One player at a time: the preview's stops playing */
    video_preview_pause(app);
    if (opened) {
        vt->play_when_ready = TRUE;
        on_prepared(vt->stream, NULL, vt);
    }
    gtk_widget_grab_focus(vt->video);
}

gboolean video_tab_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    VideoTab* vt = focused_tab(app);
    if (vt == NULL && app->notebook != NULL) {
        /* Else the current page of the preview notebook */
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        gint cur = gtk_notebook_get_current_page(nb);
        vt = cur >= 0 ? tab_of(gtk_notebook_get_nth_page(nb, cur)) : NULL;
        if (vt != NULL && !gtk_widget_get_mapped(vt->root)) vt = NULL;
    }
    if (vt == NULL || vt->closed) return FALSE;
    close_tab(vt);
    return TRUE;
}

void video_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (tab_of(page) == NULL) continue;
        if (ws->video_pages == NULL) ws->video_pages = g_ptr_array_new_with_free_func(g_object_unref);
        g_ptr_array_insert(ws->video_pages, 0, g_object_ref(page));
        gtk_notebook_remove_page(nb, i);
    }
}

void video_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->video_pages == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (guint i = 0; i < ws->video_pages->len; i++) {
        GtkWidget* root = g_ptr_array_index(ws->video_pages, i);
        VideoTab* vt = tab_of(root);
        if (vt == NULL) continue;
        gtk_notebook_append_page(nb, root, vt->tab_label);
        gtk_notebook_set_tab_reorderable(nb, root, TRUE);
        gtk_notebook_set_tab_detachable(nb, root, TRUE);
    }
    g_clear_pointer(&ws->video_pages, g_ptr_array_unref);
}

void video_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        video_tab_workspace_hidden(app, ws);
    }
    g_clear_pointer(&ws->video_pages, g_ptr_array_unref);
}

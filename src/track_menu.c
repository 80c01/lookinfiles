/* Audio and subtitle track buttons of the video players (see track_menu.h) */
#include "track_menu.h"
#include "media_player.h"

#define KIND_KEY "lookin-track-kind"
#define INDEX_KEY "lookin-track-index"

typedef struct {
    GtkMediaStream* stream; /* owned ref */
    GtkWidget* buttons[2];  /* GtkMenuButton per MediaTrackKind */
} TrackMenu;

static void track_menu_free(gpointer data) {
    TrackMenu* tm = data;
    g_clear_object(&tm->stream);
    g_free(tm);
}

static void on_item_clicked(GtkButton* item, gpointer data) {
    TrackMenu* tm = data;
    MediaTrackKind kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), KIND_KEY));
    gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), INDEX_KEY));
    gtk_menu_button_popdown(GTK_MENU_BUTTON(tm->buttons[kind]));
    media_player_set_track(tm->stream, kind, index);
}

static void add_item(TrackMenu* tm, GtkWidget* box, MediaTrackKind kind, gint index, const gchar* label, gint current) {
    GtkWidget* item = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(item, "flat");
    if (index == current) gtk_widget_add_css_class(item, "menu-item-current");
    GtkWidget* text = gtk_button_get_child(GTK_BUTTON(item));
    if (GTK_IS_LABEL(text)) gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    g_object_set_data(G_OBJECT(item), KIND_KEY, GINT_TO_POINTER(kind));
    g_object_set_data(G_OBJECT(item), INDEX_KEY, GINT_TO_POINTER(index));
    g_signal_connect(item, "clicked", G_CALLBACK(on_item_clicked), tm);
    gtk_box_append(GTK_BOX(box), item);
}

static void refresh_kind(TrackMenu* tm, MediaTrackKind kind) {
    GtkWidget* button = tm->buttons[kind];
    gchar** labels = media_player_track_labels(tm->stream, kind);
    guint n = labels != NULL ? g_strv_length(labels) : 0;
    gboolean audio = kind == MEDIA_TRACK_AUDIO;
    gtk_widget_set_visible(button, audio ? n >= 2 : n >= 1);
    if (n == 0) {
        g_strfreev(labels);
        return;
    }
    gint current = media_player_get_track(tm->stream, kind);
    const gchar* name = current >= 0 && (guint)current < n ? labels[current] : (audio ? "-" : "Off");
    gchar* text = g_strdup_printf("%s: %s", audio ? "Audio" : "Subtitles", name);
    gtk_menu_button_set_label(GTK_MENU_BUTTON(button), text);
    g_free(text);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    if (!audio) add_item(tm, box, kind, -1, "Off", current);
    for (guint i = 0; i < n; i++) add_item(tm, box, kind, (gint)i, labels[i], current);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_popover_set_child(GTK_POPOVER(gtk_menu_button_get_popover(GTK_MENU_BUTTON(button))), scroll);
    g_strfreev(labels);
}

static void on_tracks_changed(GtkMediaStream* stream, gpointer data) {
    (void)stream;
    TrackMenu* tm = g_object_get_data(G_OBJECT(data), "lookin-track-menu");
    if (tm == NULL) return;
    refresh_kind(tm, MEDIA_TRACK_AUDIO);
    refresh_kind(tm, MEDIA_TRACK_SUBTITLE);
}

static GtkWidget* kind_button_new(const gchar* tooltip) {
    GtkWidget* button = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(button), FALSE);
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(button), TRUE);
    gtk_widget_set_tooltip_text(button, tooltip);
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    gtk_widget_set_visible(button, FALSE);
    return button;
}

GtkWidget* track_menu_new(GtkMediaStream* stream, gboolean key_hints) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(box, "track-menu");
    TrackMenu* tm = g_new0(TrackMenu, 1);
    tm->stream = g_object_ref(stream);
    tm->buttons[MEDIA_TRACK_AUDIO] = kind_button_new(key_hints ? "Audio Track (a)" : "Audio Track");
    tm->buttons[MEDIA_TRACK_SUBTITLE] = kind_button_new(key_hints ? "Subtitles (s)" : "Subtitles");
    gtk_box_append(GTK_BOX(box), tm->buttons[MEDIA_TRACK_AUDIO]);
    gtk_box_append(GTK_BOX(box), tm->buttons[MEDIA_TRACK_SUBTITLE]);
    g_object_set_data_full(G_OBJECT(box), "lookin-track-menu", tm, track_menu_free);
    media_player_connect_tracks_changed(stream, G_CALLBACK(on_tracks_changed), box);
    on_tracks_changed(stream, box);
    return box;
}

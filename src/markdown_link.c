/* Links that can be followed in rendered Markdown (see markdown_link.h) */
#include "markdown_link.h"

#define LINKS_KEY "lookin-md-links"     /* buffer: GArray of MdLink (links on) */
#define HANDLER_KEY "lookin-md-handler" /* view: LinkHandler */

typedef struct {
    gint start; /* character offsets in the buffer */
    gint end;
    gchar* url;
} MdLink;

typedef struct {
    MarkdownLinkFunc func;
    gpointer data;
} LinkHandler;

static void link_clear(gpointer p) {
    g_free(((MdLink*)p)->url);
}

static GArray* links_of(GtkTextBuffer* buffer) {
    return buffer != NULL ? g_object_get_data(G_OBJECT(buffer), LINKS_KEY) : NULL;
}

gboolean markdown_link_enabled(GtkTextBuffer* buffer) {
    return links_of(buffer) != NULL;
}

void markdown_link_clear(GtkTextBuffer* buffer) {
    GArray* links = links_of(buffer);
    if (links != NULL) {
        g_array_set_size(links, 0);
    }
}

void markdown_link_add(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end, const gchar* url) {
    GArray* links = links_of(buffer);
    if (links == NULL || url == NULL) {
        return;
    }
    MdLink l = {gtk_text_iter_get_offset(start), gtk_text_iter_get_offset(end), g_strdup(url)};
    g_array_append_val(links, l);
}

guint markdown_link_count(GtkTextBuffer* buffer) {
    GArray* links = links_of(buffer);
    return links != NULL ? links->len : 0;
}

const gchar* markdown_link_get(GtkTextBuffer* buffer, guint i, GtkTextIter* start, GtkTextIter* end) {
    GArray* links = links_of(buffer);
    if (links == NULL || i >= links->len) {
        return NULL;
    }
    MdLink* l = &g_array_index(links, MdLink, i);
    if (start != NULL) gtk_text_buffer_get_iter_at_offset(buffer, start, l->start);
    if (end != NULL) gtk_text_buffer_get_iter_at_offset(buffer, end, l->end);
    return l->url;
}

gint markdown_link_index_at(GtkTextBuffer* buffer, const GtkTextIter* iter) {
    GArray* links = links_of(buffer);
    if (links == NULL) {
        return -1;
    }
    gint offset = gtk_text_iter_get_offset(iter);
    for (guint i = 0; i < links->len; i++) {
        MdLink* l = &g_array_index(links, MdLink, i);
        if (offset >= l->start && offset < l->end) {
            return (gint)i;
        }
    }
    return -1;
}

void markdown_link_activate(GtkTextView* view, const gchar* url) {
    LinkHandler* h = view != NULL ? g_object_get_data(G_OBJECT(view), HANDLER_KEY) : NULL;
    if (h != NULL && url != NULL) {
        h->func(view, url, h->data);
    }
}

/* Link under the widget point (x, y) of view, or -1 */
static gint link_at_point(GtkTextView* view, gdouble x, gdouble y) {
    gint bx = 0, by = 0;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y, &bx, &by);
    GtkTextIter iter;
    if (!gtk_text_view_get_iter_at_location(view, &iter, bx, by)) {
        return -1;
    }
    return markdown_link_index_at(gtk_text_view_get_buffer(view), &iter);
}

static void on_released(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)data;
    GtkTextView* view = GTK_TEXT_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    /* Not at the end of a selection drag */
    if (n_press != 1 || gtk_text_buffer_get_has_selection(buffer)) {
        return;
    }
    gint i = link_at_point(view, x, y);
    if (i >= 0) {
        markdown_link_activate(view, markdown_link_get(buffer, (guint)i, NULL, NULL));
    }
}

static void on_motion(GtkEventControllerMotion* motion, gdouble x, gdouble y, gpointer data) {
    (void)data;
    GtkWidget* view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
    gtk_widget_set_cursor_from_name(view, link_at_point(GTK_TEXT_VIEW(view), x, y) >= 0 ? "pointer" : NULL);
}

static gboolean on_label_link(GtkLabel* label, const gchar* uri, gpointer data) {
    (void)label;
    markdown_link_activate(GTK_TEXT_VIEW(data), uri);
    return TRUE; /* not GTK's default (open with the system) */
}

void markdown_link_connect_label(GtkWidget* label, GtkTextView* view) {
    g_signal_connect_object(label, "activate-link", G_CALLBACK(on_label_link), view, 0);
}

void markdown_link_attach(GtkTextView* view, MarkdownLinkFunc func, gpointer data) {
    if (view == NULL || func == NULL) {
        return;
    }
    LinkHandler* h = g_new0(LinkHandler, 1);
    h->func = func;
    h->data = data;
    g_object_set_data_full(G_OBJECT(view), HANDLER_KEY, h, g_free);

    GArray* links = g_array_new(FALSE, FALSE, sizeof(MdLink));
    g_array_set_clear_func(links, link_clear);
    g_object_set_data_full(G_OBJECT(gtk_text_view_get_buffer(view)), LINKS_KEY, links,
                           (GDestroyNotify)g_array_unref);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "released", G_CALLBACK(on_released), NULL);
    gtk_widget_add_controller(GTK_WIDGET(view), GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(GTK_WIDGET(view), motion);
}

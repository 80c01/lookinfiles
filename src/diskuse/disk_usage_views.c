/* The views of the Disk Usage tab under its breakdown (see disk_usage_views.h) */
#include "diskuse/disk_usage_views.h"
#include "diskuse/disk_usage_charts.h"
#include "diskuse/disk_usage_map.h"
#include "diskuse/disk_usage_wheel.h"
#include "core/i18n.h"

#define VIEWS_KEY "lookin-disk-usage-views"

enum { VIEW_MAP, VIEW_WHEEL, VIEW_CHARTS, VIEW_COUNT };

typedef struct {
    GtkWidget* stack;
    GtkWidget* buttons[VIEW_COUNT];
    GtkWidget* map;
    GtkWidget* wheel;
    GtkWidget* charts;
} Views;

static const gchar* const VIEW_NAMES[VIEW_COUNT] = { "map", "wheel", "charts" };

static Views* views_state(GtkWidget* views) {
    return views != NULL ? g_object_get_data(G_OBJECT(views), VIEWS_KEY) : NULL;
}

static void on_toggled(GtkToggleButton* button, gpointer data) {
    Views* v = data;
    if (!gtk_toggle_button_get_active(button)) return;
    for (gint i = 0; i < VIEW_COUNT; i++) {
        if (v->buttons[i] == GTK_WIDGET(button)) gtk_stack_set_visible_child_name(GTK_STACK(v->stack), VIEW_NAMES[i]);
    }
}

static GtkWidget* view_button(Views* v, gint view, const gchar* label, const gchar* tooltip) {
    GtkWidget* b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_focus_on_click(b, FALSE);
    if (view > 0) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(v->buttons[0]));
    g_signal_connect(b, "toggled", G_CALLBACK(on_toggled), v);
    v->buttons[view] = b;
    return b;
}

GtkWidget* disk_usage_views_new(const DiskUsageViewsHandlers* handlers) {
    Views* v = g_new0(Views, 1);
    DiskUsageViewsHandlers h = { 0 };
    if (handlers != NULL) h = *handlers;
    DiskUsageMapHandlers map_handlers = { h.selected, h.open, h.reveal, h.data };
    DiskUsageWheelHandlers wheel_handlers = { h.selected, h.open, h.reveal, h.up, h.data };
    v->map = disk_usage_map_new(&map_handlers);
    v->wheel = disk_usage_wheel_new(&wheel_handlers);
    v->charts = disk_usage_charts_new();

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "du-views");
    GtkWidget* switcher = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(switcher, "linked");
    gtk_widget_add_css_class(switcher, "du-switcher");
    gtk_widget_set_halign(switcher, GTK_ALIGN_END);
    /* TRANSLATORS: the views of the Disk Usage tab; v shows the next one */
    gtk_box_append(GTK_BOX(switcher), view_button(v, VIEW_MAP, _("Treemap"), _("Nested Rectangles (v: Next View)")));
    gtk_box_append(GTK_BOX(switcher), view_button(v, VIEW_WHEEL, _("Wheel"), _("Rings Around the Folder (v: Next View)")));
    gtk_box_append(GTK_BOX(switcher),
                   view_button(v, VIEW_CHARTS, _("Histograms"), _("Files by Size and by Age (v: Next View)")));
    gtk_box_append(GTK_BOX(box), switcher);

    v->stack = gtk_stack_new();
    gtk_widget_set_vexpand(v->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(v->stack), v->map, VIEW_NAMES[VIEW_MAP]);
    gtk_stack_add_named(GTK_STACK(v->stack), v->wheel, VIEW_NAMES[VIEW_WHEEL]);
    gtk_stack_add_named(GTK_STACK(v->stack), v->charts, VIEW_NAMES[VIEW_CHARTS]);
    gtk_box_append(GTK_BOX(box), v->stack);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->buttons[VIEW_MAP]), TRUE);
    g_object_set_data_full(G_OBJECT(box), VIEWS_KEY, v, g_free);
    return box;
}

void disk_usage_views_show(GtkWidget* views, DiskNode* node) {
    Views* v = views_state(views);
    if (v == NULL) return;
    disk_usage_map_show(v->map, node);
    disk_usage_wheel_show(v->wheel, node);
    disk_usage_charts_show(v->charts, node);
}

void disk_usage_views_select(GtkWidget* views, const gchar* name) {
    Views* v = views_state(views);
    if (v == NULL) return;
    disk_usage_map_select(v->map, name);
    disk_usage_wheel_select(v->wheel, name);
}

void disk_usage_views_next(GtkWidget* views) {
    Views* v = views_state(views);
    if (v == NULL) return;
    for (gint i = 0; i < VIEW_COUNT; i++) {
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->buttons[i]))) continue;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->buttons[(i + 1) % VIEW_COUNT]), TRUE);
        return;
    }
}

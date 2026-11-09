/* The right-click menu of the preview (see preview_menu.h) */
#include "preview/preview_menu.h"
#include "preview/preview_tagbar.h"
#include "widgets/minimap.h"
#include "core/i18n.h"

typedef struct {
    AppData* app;
    GMenu* section;          /* the tagbar and minimap items */
    GSimpleAction* tagbar;
    GSimpleAction* minimap;
} PreviewMenu;

static void preview_menu_free(gpointer data) {
    PreviewMenu* pm = data;
    g_object_unref(pm->section);
    g_object_unref(pm->tagbar);
    g_object_unref(pm->minimap);
    g_free(pm);
}

static void on_tagbar(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)action;
    (void)param;
    PreviewMenu* pm = data;
    preview_tagbar_toggle(pm->app);
}

static void on_minimap(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)action;
    (void)param;
    PreviewMenu* pm = data;
    minimap_viewer_set_enabled(pm->app, !minimap_viewer_enabled(pm->app));
}

static void append_item(GMenu* menu, const gchar* label, const gchar* action, const gchar* accel) {
    GMenuItem* item = g_menu_item_new(label, action);
    g_menu_item_set_attribute(item, "hidden-when", "s", "action-disabled");
    if (accel != NULL) g_menu_item_set_attribute(item, "accel", "s", accel);
    g_menu_append_item(menu, item);
    g_object_unref(item);
}

/* Right click, before the menu opens: the labels follow the current state */
static void on_secondary_press(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    PreviewMenu* pm = data;
    AppData* app = pm->app;
    g_simple_action_set_enabled(pm->tagbar, preview_tagbar_available(app));
    g_simple_action_set_enabled(pm->minimap, app->viewer_minimap != NULL);
    g_menu_remove_all(pm->section);
    append_item(pm->section, preview_tagbar_shown(app) ? _("Hide Tagbar") : _("Show Tagbar"), "preview.tagbar",
                "o");
    append_item(pm->section,
                minimap_viewer_enabled(app) ? _("Hide Minimap") : _("Show Minimap"),
                "preview.minimap", NULL);
}

void preview_menu_attach(AppData* app, GtkWidget* text_view) {
    PreviewMenu* pm = g_new0(PreviewMenu, 1);
    pm->app = app;
    pm->section = g_menu_new();
    pm->tagbar = g_simple_action_new("tagbar", NULL);
    pm->minimap = g_simple_action_new("minimap", NULL);
    g_signal_connect(pm->tagbar, "activate", G_CALLBACK(on_tagbar), pm);
    g_signal_connect(pm->minimap, "activate", G_CALLBACK(on_minimap), pm);

    GSimpleActionGroup* group = g_simple_action_group_new();
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(pm->tagbar));
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(pm->minimap));
    gtk_widget_insert_action_group(text_view, "preview", G_ACTION_GROUP(group));
    g_object_unref(group);

    /* The items already there first, then ours in a section of their own */
    GMenu* menu = g_menu_new();
    GMenuModel* before = gtk_text_view_get_extra_menu(GTK_TEXT_VIEW(text_view));
    if (before != NULL) g_menu_append_section(menu, NULL, before);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(pm->section));
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(text_view), G_MENU_MODEL(menu));
    g_object_unref(menu);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_secondary_press), pm);
    gtk_widget_add_controller(text_view, GTK_EVENT_CONTROLLER(click));
    g_object_set_data_full(G_OBJECT(click), "preview-menu", pm, preview_menu_free);
}

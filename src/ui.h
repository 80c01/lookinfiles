#ifndef UI_H
#define UI_H

#include "app_types.h"

gboolean window_is_fullscreen(GtkWindow* win);
void toggle_fullscreen(GtkWindow* window);

void create_help_panel(GtkWindow* parent, AppData* app);
gboolean on_help_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

void focus_adjacent_panel(gint step, AppData* app);
gboolean on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

void update_pane_dims(GtkWidget* paned_widget, AppData* app);
gboolean pane_dims_idle(gpointer data);
gboolean on_paned_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data);
void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data);
gboolean on_paned_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data);

void layout_save_frac(AppData* app);
gboolean layout_position_idle(gpointer data);
void layout_build(AppData* app, GtkOrientation orientation);
void layout_toggle(AppData* app);

void apply_panel_borders(void);

#endif /* UI_H */

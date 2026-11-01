#ifndef TOPBAR_H
#define TOPBAR_H

/* Top bar breadcrumbs and toggle pills */
#include "app_types.h"

/* Rebuild the top bar from AppData: single source of truth for directory/sort/hidden state */
void update_path_label(AppData* app);

void on_files_pill_clicked(GtkButton* btn, gpointer data);
void on_preview_pill_clicked(GtkButton* btn, gpointer data);
void on_split_btn_clicked(GtkButton* btn, gpointer data);
void on_term_pill_clicked(GtkButton* btn, gpointer data);
void on_sort_pill_clicked(GtkButton* btn, gpointer data);
void on_hidden_pill_clicked(GtkButton* btn, gpointer data);
/* Help button: a click opens the user guide (Help tab), a right click shows
 * or hides the keyboard cheat sheet */
void on_help_pill_clicked(GtkButton* btn, gpointer data);
void on_help_pill_right_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);

#endif /* TOPBAR_H */

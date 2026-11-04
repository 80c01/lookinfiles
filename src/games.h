#ifndef GAMES_H
#define GAMES_H

/* Games mode: secret arcade and retro games explorer view and player. */
#include "app_types.h"

void games_init(AppData* app);
void games_append_rows(ExplorerRow* root, AppData* app);
void games_refresh(AppData* app);
void games_activate_selected(AppData* app);
gchar* games_get_preview_summary(ExplorerRow* row);
gboolean games_check_help_key(AppData* app, guint keyval, GdkModifierType state);
void games_launch(AppData* app, const gchar* game_id);
void games_close_tab(AppData* app, GtkWidget* page);
gboolean games_tab_close_shown(AppData* app);

#endif /* GAMES_H */

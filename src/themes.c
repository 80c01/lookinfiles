/* Theme switching in the running application (see themes.h). */
#include "themes.h"
#include "app_events.h"
#include "minimap.h"
#include "terminal.h"
#include "text_style.h"
#include "text_tags.h"
#include "theme.h"
#include "theme_store.h"

static gchar* current_name = NULL;

const gchar* themes_current_name(void) {
    if (current_name == NULL) {
        gchar* path = theme_store_choice_path();
        current_name = theme_store_load_choice(path);
        g_free(path);
    }
    return current_name;
}

/* Every widget redrawn: drawing areas (gutters, minimaps, workspace
 * thumbnails) read the palette when they draw */
static void queue_draw_tree(GtkWidget* widget) {
    gtk_widget_queue_draw(widget);
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        queue_draw_tree(child);
    }
}

/* Minimap caches of every editor tab, in every workspace */
static void clear_minimap_caches(AppData* app) {
    minimap_cache_clear(&app->mm_cache);
    GHashTable* seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (gint w = -1; w < app->num_workspaces; w++) {
        GList* editors = (w < 0) ? app->editors : app->workspaces[w].editors;
        for (GList* l = editors; l != NULL; l = l->next) {
            EditorTab* tab = l->data;
            if (tab != NULL && g_hash_table_add(seen, tab)) {
                minimap_cache_clear(&tab->mm_cache);
            }
        }
    }
    g_hash_table_destroy(seen);
}

void themes_refresh(AppData* app, gboolean dark) {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings != NULL) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", dark, NULL);
    }
    theme_reload();
    text_tags_refresh_all();
#ifdef HAVE_GSV
    text_style_refresh_scheme(dark);
#endif
#ifdef HAVE_VTE
    terminal_refresh_theme();
#endif
    if (app == NULL) {
        return;
    }
    clear_minimap_caches(app);
    if (app->window != NULL) {
        queue_draw_tree(GTK_WIDGET(app->window));
    }
    /* Rows and previews written with colours: rebuilt */
    app_emit(app, APP_EVENT_GIT_STATUS_CHANGED);
    app_emit(app, APP_EVENT_FILES_CHANGED);
    app_emit(app, APP_EVENT_PREVIEW_STALE);
}

gboolean themes_apply(AppData* app, const gchar* name, gboolean remember, GError** error) {
    gchar* dir = theme_store_user_dir();
    GPtrArray* themes = theme_store_list(dir, NULL);
    g_free(dir);
    gboolean dark = TRUE;
    gboolean ok = theme_store_apply(themes, name, &dark, error);
    g_ptr_array_free(themes, TRUE);
    if (!ok) {
        return FALSE;
    }
    g_free(current_name);
    current_name = g_strdup(name);
    themes_refresh(app, dark);
    if (remember) {
        gchar* path = theme_store_choice_path();
        GError* save_error = NULL;
        if (!theme_store_save_choice(path, name, &save_error)) {
            g_printerr("[ Theme choice not saved: %s ]\n", save_error->message);
            g_clear_error(&save_error);
        }
        g_free(path);
    }
    return TRUE;
}

/* Icon sets (see icon_sets.h).
 *
 * The files of a set are "unthemed" icons of the display's icon theme, found
 * through a search path (user set) or a resource path (built-in set). Their
 * unique lookin-set- names cannot match an icon of the system theme, and
 * icons.c lists them before the default icons in every icon it shows, so
 * changing the paths makes GTK redraw every image. */
#include "icon_sets.h"

#include "app_paths.h"
#include "theme_store.h"

#include <gtk/gtk.h>
#include <string.h>

#define BUILTIN_PREFIX "/org/lookin/iconsets"

static gchar* current_id = NULL;
static gchar* current_search_dir = NULL;     /* search path entry added for the set */
static gchar* current_resource_path = NULL;  /* resource path entry added for the set */

gchar* icon_sets_user_dir(void) {
    gchar* config = app_config_dir();
    gchar* dir = g_build_filename(config, "icons", NULL);
    g_free(config);
    return dir;
}

const gchar* icon_sets_current(void) {
    return current_id;
}

/* The path list without entry (NULL entry: nothing removed), then entry
 * appended when add is TRUE; NULL-terminated, caller frees with g_strfreev() */
static gchar** replace_path(const gchar* const* paths, const gchar* remove, const gchar* add) {
    GPtrArray* out = g_ptr_array_new();
    for (gsize i = 0; paths != NULL && paths[i] != NULL; i++) {
        if (g_strcmp0(paths[i], remove) != 0) {
            g_ptr_array_add(out, g_strdup(paths[i]));
        }
    }
    if (add != NULL) {
        g_ptr_array_add(out, g_strdup(add));
    }
    g_ptr_array_add(out, NULL);
    return (gchar**)g_ptr_array_free(out, FALSE);
}

/* TRUE when dir holds at least one icon file of a set (lookin-set-*.svg) */
static gboolean has_set_files(const gchar* dir) {
    GDir* d = g_dir_open(dir, 0, NULL);
    gboolean found = FALSE;
    const gchar* entry;
    while (!found && d != NULL && (entry = g_dir_read_name(d)) != NULL) {
        found = g_str_has_prefix(entry, "lookin-set-") && g_str_has_suffix(entry, ".svg");
    }
    if (d != NULL) {
        g_dir_close(d);
    }
    return found;
}

static gboolean builtin_exists(const gchar* id) {
    gchar* path = g_strdup_printf("%s/%s", BUILTIN_PREFIX, id);
    gchar** children = g_resources_enumerate_children(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    gboolean found = children != NULL;
    g_strfreev(children);
    g_free(path);
    return found;
}

void icon_sets_use(const gchar* id) {
    if (id != NULL && (strcmp(id, "default") == 0 || !theme_store_icon_set_valid(id))) {
        id = NULL;
    }
    if (g_strcmp0(id, current_id) == 0) {
        return;
    }
    GdkDisplay* display = gdk_display_get_default();
    if (display == NULL) {
        return;
    }
    GtkIconTheme* theme = gtk_icon_theme_get_for_display(display);

    gchar* search_dir = NULL;
    gchar* resource_path = NULL;
    if (id != NULL) {
        gchar* user = icon_sets_user_dir();
        gchar* dir = g_build_filename(user, id, NULL);
        g_free(user);
        if (g_file_test(dir, G_FILE_TEST_IS_DIR) && has_set_files(dir)) {
            search_dir = dir;
        } else {
            if (g_file_test(dir, G_FILE_TEST_IS_DIR)) {
                g_warning("icon set \"%s\": %s has no lookin-set-<icon>.svg file, ignored", id, dir);
            }
            g_free(dir);
            if (builtin_exists(id)) {
                resource_path = g_strdup_printf("%s/%s", BUILTIN_PREFIX, id);
            } else {
                g_warning("icon set \"%s\" not found (built in, or in ~/.config/lookinfiles/icons)", id);
                id = NULL;
            }
        }
    }

    gchar** search = gtk_icon_theme_get_search_path(theme);
    gchar** new_search = replace_path((const gchar* const*)search, current_search_dir, search_dir);
    gtk_icon_theme_set_search_path(theme, (const gchar* const*)new_search);
    g_strfreev(search);
    g_strfreev(new_search);

    gchar** resources = gtk_icon_theme_get_resource_path(theme);
    gchar** new_resources = replace_path((const gchar* const*)resources, current_resource_path, resource_path);
    gtk_icon_theme_set_resource_path(theme, (const gchar* const*)new_resources);
    g_strfreev(resources);
    g_strfreev(new_resources);

    g_free(current_search_dir);
    g_free(current_resource_path);
    g_free(current_id);
    current_search_dir = search_dir;
    current_resource_path = resource_path;
    current_id = g_strdup(id);
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const gchar* const*)a, *(const gchar* const*)b);
}

gchar** icon_sets_list(void) {
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    gchar** builtin = g_resources_enumerate_children(BUILTIN_PREFIX, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    for (gsize i = 0; builtin != NULL && builtin[i] != NULL; i++) {
        gsize len = strlen(builtin[i]);
        if (len > 1 && builtin[i][len - 1] == '/') {
            g_ptr_array_add(names, g_strndup(builtin[i], len - 1));
        }
    }
    g_strfreev(builtin);

    gchar* dir = icon_sets_user_dir();
    GDir* d = g_dir_open(dir, 0, NULL);
    const gchar* entry;
    while (d != NULL && (entry = g_dir_read_name(d)) != NULL) {
        gchar* path = g_build_filename(dir, entry, NULL);
        gboolean known = g_ptr_array_find_with_equal_func(names, entry, g_str_equal, NULL);
        if (!known && g_file_test(path, G_FILE_TEST_IS_DIR) && theme_store_icon_set_valid(entry)) {
            g_ptr_array_add(names, g_strdup(entry));
        }
        g_free(path);
    }
    if (d != NULL) {
        g_dir_close(d);
    }
    g_free(dir);
    g_ptr_array_sort(names, compare_names);
    g_ptr_array_add(names, NULL);
    return (gchar**)g_ptr_array_free(names, FALSE);
}

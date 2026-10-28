/* Filesystem watching: GFileMonitors on expanded directories, the previewed file
 * and the .git directory, with a debounced refresh (schedule_refresh()).
 * Reports changes through app_events only; knows neither the tree nor the viewer. */
#include "fs_watch.h"
#include "app_events.h"
#include "git_utils.h"
#include "git_tab.h"

static gboolean deferred_refresh(gpointer data) {
    AppData* app = (AppData*)data;
    app->refresh_source = 0;

    if (app->dir_dirty) {
        app->dir_dirty = FALSE;
        app->git_dirty = FALSE;
        app_emit(app, APP_EVENT_FILES_CHANGED);
        git_tab_update(app, app->base_dir);
    } else if (app->git_dirty) {
        app->git_dirty = FALSE;
        app_emit(app, APP_EVENT_GIT_STATUS_CHANGED);
        git_tab_update(app, app->base_dir);
    }
    if (app->file_dirty) {
        app->file_dirty = FALSE;
        /* Do not overwrite preview if the viewed file is an open buffer being edited */
        gboolean is_open_buffer = FALSE;
        if (app->viewed_path != NULL) {
            const gchar* cfg = g_get_user_config_dir();
            if (cfg != NULL && g_str_has_prefix(app->viewed_path, cfg)) {
                for (GList* l = app->editors; l != NULL; l = l->next) {
                    EditorTab* tab = (EditorTab*)l->data;
                    if (tab != NULL && g_strcmp0(tab->path, app->viewed_path) == 0) {
                        is_open_buffer = TRUE;
                        break;
                    }
                }
            }
        }
        if (app->viewed_path != NULL && !is_open_buffer) {
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
    }
    return G_SOURCE_REMOVE;
}

void schedule_refresh(AppData* app) {
    if (app->refresh_source == 0) {
        app->refresh_source = g_timeout_add(150, deferred_refresh, app);
    }
}

static void on_dir_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                           GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)other_file;
    AppData* app = (AppData*)data;

    /* Ignore events for .git directory, git internals, and temporary lock/swap files */
    if (file != NULL) {
        gchar* name = g_file_get_basename(file);
        if (name != NULL) {
            if (g_strcmp0(name, ".git") == 0 ||
                g_str_has_suffix(name, ".lock") ||
                g_str_has_suffix(name, ".tmp") ||
                g_str_has_suffix(name, "~")) {
                g_free(name);
                return;
            }
            g_free(name);
        }
        gchar* path = g_file_get_path(file);
        if (path != NULL) {
            if (strstr(path, "/.git/") != NULL || g_str_has_suffix(path, "/.git")) {
                g_free(path);
                return;
            }
            g_free(path);
        }
    }

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
        app->dir_dirty = TRUE;
        schedule_refresh(app);
        break;
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
        app->git_dirty = TRUE;
        if (file != NULL && app->viewed_path != NULL) {
            gchar* p = g_file_get_path(file);
            if (p != NULL && g_strcmp0(p, app->viewed_path) == 0) {
                app->file_dirty = TRUE;
            }
            g_free(p);
        }
        schedule_refresh(app);
        break;
    default:
        break;
    }
}

static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                            GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)other_file;
    AppData* app = (AppData*)data;

    if (file != NULL) {
        gchar* path = g_file_get_path(file);
        if (path != NULL) {
            if (strstr(path, "/.git/") != NULL || g_str_has_suffix(path, "/.git")) {
                g_free(path);
                return;
            }
            g_free(path);
        }
    }

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_DELETED:
        app->file_dirty = TRUE;
        app->git_dirty = TRUE;
        schedule_refresh(app);
        break;
    default:
        break;
    }
}

static void on_git_dir_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                               GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)other_file; (void)event_type;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (file != NULL) {
        gchar* name = g_file_get_basename(file);
        if (name != NULL) {
            /* Ignore temporary lock files created and destroyed during git commands */
            if (g_str_has_suffix(name, ".lock")) {
                g_free(name);
                return;
            }
            g_free(name);
        }
    }
    app->git_dirty = TRUE;
    schedule_refresh(app);
}

static void sync_git_monitor(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* git_root = (app->base_dir != NULL) ? find_git_root(app->base_dir) : NULL;
    if (git_root == NULL) {
        if (app->git_monitor != NULL) {
            g_file_monitor_cancel(app->git_monitor);
            g_object_unref(app->git_monitor);
            app->git_monitor = NULL;
        }
        g_free(app->git_monitored_path);
        app->git_monitored_path = NULL;
        return;
    }

    gchar* git_dir = git_resolve_dir(git_root);
    g_free(git_root);

    if (git_dir == NULL) {
        if (app->git_monitor != NULL) {
            g_file_monitor_cancel(app->git_monitor);
            g_object_unref(app->git_monitor);
            app->git_monitor = NULL;
        }
        g_free(app->git_monitored_path);
        app->git_monitored_path = NULL;
        return;
    }

    if (app->git_monitor != NULL && g_strcmp0(app->git_monitored_path, git_dir) == 0) {
        g_free(git_dir);
        return;
    }

    if (app->git_monitor != NULL) {
        g_file_monitor_cancel(app->git_monitor);
        g_object_unref(app->git_monitor);
        app->git_monitor = NULL;
    }
    g_free(app->git_monitored_path);
    app->git_monitored_path = git_dir;

    GFile* gf = g_file_new_for_path(git_dir);
    GError* err = NULL;
    app->git_monitor = g_file_monitor_directory(gf, G_FILE_MONITOR_NONE, NULL, &err);
    if (err != NULL) {
        g_error_free(err);
        app->git_monitor = NULL;
    } else if (app->git_monitor != NULL) {
        g_signal_connect(app->git_monitor, "changed", G_CALLBACK(on_git_dir_changed), app);
    }
    g_object_unref(gf);
}

static void add_single_dir_monitor(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    if (app->dir_monitors == NULL) {
        app->dir_monitors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    if (g_hash_table_contains(app->dir_monitors, path)) {
        return;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return;
    }
    GFile* f = g_file_new_for_path(path);
    GError* err = NULL;
    GFileMonitor* mon = g_file_monitor_directory(f, G_FILE_MONITOR_NONE, NULL, &err);
    if (err != NULL) {
        g_error_free(err);
    } else if (mon != NULL) {
        g_signal_connect(mon, "changed", G_CALLBACK(on_dir_changed), app);
        g_hash_table_insert(app->dir_monitors, g_strdup(path), mon);
    }
    g_object_unref(f);
}

void stop_directory_monitors(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->git_monitor != NULL) {
        g_file_monitor_cancel(app->git_monitor);
        g_object_unref(app->git_monitor);
        app->git_monitor = NULL;
    }
    g_free(app->git_monitored_path);
    app->git_monitored_path = NULL;

    if (app->dir_monitors == NULL) {
        return;
    }
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, app->dir_monitors);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        if (value != NULL) {
            g_file_monitor_cancel(G_FILE_MONITOR(value));
        }
    }
    g_hash_table_destroy(app->dir_monitors);
    app->dir_monitors = NULL;
}

void sync_directory_monitors(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->dir_monitors == NULL) {
        app->dir_monitors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }

    /* Collect all paths that should currently be monitored:
     * base_dir and all existing expanded subdirectories */
    GHashTable* needed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (app->base_dir != NULL && g_file_test(app->base_dir, G_FILE_TEST_IS_DIR)) {
        g_hash_table_add(needed, g_strdup(app->base_dir));
    }

    if (app->expanded_paths != NULL) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, app->expanded_paths);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            const gchar* p = (const gchar*)key;
            if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
                g_hash_table_add(needed, g_strdup(p));
            } else {
                /* Prune non-existent directory path from expanded_paths */
                g_hash_table_iter_remove(&iter);
            }
        }
    }

    /* Remove monitors that are no longer needed */
    GHashTableIter miter;
    gpointer mkey, mvalue;
    g_hash_table_iter_init(&miter, app->dir_monitors);
    while (g_hash_table_iter_next(&miter, &mkey, &mvalue)) {
        const gchar* mp = (const gchar*)mkey;
        if (!g_hash_table_contains(needed, mp)) {
            if (mvalue != NULL) {
                g_file_monitor_cancel(G_FILE_MONITOR(mvalue));
            }
            g_hash_table_iter_remove(&miter);
        }
    }

    /* Add monitors for any needed paths not yet monitored */
    GHashTableIter niter;
    gpointer nkey, nvalue;
    g_hash_table_iter_init(&niter, needed);
    while (g_hash_table_iter_next(&niter, &nkey, &nvalue)) {
        const gchar* np = (const gchar*)nkey;
        if (!g_hash_table_contains(app->dir_monitors, np)) {
            add_single_dir_monitor(app, np);
        }
    }

    g_hash_table_destroy(needed);
    sync_git_monitor(app);
}

void monitor_directory(AppData* app) {
    sync_directory_monitors(app);
}

void monitor_file(AppData* app, const char* path) {
    if (app->file_monitor != NULL) {
        g_file_monitor_cancel(app->file_monitor);
        g_object_unref(app->file_monitor);
        app->file_monitor = NULL;
    }
    if (path != NULL) {
        GFile* f = g_file_new_for_path(path);
        GError* err = NULL;
        app->file_monitor = g_file_monitor(f, G_FILE_MONITOR_NONE, NULL, &err);
        if (err != NULL) {
            g_error_free(err);
            app->file_monitor = NULL;
        } else {
            g_signal_connect(app->file_monitor, "changed",
                             G_CALLBACK(on_file_changed), app);
        }
        g_object_unref(f);
    }
}

/* Remote machine data: RemoteMachine list in AppData, remotes.xml persistence,
 * ~/.ssh/config discovery and lookups. No widgets (unit-tested in tests/). */
#include "explorer/remote_store.h"
#include "core/app_paths.h"
#include "notebook_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Machines and folders of the remote view, private to this file */
struct RemoteStore {
    GList* machines; /* RemoteMachine*, in display order */
    GList* folders;  /* RemoteFolder*, in display order */
};

static struct RemoteStore* store(AppData* app) {
    if (app->remote_store == NULL) {
        app->remote_store = g_new0(struct RemoteStore, 1);
    }
    return app->remote_store;
}

GList* remote_machine_list(AppData* app) {
    return (app != NULL && app->remote_store != NULL) ? app->remote_store->machines : NULL;
}

GList* remote_folder_list(AppData* app) {
    return (app != NULL && app->remote_store != NULL) ? app->remote_store->folders : NULL;
}

void remote_machine_add(AppData* app, RemoteMachine* rm) {
    store(app)->machines = g_list_append(store(app)->machines, rm);
}

void remote_machine_delete(AppData* app, RemoteMachine* rm) {
    store(app)->machines = g_list_remove(store(app)->machines, rm);
    remote_machine_free(rm);
}

#define REMOTES_XML_FILE "remotes.xml"

void remote_machine_free(RemoteMachine* rm) {
    if (rm == NULL) {
        return;
    }
    g_free(rm->id);
    g_free(rm->name);
    g_free(rm->host);
    g_free(rm->user);
    g_free(rm->remote_path);
    g_free(rm->identity_file);
    g_free(rm->mount_point);
    g_free(rm->folder_id);
    g_free(rm);
}

static void remote_folder_free(RemoteFolder* folder) {
    if (folder == NULL) {
        return;
    }
    g_free(folder->id);
    g_free(folder->name);
    g_free(folder);
}

static gchar* remote_get_config_path(void) {
    gchar* app_dir = app_config_dir();
    g_mkdir_with_parents(app_dir, 0700);
    gchar* file = g_build_filename(app_dir, REMOTES_XML_FILE, NULL);
    g_free(app_dir);
    return file;
}

gchar* remote_get_cache_dir(const gchar* machine_id) {
    gchar* cache_dir = app_cache_dir();
    gchar* dir = g_build_filename(cache_dir, "remotes", machine_id, NULL);
    g_free(cache_dir);
    g_mkdir_with_parents(dir, 0700);
    return dir;
}

RemoteMachine* remote_find_by_id(AppData* app, const gchar* id) {
    if (app == NULL || id == NULL) {
        return NULL;
    }
    for (GList* l = store(app)->machines; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && g_strcmp0(rm->id, id) == 0) {
            return rm;
        }
    }
    return NULL;
}

RemoteFolder* remote_folder_find(AppData* app, const gchar* id) {
    if (app == NULL || id == NULL) {
        return NULL;
    }
    for (GList* l = store(app)->folders; l != NULL; l = l->next) {
        RemoteFolder* folder = (RemoteFolder*)l->data;
        if (folder != NULL && g_strcmp0(folder->id, id) == 0) {
            return folder;
        }
    }
    return NULL;
}

RemoteFolder* remote_machine_folder(AppData* app, const RemoteMachine* rm) {
    return (rm != NULL) ? remote_folder_find(app, rm->folder_id) : NULL;
}

RemoteFolder* remote_folder_add(AppData* app, const gchar* name, const gchar* icon) {
    if (app == NULL) {
        return NULL;
    }
    RemoteFolder* folder = g_new0(RemoteFolder, 1);
    do {
        g_free(folder->id);
        folder->id = g_strdup_printf("rfold_%u", (guint)g_random_int());
    } while (remote_folder_find(app, folder->id) != NULL);
    folder->name = g_strdup((name != NULL && name[0] != '\0') ? name : "Folder");
    folder->icon = bookmark_folder_icon_normalize(icon);
    folder->expanded = TRUE;
    store(app)->folders = g_list_append(store(app)->folders, folder);
    return folder;
}

void remote_folder_remove(AppData* app, RemoteFolder* folder) {
    if (app == NULL || folder == NULL) {
        return;
    }
    for (GList* l = store(app)->machines; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && g_strcmp0(rm->folder_id, folder->id) == 0) {
            remote_machine_set_folder(rm, NULL);
        }
    }
    store(app)->folders = g_list_remove(store(app)->folders, folder);
    remote_folder_free(folder);
}

guint remote_folder_count(AppData* app, const RemoteFolder* folder) {
    guint n = 0;
    if (app == NULL || folder == NULL) {
        return 0;
    }
    for (GList* l = store(app)->machines; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && g_strcmp0(rm->folder_id, folder->id) == 0) {
            n++;
        }
    }
    return n;
}

void remote_machine_set_folder(RemoteMachine* rm, const RemoteFolder* folder) {
    if (rm == NULL) {
        return;
    }
    g_free(rm->folder_id);
    rm->folder_id = (folder != NULL) ? g_strdup(folder->id) : NULL;
}

RemoteMachine* remote_get_active_machine(AppData* app) {
    if (app == NULL || app->current_remote_id == NULL) {
        return NULL;
    }
    return remote_find_by_id(app, app->current_remote_id);
}

/* ------------------------------------------------------------------------- */
/* XML and SSH Config Persistence                                            */
/* ------------------------------------------------------------------------- */

/* <folder id="" name="" icon="" expanded=""/> in remotes.xml */
static void parse_folder_element(AppData* app, const gchar** names, const gchar** values) {
    const gchar* id = NULL;
    const gchar* name = NULL;
    const gchar* icon = NULL;
    const gchar* expanded = NULL;
    for (gint i = 0; names[i] != NULL; i++) {
        if (g_strcmp0(names[i], "id") == 0) id = values[i];
        else if (g_strcmp0(names[i], "name") == 0) name = values[i];
        else if (g_strcmp0(names[i], "icon") == 0) icon = values[i];
        else if (g_strcmp0(names[i], "expanded") == 0) expanded = values[i];
    }
    if (id == NULL || id[0] == '\0' || remote_folder_find(app, id) != NULL) {
        return;
    }
    RemoteFolder* folder = g_new0(RemoteFolder, 1);
    folder->id = g_strdup(id);
    folder->name = g_strdup((name != NULL && name[0] != '\0') ? name : "Folder");
    folder->icon = bookmark_folder_icon_normalize(icon);
    folder->expanded = g_strcmp0(expanded, "false") != 0;
    store(app)->folders = g_list_append(store(app)->folders, folder);
}

/* <machine id="" name="" host="" user="" port="" path="" key="" folder=""/> in remotes.xml */
static void on_remotes_xml_start(GMarkupParseContext* ctx, const gchar* element,
                                 const gchar** names, const gchar** values,
                                 gpointer user_data, GError** error) {
    (void)ctx;
    (void)error;
    AppData* app = (AppData*)user_data;
    if (g_strcmp0(element, "folder") == 0) {
        parse_folder_element(app, names, values);
        return;
    }
    if (g_strcmp0(element, "machine") != 0) {
        return;
    }
    const gchar* id = NULL;
    const gchar* name = NULL;
    const gchar* host = NULL;
    const gchar* user = NULL;
    const gchar* port_str = NULL;
    const gchar* remote_path = NULL;
    const gchar* key = NULL;
    const gchar* folder_id = NULL;
    for (gint i = 0; names[i] != NULL; i++) {
        if (g_strcmp0(names[i], "id") == 0) id = values[i];
        else if (g_strcmp0(names[i], "name") == 0) name = values[i];
        else if (g_strcmp0(names[i], "host") == 0) host = values[i];
        else if (g_strcmp0(names[i], "user") == 0) user = values[i];
        else if (g_strcmp0(names[i], "port") == 0) port_str = values[i];
        else if (g_strcmp0(names[i], "path") == 0) remote_path = values[i];
        else if (g_strcmp0(names[i], "key") == 0) key = values[i];
        else if (g_strcmp0(names[i], "folder") == 0) folder_id = values[i];
    }
    if (host == NULL || host[0] == '\0') {
        return;
    }
    RemoteMachine* rm = g_new0(RemoteMachine, 1);
    rm->id = id ? g_strdup(id) : g_strdup_printf("rem_%u", (guint)g_random_int());
    rm->name = g_strdup(name ? name : host);
    rm->host = g_strdup(host);
    rm->user = g_strdup(user ? user : "");
    rm->port = (port_str && atoi(port_str) > 0) ? atoi(port_str) : 22;
    rm->remote_path = g_strdup(remote_path ? remote_path : "~");
    rm->identity_file = g_strdup(key ? key : "");
    rm->from_ssh_config = FALSE;
    rm->is_connected = FALSE;
    rm->mount_point = NULL;
    rm->folder_id = (folder_id != NULL && folder_id[0] != '\0') ? g_strdup(folder_id) : NULL;
    store(app)->machines = g_list_append(store(app)->machines, rm);
}

/* Append the machines saved in remotes.xml to the machine list (entities decoded) */
static void parse_remotes_xml(AppData* app, const gchar* path) {
    gchar* contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL) || contents == NULL) {
        return;
    }
    static const GMarkupParser parser = {on_remotes_xml_start, NULL, NULL, NULL, NULL};
    GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, app, NULL);
    GError* err = NULL;
    if (!g_markup_parse_context_parse(ctx, contents, (gssize)length, &err) ||
        !g_markup_parse_context_end_parse(ctx, &err)) {
        g_printerr("[ remotes.xml: %s ]\n", err != NULL ? err->message : "parse error");
        g_clear_error(&err);
    }
    g_markup_parse_context_free(ctx);
    g_free(contents);
}

static void parse_ssh_config(AppData* app) {
    const gchar* home = g_get_home_dir();
    if (home == NULL) {
        return;
    }
    gchar* cfg_path = g_build_filename(home, ".ssh", "config", NULL);
    gchar* contents = NULL;
    if (!g_file_get_contents(cfg_path, &contents, NULL, NULL) || contents == NULL) {
        g_free(cfg_path);
        return;
    }
    g_free(cfg_path);

    gchar** lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    if (lines == NULL) {
        return;
    }

    RemoteMachine* cur = NULL;

    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strstrip(lines[i]);
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        gchar** tokens = g_strsplit_set(line, " \t=", -1);
        if (tokens == NULL || tokens[0] == NULL) {
            g_strfreev(tokens);
            continue;
        }

        /* Filter non-empty tokens */
        GPtrArray* clean = g_ptr_array_new();
        for (gint t = 0; tokens[t] != NULL; t++) {
            if (tokens[t][0] != '\0') {
                g_ptr_array_add(clean, tokens[t]);
            }
        }

        if (clean->len >= 2) {
            const gchar* key = (const gchar*)g_ptr_array_index(clean, 0);
            const gchar* val = (const gchar*)g_ptr_array_index(clean, 1);

            if (g_ascii_strcasecmp(key, "Host") == 0) {
                if (cur != NULL && cur->host != NULL && cur->host[0] != '\0') {
                    /* Check if already present */
                    gboolean exists = FALSE;
                    for (GList* l = store(app)->machines; l != NULL; l = l->next) {
                        RemoteMachine* existing = (RemoteMachine*)l->data;
                        if (existing != NULL && (g_strcmp0(existing->name, cur->name) == 0 ||
                                                 g_strcmp0(existing->host, cur->host) == 0)) {
                            exists = TRUE;
                            break;
                        }
                    }
                    if (!exists) {
                        store(app)->machines = g_list_append(store(app)->machines, cur);
                    } else {
                        remote_machine_free(cur);
                    }
                    cur = NULL;
                } else if (cur != NULL) {
                    remote_machine_free(cur);
                    cur = NULL;
                }

                /* Ignore wildcard host patterns */
                if (strchr(val, '*') == NULL && strchr(val, '?') == NULL) {
                    cur = g_new0(RemoteMachine, 1);
                    cur->id = g_strdup_printf("ssh_%s", val);
                    cur->name = g_strdup(val);
                    cur->host = g_strdup(val);
                    cur->user = g_strdup("");
                    cur->port = 22;
                    cur->remote_path = g_strdup("~");
                    cur->identity_file = g_strdup("");
                    cur->from_ssh_config = TRUE;
                }
            } else if (cur != NULL) {
                if (g_ascii_strcasecmp(key, "HostName") == 0) {
                    g_free(cur->host);
                    cur->host = g_strdup(val);
                } else if (g_ascii_strcasecmp(key, "User") == 0) {
                    g_free(cur->user);
                    cur->user = g_strdup(val);
                } else if (g_ascii_strcasecmp(key, "Port") == 0) {
                    gint p = atoi(val);
                    if (p > 0) cur->port = p;
                } else if (g_ascii_strcasecmp(key, "IdentityFile") == 0) {
                    g_free(cur->identity_file);
                    cur->identity_file = g_strdup(val);
                }
            }
        }
        g_ptr_array_free(clean, TRUE);
        g_strfreev(tokens);
    }

    if (cur != NULL && cur->host != NULL && cur->host[0] != '\0') {
        gboolean exists = FALSE;
        for (GList* l = store(app)->machines; l != NULL; l = l->next) {
            RemoteMachine* existing = (RemoteMachine*)l->data;
            if (existing != NULL && (g_strcmp0(existing->name, cur->name) == 0 ||
                                     g_strcmp0(existing->host, cur->host) == 0)) {
                exists = TRUE;
                break;
            }
        }
        if (!exists) {
            store(app)->machines = g_list_append(store(app)->machines, cur);
        } else {
            remote_machine_free(cur);
        }
    } else if (cur != NULL) {
        remote_machine_free(cur);
    }

    g_strfreev(lines);
}

/* Drop the ~/.ssh/config machines that are not connected, then parse the file
 * again: new hosts appear, removed ones disappear, connected ones are kept. */
void remote_reload_ssh_config(AppData* app) {
    if (app == NULL) {
        return;
    }
    GList* l = store(app)->machines;
    while (l != NULL) {
        GList* next = l->next;
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && rm->from_ssh_config && !rm->is_connected) {
            remote_machine_free(rm);
            store(app)->machines = g_list_delete_link(store(app)->machines, l);
        }
        l = next;
    }
    parse_ssh_config(app);
}

void remote_init(AppData* app) {
    if (app == NULL) {
        return;
    }
    app->current_remote_id = NULL;

    gchar* cfg = remote_get_config_path();
    parse_remotes_xml(app, cfg);
    g_free(cfg);

    parse_ssh_config(app);
}

void remote_free(AppData* app) {
    if (app == NULL) {
        return;
    }
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL) {
            if (rm->is_connected && rm->mount_point != NULL) {
                /* Clean unmount */
                gchar* cmd = g_strdup_printf("fusermount3 -u \"%s\" 2>/dev/null || fusermount -u \"%s\" 2>/dev/null",
                                             rm->mount_point, rm->mount_point);
                int ignored = system(cmd);
                (void)ignored;
                g_free(cmd);
            }
            remote_machine_free(rm);
        }
    }
    if (app->remote_store != NULL) {
        g_list_free(app->remote_store->machines);
        g_list_free_full(app->remote_store->folders, (GDestroyNotify)remote_folder_free);
        g_clear_pointer(&app->remote_store, g_free);
    }
    g_free(app->current_remote_id);
    app->current_remote_id = NULL;
}

void remote_save(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* path = remote_get_config_path();
    GString* xml = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<remotes>\n");

    for (GList* l = store(app)->folders; l != NULL; l = l->next) {
        RemoteFolder* folder = (RemoteFolder*)l->data;
        if (folder == NULL) {
            continue;
        }
        gchar* line = g_markup_printf_escaped("  <folder id=\"%s\" name=\"%s\" icon=\"%s\" expanded=\"%s\"/>\n",
                                              folder->id, folder->name, folder->icon,
                                              folder->expanded ? "true" : "false");
        g_string_append(xml, line);
        g_free(line);
    }

    for (GList* l = store(app)->machines; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm == NULL || rm->from_ssh_config) {
            continue; /* Keep ~/.ssh/config hosts separate from custom saved machines */
        }
        gchar* esc_name = g_markup_escape_text(rm->name ? rm->name : "", -1);
        gchar* esc_host = g_markup_escape_text(rm->host ? rm->host : "", -1);
        gchar* esc_user = g_markup_escape_text(rm->user ? rm->user : "", -1);
        gchar* esc_path = g_markup_escape_text(rm->remote_path ? rm->remote_path : "", -1);
        gchar* esc_key  = g_markup_escape_text(rm->identity_file ? rm->identity_file : "", -1);
        RemoteFolder* folder = remote_machine_folder(app, rm);
        gchar* esc_folder = g_markup_escape_text(folder != NULL ? folder->id : "", -1);

        g_string_append_printf(xml, "  <machine id=\"%s\" name=\"%s\" host=\"%s\" user=\"%s\" port=\"%d\" path=\"%s\" key=\"%s\" folder=\"%s\"/>\n",
                               rm->id ? rm->id : "",
                               esc_name, esc_host, esc_user,
                               rm->port, esc_path, esc_key, esc_folder);
        g_free(esc_folder);
        g_free(esc_name);
        g_free(esc_host);
        g_free(esc_user);
        g_free(esc_path);
        g_free(esc_key);
    }
    g_string_append(xml, "</remotes>\n");
    g_file_set_contents(path, xml->str, (gssize)xml->len, NULL);
    g_string_free(xml, TRUE);
    g_free(path);
}

/* ------------------------------------------------------------------------- */
/* TreeView Rendering & Refresh                                              */

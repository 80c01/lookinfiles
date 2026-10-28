/* Remote machine data: RemoteMachine list in AppData, remotes.xml persistence,
 * ~/.ssh/config discovery and lookups. No widgets (unit-tested in tests/). */
#include "remote_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    g_free(rm);
}

static gchar* remote_get_config_path(void) {
    const gchar* cfg_dir = g_get_user_config_dir();
    gchar* app_dir = g_build_filename(cfg_dir, "explorateur_fichiers", NULL);
    g_mkdir_with_parents(app_dir, 0700);
    gchar* file = g_build_filename(app_dir, REMOTES_XML_FILE, NULL);
    g_free(app_dir);
    return file;
}

gchar* remote_get_cache_dir(const gchar* machine_id) {
    const gchar* cache_dir = g_get_user_cache_dir();
    gchar* dir = g_build_filename(cache_dir, "explorateur_fichiers", "remotes", machine_id, NULL);
    g_mkdir_with_parents(dir, 0700);
    return dir;
}

RemoteMachine* remote_find_by_id(AppData* app, const gchar* id) {
    if (app == NULL || id == NULL) {
        return NULL;
    }
    for (GList* l = app->remotes; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && g_strcmp0(rm->id, id) == 0) {
            return rm;
        }
    }
    return NULL;
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

static void parse_remotes_xml(AppData* app, const gchar* path) {
    gchar* contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL) || contents == NULL) {
        return;
    }

    gchar** lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    if (lines == NULL) {
        return;
    }

    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strstrip(lines[i]);
        if (!g_str_has_prefix(line, "<machine")) {
            continue;
        }

        /* Extract attributes */
        gchar* id = NULL;
        gchar* name = NULL;
        gchar* host = NULL;
        gchar* user = NULL;
        gchar* port_str = NULL;
        gchar* remote_path = NULL;
        gchar* key = NULL;

        const gchar* p = line;
        while ((p = strstr(p, "=\"")) != NULL) {
            /* Find attribute name before =" */
            const gchar* attr_end = p;
            const gchar* attr_start = attr_end - 1;
            while (attr_start > line && *attr_start != ' ') {
                attr_start--;
            }
            if (*attr_start == ' ') {
                attr_start++;
            }
            gchar* attr_name = g_strndup(attr_start, attr_end - attr_start);

            /* Find value */
            const gchar* val_start = p + 2;
            const gchar* val_end = strchr(val_start, '"');
            if (val_end != NULL) {
                gchar* val = g_strndup(val_start, val_end - val_start);
                if (g_strcmp0(attr_name, "id") == 0) id = val;
                else if (g_strcmp0(attr_name, "name") == 0) name = val;
                else if (g_strcmp0(attr_name, "host") == 0) host = val;
                else if (g_strcmp0(attr_name, "user") == 0) user = val;
                else if (g_strcmp0(attr_name, "port") == 0) port_str = val;
                else if (g_strcmp0(attr_name, "path") == 0) remote_path = val;
                else if (g_strcmp0(attr_name, "key") == 0) key = val;
                else g_free(val);
                p = val_end + 1;
            } else {
                p = p + 2;
            }
            g_free(attr_name);
        }

        if (host != NULL && host[0] != '\0') {
            RemoteMachine* rm = g_new0(RemoteMachine, 1);
            rm->id = id ? id : g_strdup_printf("rem_%u", (guint)g_random_int());
            rm->name = name ? name : g_strdup(host);
            rm->host = host;
            rm->user = user ? user : g_strdup("");
            rm->port = (port_str && atoi(port_str) > 0) ? atoi(port_str) : 22;
            rm->remote_path = remote_path ? remote_path : g_strdup("~");
            rm->identity_file = key ? key : g_strdup("");
            rm->from_ssh_config = FALSE;
            rm->is_connected = FALSE;
            rm->mount_point = NULL;
            app->remotes = g_list_append(app->remotes, rm);
        } else {
            g_free(id);
            g_free(name);
            g_free(host);
            g_free(user);
            g_free(remote_path);
            g_free(key);
        }
        g_free(port_str);
    }
    g_strfreev(lines);
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
                    for (GList* l = app->remotes; l != NULL; l = l->next) {
                        RemoteMachine* existing = (RemoteMachine*)l->data;
                        if (existing != NULL && (g_strcmp0(existing->name, cur->name) == 0 ||
                                                 g_strcmp0(existing->host, cur->host) == 0)) {
                            exists = TRUE;
                            break;
                        }
                    }
                    if (!exists) {
                        app->remotes = g_list_append(app->remotes, cur);
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
        for (GList* l = app->remotes; l != NULL; l = l->next) {
            RemoteMachine* existing = (RemoteMachine*)l->data;
            if (existing != NULL && (g_strcmp0(existing->name, cur->name) == 0 ||
                                     g_strcmp0(existing->host, cur->host) == 0)) {
                exists = TRUE;
                break;
            }
        }
        if (!exists) {
            app->remotes = g_list_append(app->remotes, cur);
        } else {
            remote_machine_free(cur);
        }
    } else if (cur != NULL) {
        remote_machine_free(cur);
    }

    g_strfreev(lines);
}

void remote_init(AppData* app) {
    if (app == NULL) {
        return;
    }
    app->remotes = NULL;
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
    for (GList* l = app->remotes; l != NULL; l = l->next) {
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
    g_list_free(app->remotes);
    app->remotes = NULL;
    g_free(app->current_remote_id);
    app->current_remote_id = NULL;
}

void remote_save(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* path = remote_get_config_path();
    GString* xml = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<remotes>\n");

    for (GList* l = app->remotes; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm == NULL || rm->from_ssh_config) {
            continue; /* Keep ~/.ssh/config hosts separate from custom saved machines */
        }
        gchar* esc_name = g_markup_escape_text(rm->name ? rm->name : "", -1);
        gchar* esc_host = g_markup_escape_text(rm->host ? rm->host : "", -1);
        gchar* esc_user = g_markup_escape_text(rm->user ? rm->user : "", -1);
        gchar* esc_path = g_markup_escape_text(rm->remote_path ? rm->remote_path : "", -1);
        gchar* esc_key  = g_markup_escape_text(rm->identity_file ? rm->identity_file : "", -1);

        g_string_append_printf(xml, "  <machine id=\"%s\" name=\"%s\" host=\"%s\" user=\"%s\" port=\"%d\" path=\"%s\" key=\"%s\"/>\n",
                               rm->id ? rm->id : "",
                               esc_name, esc_host, esc_user,
                               rm->port, esc_path, esc_key);
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

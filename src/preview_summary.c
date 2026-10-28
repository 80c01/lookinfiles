/* Text summaries for explorer rows without file content (see preview_summary.h). */
#include "preview_summary.h"

#include <string.h>

#define FIELD_WIDTH 14 /* label column of "Label: value" lines */
#define KEY_WIDTH   26 /* key column of the shortcut lists */
#define ITEM_NAME_MAX    40 /* name column of item lists (longer names are not padded) */

typedef struct {
    const gchar* keys;
    const gchar* action;
} SummaryShortcut;

/* Append text padded with spaces to width characters */
static void append_padded(GString* s, const gchar* text, gint width) {
    g_string_append(s, text);
    for (glong n = g_utf8_strlen(text, -1); n < width; n++) {
        g_string_append_c(s, ' ');
    }
}

static void append_title(GString* s, const gchar* title) {
    g_string_append_printf(s, "%s\n", title);
    for (glong n = g_utf8_strlen(title, -1); n > 0; n--) {
        g_string_append_c(s, '=');
    }
    g_string_append(s, "\n\n");
}

/* "  Label:        value" */
static void append_field(GString* s, const gchar* label, const gchar* value) {
    gchar* with_colon = g_strdup_printf("%s:", label);
    g_string_append(s, "  ");
    append_padded(s, with_colon, FIELD_WIDTH);
    g_string_append_printf(s, " %s\n", value);
    g_free(with_colon);
}

static void append_shortcuts(GString* s, const SummaryShortcut* list, gsize count) {
    g_string_append(s, "\nShortcuts:\n");
    for (gsize i = 0; i < count; i++) {
        g_string_append(s, "  • ");
        append_padded(s, list[i].keys, KEY_WIDTH);
        g_string_append_printf(s, " %s\n", list[i].action);
    }
}

/* Plain text of a Pango markup badge ("" when empty or invalid) */
static gchar* markup_to_text(const gchar* markup) {
    gchar* text = NULL;
    if (markup == NULL || markup[0] == '\0' ||
        !pango_parse_markup(markup, -1, 0, NULL, &text, NULL, NULL)) {
        return g_strdup("");
    }
    return text;
}

gchar* preview_summary_remote(const RemoteMachine* rm) {
    if (rm == NULL) {
        return NULL;
    }
    static const SummaryShortcut keys[] = {
        {"Enter / c / Double-click", "Connect and browse remote files in Explorer"},
        {"t", "Open SSH terminal to this machine"},
        {"F2 / r / e", "Edit machine configuration"},
        {"d / Delete", "Remove machine"},
        {"a", "Add new remote machine"},
        {"F5", "Re-scan ~/.ssh/config"},
    };
    GString* s = g_string_new(NULL);
    gchar* title = g_strdup_printf("🌐 Remote Machine: %s", rm->name ? rm->name : "Unknown");
    append_title(s, title);
    g_free(title);

    append_field(s, "Status", rm->is_connected ? "🟢 Connected (Mounted via SSHFS)" : "⚪ Disconnected");
    append_field(s, "Host", rm->host ? rm->host : "(none)");
    append_field(s, "User", (rm->user && *rm->user) ? rm->user : "(default)");
    gchar* port = g_strdup_printf("%d", rm->port);
    append_field(s, "Port", port);
    g_free(port);
    append_field(s, "Remote Path", rm->remote_path ? rm->remote_path : "~");
    if (rm->identity_file && *rm->identity_file) {
        append_field(s, "Identity Key", rm->identity_file);
    }
    if (rm->is_connected && rm->mount_point) {
        append_field(s, "Mount Point", rm->mount_point);
    }
    append_field(s, "Source", rm->from_ssh_config ? "Auto-discovered from ~/.ssh/config"
                                                  : "Configured manually");
    append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    return g_string_free(s, FALSE);
}

/* Human-readable note format from the note row badge ("rich", "org" or empty) */
static const gchar* note_format_name(const gchar* badge_text) {
    if (g_strcmp0(badge_text, "rich") == 0) {
        return "Rich Format";
    }
    if (g_strcmp0(badge_text, "org") == 0) {
        return "Org-mode";
    }
    return "Plain Text";
}

typedef struct {
    gchar* name;
    gchar* detail;
} SummaryItem;

static void summary_item_free(gpointer data) {
    SummaryItem* item = (SummaryItem*)data;
    g_free(item->name);
    g_free(item->detail);
    g_free(item);
}

gchar* preview_summary_container(GtkTreeModel* model, GtkTreeIter* iter) {
    if (model == NULL || iter == NULL) {
        return NULL;
    }
    gchar* type = NULL;
    gchar* name = NULL;
    gchar* path = NULL;
    gtk_tree_model_get(model, iter, COL_TYPE, &type, COL_NAME, &name, COL_PATH, &path, -1);

    gboolean is_notebook = (g_strcmp0(type, "notebook") == 0);
    gboolean is_bm_folder = (g_strcmp0(type, "bm_folder") == 0);
    gboolean is_trash = (g_strcmp0(type, "trash") == 0);
    if (!is_notebook && !is_bm_folder && !is_trash) {
        g_free(type);
        g_free(name);
        g_free(path);
        return NULL;
    }

    /* Collect the children, skipping placeholder rows (type "-") */
    GPtrArray* items = g_ptr_array_new_with_free_func(summary_item_free);
    glong name_width = 0;
    GtkTreeIter child;
    gboolean ok = gtk_tree_model_iter_children(model, &child, iter);
    while (ok) {
        gchar* c_type = NULL;
        gchar* c_name = NULL;
        gchar* c_path = NULL;
        gchar* c_badge = NULL;
        gtk_tree_model_get(model, &child, COL_TYPE, &c_type, COL_NAME, &c_name,
                           COL_PATH, &c_path, COL_GIT_STATUS, &c_badge, -1);
        if (g_strcmp0(c_type, "-") != 0 && c_name != NULL) {
            SummaryItem* item = g_new0(SummaryItem, 1);
            gchar* badge_text = markup_to_text(c_badge);
            if (g_strcmp0(c_type, "bm_folder") == 0) {
                item->name = g_strdup_printf("%s/", c_name);
                gint n = gtk_tree_model_iter_n_children(model, &child);
                item->detail = g_strdup_printf("folder, %d item%s", n, n == 1 ? "" : "s");
            } else if (g_strcmp0(c_type, "bookmark") == 0) {
                item->name = g_strdup(c_name);
                item->detail = g_strdup(c_path ? c_path : "");
            } else if (g_strcmp0(c_type, "note") == 0) {
                item->name = g_strdup(c_name);
                item->detail = g_strdup(note_format_name(badge_text));
            } else {
                item->name = g_strdup(c_name);
                item->detail = g_strdup(badge_text); /* e.g. "from General" */
            }
            g_free(badge_text);
            name_width = MAX(name_width, MIN(g_utf8_strlen(item->name, -1), ITEM_NAME_MAX));
            g_ptr_array_add(items, item);
        }
        g_free(c_type);
        g_free(c_name);
        g_free(c_path);
        g_free(c_badge);
        ok = gtk_tree_model_iter_next(model, &child);
    }

    GString* s = g_string_new(NULL);
    gchar* count = g_strdup_printf("%u", items->len);
    const gchar* list_title = NULL;
    if (is_notebook) {
        gchar* title = g_strdup_printf("📓 Notebook: %s", name ? name : "");
        append_title(s, title);
        g_free(title);
        append_field(s, "Notes", count);
        if (path != NULL && path[0] != '\0') {
            append_field(s, "Location", path);
        }
        list_title = "Notes";
    } else if (is_bm_folder) {
        gchar* title = g_strdup_printf("🔖 Bookmark Folder: %s", name ? name : "");
        append_title(s, title);
        g_free(title);
        append_field(s, "Items", count);
        list_title = "Contents";
    } else {
        append_title(s, "🗑 Trash");
        append_field(s, "Notes", count);
        if (path != NULL && path[0] != '\0') {
            append_field(s, "Location", path);
        }
        list_title = "Trashed notes";
    }
    g_free(count);

    if (items->len > 0) {
        g_string_append_printf(s, "\n%s:\n", list_title);
        for (guint i = 0; i < items->len; i++) {
            SummaryItem* item = (SummaryItem*)g_ptr_array_index(items, i);
            g_string_append(s, "  • ");
            append_padded(s, item->name, (gint)name_width);
            if (item->detail != NULL && item->detail[0] != '\0') {
                g_string_append_printf(s, "   %s", item->detail);
            }
            g_string_append_c(s, '\n');
        }
    } else {
        g_string_append(s, "\n(empty)\n");
    }

    if (is_notebook) {
        static const SummaryShortcut keys[] = {
            {"a / Shift+A", "New note / new notebook"},
            {"F2 / r / e", "Edit notebook (name and icon)"},
            {"i", "Change notebook icon"},
            {"d / Delete", "Delete notebook"},
            {"Shift+K / Shift+J", "Move notebook up / down"},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    } else if (is_bm_folder) {
        static const SummaryShortcut keys[] = {
            {"Enter / l", "Expand / collapse folder"},
            {"a / Shift+A", "Add bookmark / new folder here"},
            {"F2 / r / e", "Edit folder"},
            {"d / Delete", "Delete folder"},
            {"Shift+K / Shift+J", "Move folder up / down"},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    } else {
        static const SummaryShortcut keys[] = {
            {"Shift+R", "Restore the selected trashed note to its notebook"},
            {"d / Delete", "Delete the selected trashed note permanently"},
            {"Trash toolbar button", "Empty the trash"},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    }

    g_ptr_array_free(items, TRUE);
    g_free(type);
    g_free(name);
    g_free(path);
    return g_string_free(s, FALSE);
}

/* Text summaries for explorer rows without file content (see preview_summary.h). */
#include "preview_summary.h"
#include "core/i18n.h"
#include "note_types.h"
#include "canvas/canvas_doc.h"

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
    /* TRANSLATORS: a field label followed by its value, "Host: example.org" */
    gchar* with_colon = g_strdup_printf(_("%s:"), label);
    g_string_append(s, "  ");
    append_padded(s, with_colon, FIELD_WIDTH);
    g_string_append_printf(s, " %s\n", value);
    g_free(with_colon);
}

static void append_shortcuts(GString* s, const SummaryShortcut* list, gsize count) {
    g_string_append_printf(s, "\n%s\n", _("Shortcuts:"));
    for (gsize i = 0; i < count; i++) {
        g_string_append(s, "  • ");
        append_padded(s, g_dpgettext2(GETTEXT_PACKAGE, "key", list[i].keys), KEY_WIDTH);
        g_string_append_printf(s, " %s\n", _(list[i].action));
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
        {NC_("key", "Enter / c / Double-click"), N_("Connect and browse remote files in Explorer")},
        {"t", N_("Open SSH terminal to this machine")},
        {"F2 / r / e", N_("Edit machine configuration")},
        {"d / Delete", N_("Remove machine")},
        {"a", N_("Add new remote machine")},
        {"F5", N_("Re-scan ~/.ssh/config")},
    };
    GString* s = g_string_new(NULL);
    gchar* title = g_strdup_printf(_("Remote Machine: %s"), rm->name ? rm->name : _("Unknown"));
    append_title(s, title);
    g_free(title);

    append_field(s, _("Status"), rm->is_connected ? _("Connected (Mounted via SSHFS)") : _("Disconnected"));
    append_field(s, _("Host"), rm->host ? rm->host : _("(none)"));
    append_field(s, _("User"), (rm->user && *rm->user) ? rm->user : _("(default)"));
    gchar* port = g_strdup_printf("%d", rm->port);
    append_field(s, _("Port"), port);
    g_free(port);
    append_field(s, _("Remote Path"), rm->remote_path ? rm->remote_path : "~");
    if (rm->identity_file && *rm->identity_file) {
        append_field(s, _("Identity Key"), rm->identity_file);
    }
    if (rm->is_connected && rm->mount_point) {
        append_field(s, _("Mount Point"), rm->mount_point);
    }
    append_field(s, _("Source"), rm->from_ssh_config ? _("Auto-discovered from ~/.ssh/config")
                                                  : _("Configured manually"));
    append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    return g_string_free(s, FALSE);
}

/* Human-readable note format from the note row badge ("md", "org" or empty) */
static const gchar* note_format_name(const gchar* badge_text) {
    return note_type_name(note_type_from_badge(badge_text));
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

/* Children of a row, placeholders (type "-") excluded */
static guint count_items(ExplorerRow* row) {
    guint n = 0;
    for (guint i = 0; i < explorer_row_n_children(row); i++) {
        if (!explorer_row_is(explorer_row_child(row, i), "-")) {
            n++;
        }
    }
    return n;
}

gchar* preview_summary_canvas(const gchar* name, const gchar* path) {
    if (path == NULL || note_type_from_filename(path) != NOTE_TYPE_CANVAS ||
        !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return NULL;
    }
    static const SummaryShortcut keys[] = {
        {"Enter", N_("Edit the canvas (Canvas tab)")},
        {"F2 / r / e", N_("Rename the note")},
        {"m", N_("Move the note to another notebook")},
        {"d / Delete", N_("Move the note to the trash")},
    };
    GString* s = g_string_new(NULL);
    gchar* title = g_strdup_printf(_("Canvas: %s"), name != NULL ? name : "");
    append_title(s, title);
    g_free(title);

    /* Bounded read: a larger page is only opened in the Canvas tab */
    gchar* page = g_build_filename(path, CANVAS_PAGE_FILE, NULL);
    GStatBuf st;
    CanvasDoc* doc = NULL;
    gboolean too_large = g_stat(page, &st) == 0 && st.st_size > PREVIEW_BYTES;
    if (!too_large) {
        doc = canvas_doc_load(path, NULL);
    }
    g_free(page);
    if (doc != NULL) {
        guint texts = 0, pictures = 0, shapes = 0, tables = 0;
        for (guint i = 0; i < doc->blocks->len; i++) {
            const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
            if (b->kind == CANVAS_BLOCK_TEXT) {
                texts++;
            } else if (b->kind == CANVAS_BLOCK_IMAGE) {
                pictures++;
            } else if (b->kind == CANVAS_BLOCK_SHAPE) {
                shapes++;
            } else if (b->kind == CANVAS_BLOCK_TABLE) {
                tables++;
            }
        }
        gchar* n = g_strdup_printf("%u", texts);
        append_field(s, _("Text boxes"), n);
        g_free(n);
        n = g_strdup_printf("%u", pictures);
        append_field(s, _("Pictures"), n);
        g_free(n);
        if (shapes > 0) {
            n = g_strdup_printf("%u", shapes);
            append_field(s, _("Shapes"), n);
            g_free(n);
        }
        if (tables > 0) {
            n = g_strdup_printf("%u", tables);
            append_field(s, _("Tables"), n);
            g_free(n);
        }
    }
    append_field(s, _("Location"), path);
    if (doc != NULL) {
        gchar* md = canvas_doc_to_markdown(doc, NULL);
        g_string_append_printf(s, "\n%s", md[0] != '\0' ? md : _("(empty)\n"));
        g_free(md);
        canvas_doc_free(doc);
    } else {
        g_string_append(s, too_large ? _("\n(too large to preview: Enter opens it)\n") : _("\n(unreadable page)\n"));
    }
    append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    return g_string_free(s, FALSE);
}

gchar* preview_summary_container(ExplorerRow* row) {
    if (row == NULL) {
        return NULL;
    }
    const gchar* name = row->name;
    const gchar* path = row->path;
    gchar* canvas = preview_summary_canvas(name, path);
    if (canvas != NULL) {
        return canvas;
    }
    gboolean is_notebook = explorer_row_is(row, "notebook");
    gboolean is_bm_folder = explorer_row_is(row, "bm_folder");
    gboolean is_trash = explorer_row_is(row, "trash");
    gboolean is_remote_folder = explorer_row_is(row, "remote_folder");
    if (!is_notebook && !is_bm_folder && !is_trash && !is_remote_folder) {
        return NULL;
    }

    /* Collect the children, skipping placeholder rows (type "-") */
    GPtrArray* items = g_ptr_array_new_with_free_func(summary_item_free);
    glong name_width = 0;
    for (guint i = 0; i < explorer_row_n_children(row); i++) {
        ExplorerRow* child = explorer_row_child(row, i);
        if (explorer_row_is(child, "-")) {
            continue;
        }
        SummaryItem* item = g_new0(SummaryItem, 1);
        gchar* badge_text = markup_to_text(child->badge);
        if (explorer_row_is(child, "bm_folder")) {
            item->name = g_strdup_printf("%s/", child->name);
            guint n = count_items(child);
            item->detail = g_strdup_printf(ngettext("folder, %u item", "folder, %u items", n), n);
        } else if (explorer_row_is(child, "bookmark")) {
            item->name = g_strdup(child->name);
            item->detail = g_strdup(child->path);
        } else if (explorer_row_is(child, "note")) {
            item->name = g_strdup(child->name);
            item->detail = g_strdup(note_format_name(badge_text));
        } else {
            item->name = g_strdup(child->name);
            item->detail = g_strdup(badge_text); /* e.g. "from General" */
        }
        g_free(badge_text);
        name_width = MAX(name_width, MIN(g_utf8_strlen(item->name, -1), ITEM_NAME_MAX));
        g_ptr_array_add(items, item);
    }

    GString* s = g_string_new(NULL);
    gchar* count = g_strdup_printf("%u", items->len);
    const gchar* list_title = NULL;
    if (is_notebook) {
        gchar* title = g_strdup_printf(_("Notebook: %s"), name ? name : "");
        append_title(s, title);
        g_free(title);
        append_field(s, _("Notes"), count);
        if (path != NULL && path[0] != '\0') {
            append_field(s, _("Location"), path);
        }
        list_title = _("Notes");
    } else if (is_bm_folder) {
        gchar* title = g_strdup_printf(_("Bookmark Folder: %s"), name ? name : "");
        append_title(s, title);
        g_free(title);
        append_field(s, _("Items"), count);
        list_title = _("Contents");
    } else if (is_remote_folder) {
        gchar* title = g_strdup_printf(_("Remote Folder: %s"), name ? name : "");
        append_title(s, title);
        g_free(title);
        append_field(s, _("Machines"), count);
        list_title = _("Machines");
    } else {
        append_title(s, _("Trash"));
        append_field(s, _("Notes"), count);
        if (path != NULL && path[0] != '\0') {
            append_field(s, _("Location"), path);
        }
        list_title = _("Trashed notes");
    }
    g_free(count);

    if (items->len > 0) {
        gchar* heading = g_strdup_printf(_("%s:"), list_title);
        g_string_append_printf(s, "\n%s\n", heading);
        g_free(heading);
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
        g_string_append(s, _("\n(empty)\n"));
    }

    if (is_notebook) {
        static const SummaryShortcut keys[] = {
            {"a / Shift+A", N_("New note / new notebook")},
            {"F2 / r / e", N_("Edit notebook (name and icon)")},
            {"d / Delete", N_("Delete notebook")},
            {"Shift+K / Shift+J", N_("Move notebook up / down")},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    } else if (is_bm_folder) {
        static const SummaryShortcut keys[] = {
            {"Enter / l", N_("Expand / collapse folder")},
            {"a / Shift+A", N_("Add bookmark / new folder here")},
            {"F2 / r / e", N_("Edit folder (name and icon)")},
            {"d / Delete", N_("Delete folder")},
            {"Shift+K / Shift+J", N_("Move folder up / down")},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    } else if (is_remote_folder) {
        static const SummaryShortcut keys[] = {
            {"Enter / l", N_("Expand / collapse folder")},
            {"a / Shift+A", N_("Add machine here / new folder")},
            {"F2 / r / e", N_("Edit folder (name and icon)")},
            {"d / Delete", N_("Delete folder (its machines move to the top level)")},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    } else {
        static const SummaryShortcut keys[] = {
            {"Shift+R", N_("Restore the selected trashed note to its notebook")},
            {"d / Delete", N_("Delete the selected trashed note permanently")},
            {NC_("key", "Trash toolbar button"), N_("Empty the trash")},
        };
        append_shortcuts(s, keys, G_N_ELEMENTS(keys));
    }

    g_ptr_array_free(items, TRUE);
    return g_string_free(s, FALSE);
}

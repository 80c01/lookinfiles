/* Summaries for explorer rows without file content (see preview_summary.h). */
#include "preview/preview_summary.h"
#include "core/i18n.h"
#include "notes/note_types.h"
#include "canvas/canvas_doc.h"

#include <string.h>

#define FIELD_WIDTH 14 /* label column of "Label: value" lines */
#define KEY_WIDTH   26 /* key column of the shortcut lists */

typedef struct {
    const gchar* keys;
    const gchar* action;
} SummaryShortcut;

static void line_free(gpointer data) {
    PreviewSummaryLine* line = data;
    g_free(line->text);
    g_free(line->detail);
    g_free(line->glyph);
    g_free(line->file_icon);
    g_free(line);
}

static GPtrArray* lines_new(void) {
    return g_ptr_array_new_with_free_func(line_free);
}

/* Empty strings are stored as NULL */
static gchar* dup_or_null(const gchar* s) {
    return (s != NULL && s[0] != '\0') ? g_strdup(s) : NULL;
}

static PreviewSummaryLine* add_line(GPtrArray* lines, PreviewSummaryKind kind,
                                    const gchar* text, const gchar* detail) {
    PreviewSummaryLine* line = g_new0(PreviewSummaryLine, 1);
    line->kind = kind;
    line->text = g_strdup(text != NULL ? text : "");
    line->detail = dup_or_null(detail);
    g_ptr_array_add(lines, line);
    return line;
}

/* The icon a row shows in the explorer: its glyph, else its file-type icon */
static void line_set_row_icon(PreviewSummaryLine* line, ExplorerRow* row) {
    line->glyph = dup_or_null(row->glyph);
    if (line->glyph == NULL) {
        line->file_icon = dup_or_null(row->icon_name);
    }
}

static void add_shortcuts(GPtrArray* lines, const SummaryShortcut* list, gsize count) {
    add_line(lines, PREVIEW_SUMMARY_HEADING, _("Shortcuts"), NULL);
    for (gsize i = 0; i < count; i++) {
        add_line(lines, PREVIEW_SUMMARY_KEY, g_dpgettext2(GETTEXT_PACKAGE, "key", list[i].keys),
                 _(list[i].action));
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

GPtrArray* preview_summary_remote_lines(const RemoteMachine* rm) {
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
    GPtrArray* lines = lines_new();
    gchar* title = g_strdup_printf(_("Remote Machine: %s"), rm->name ? rm->name : _("Unknown"));
    PreviewSummaryLine* head = add_line(lines, PREVIEW_SUMMARY_TITLE, title, NULL);
    head->glyph = g_strdup(rm->is_connected ? "remote-connected" : "remote");
    g_free(title);

    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Status"),
             rm->is_connected ? _("Connected (Mounted via SSHFS)") : _("Disconnected"));
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Host"), rm->host ? rm->host : _("(none)"));
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("User"),
             (rm->user && *rm->user) ? rm->user : _("(default)"));
    gchar* port = g_strdup_printf("%d", rm->port);
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Port"), port);
    g_free(port);
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Remote Path"), rm->remote_path ? rm->remote_path : "~");
    if (rm->identity_file && *rm->identity_file) {
        add_line(lines, PREVIEW_SUMMARY_FIELD, _("Identity Key"), rm->identity_file);
    }
    if (rm->is_connected && rm->mount_point) {
        add_line(lines, PREVIEW_SUMMARY_FIELD, _("Mount Point"), rm->mount_point);
    }
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Source"),
             rm->from_ssh_config ? _("Auto-discovered from ~/.ssh/config") : _("Configured manually"));
    add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    return lines;
}

/* Human-readable note format from the note row badge ("md", "org" or empty) */
static const gchar* note_format_name(const gchar* badge_text) {
    return note_type_name(note_type_from_badge(badge_text));
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

static void add_count(GPtrArray* lines, const gchar* label, guint n) {
    gchar* text = g_strdup_printf("%u", n);
    add_line(lines, PREVIEW_SUMMARY_FIELD, label, text);
    g_free(text);
}

GPtrArray* preview_summary_canvas_lines(const gchar* name, const gchar* path) {
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
    GPtrArray* lines = lines_new();
    gchar* title = g_strdup_printf(_("Canvas: %s"), name != NULL ? name : "");
    PreviewSummaryLine* head = add_line(lines, PREVIEW_SUMMARY_TITLE, title, NULL);
    head->file_icon = g_path_get_basename(path);
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
        add_count(lines, _("Text boxes"), texts);
        add_count(lines, _("Pictures"), pictures);
        if (shapes > 0) {
            add_count(lines, _("Shapes"), shapes);
        }
        if (tables > 0) {
            add_count(lines, _("Tables"), tables);
        }
    }
    add_line(lines, PREVIEW_SUMMARY_FIELD, _("Location"), path);
    if (doc != NULL) {
        gchar* md = canvas_doc_to_markdown(doc, NULL);
        if (md[0] != '\0') {
            add_line(lines, PREVIEW_SUMMARY_TEXT, md, NULL);
        } else {
            add_line(lines, PREVIEW_SUMMARY_NOTE, _("(empty)"), NULL);
        }
        g_free(md);
        canvas_doc_free(doc);
    } else {
        add_line(lines, PREVIEW_SUMMARY_NOTE,
                 too_large ? _("(too large to preview: Enter opens it)") : _("(unreadable page)"), NULL);
    }
    add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    return lines;
}

/* One line per child, placeholder rows (type "-") skipped */
static guint add_items(GPtrArray* lines, ExplorerRow* row) {
    guint n = 0;
    for (guint i = 0; i < explorer_row_n_children(row); i++) {
        ExplorerRow* child = explorer_row_child(row, i);
        if (explorer_row_is(child, "-")) {
            continue;
        }
        gchar* badge_text = markup_to_text(child->badge);
        gchar* name = NULL;
        gchar* detail = NULL;
        if (explorer_row_is(child, "bm_folder")) {
            name = g_strdup_printf("%s/", child->name);
            guint count = count_items(child);
            detail = g_strdup_printf(ngettext("folder, %u item", "folder, %u items", count), count);
        } else if (explorer_row_is(child, "bookmark")) {
            name = g_strdup(child->name);
            detail = g_strdup(child->path);
        } else if (explorer_row_is(child, "note")) {
            name = g_strdup(child->name);
            detail = g_strdup(note_format_name(badge_text));
        } else {
            name = g_strdup(child->name);
            detail = g_strdup(badge_text); /* e.g. "from General" */
        }
        PreviewSummaryLine* line = add_line(lines, PREVIEW_SUMMARY_ITEM, name, detail);
        line_set_row_icon(line, child);
        g_free(name);
        g_free(detail);
        g_free(badge_text);
        n++;
    }
    return n;
}

GPtrArray* preview_summary_container_lines(ExplorerRow* row) {
    if (row == NULL) {
        return NULL;
    }
    const gchar* name = row->name ? row->name : "";
    const gchar* path = row->path;
    GPtrArray* canvas = preview_summary_canvas_lines(name, path);
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

    GPtrArray* lines = lines_new();
    gchar* title = NULL;
    const gchar* count_label = NULL;
    const gchar* list_title = NULL;
    gboolean show_path = FALSE;
    if (is_notebook) {
        title = g_strdup_printf(_("Notebook: %s"), name);
        count_label = _("Notes");
        list_title = _("Notes");
        show_path = TRUE;
    } else if (is_bm_folder) {
        title = g_strdup_printf(_("Bookmark Folder: %s"), name);
        count_label = _("Items");
        list_title = _("Contents");
    } else if (is_remote_folder) {
        title = g_strdup_printf(_("Remote Folder: %s"), name);
        count_label = _("Machines");
        list_title = _("Machines");
    } else {
        title = g_strdup(_("Trash"));
        count_label = _("Notes");
        list_title = _("Trashed notes");
        show_path = TRUE;
    }
    PreviewSummaryLine* head = add_line(lines, PREVIEW_SUMMARY_TITLE, title, NULL);
    line_set_row_icon(head, row);
    g_free(title);
    add_count(lines, count_label, count_items(row));
    if (show_path && path != NULL && path[0] != '\0') {
        add_line(lines, PREVIEW_SUMMARY_FIELD, _("Location"), path);
    }

    guint heading_at = lines->len;
    add_line(lines, PREVIEW_SUMMARY_HEADING, list_title, NULL);
    if (add_items(lines, row) == 0) {
        g_ptr_array_remove_index(lines, heading_at);
        add_line(lines, PREVIEW_SUMMARY_NOTE, _("(empty)"), NULL);
    }

    if (is_notebook) {
        static const SummaryShortcut keys[] = {
            {"a / Shift+A", N_("New note / new notebook")},
            {"F2 / r / e", N_("Edit notebook (name and icon)")},
            {"d / Delete", N_("Delete notebook")},
            {"Shift+K / Shift+J", N_("Move notebook up / down")},
        };
        add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    } else if (is_bm_folder) {
        static const SummaryShortcut keys[] = {
            {"Enter / l", N_("Expand / collapse folder")},
            {"a / Shift+A", N_("Add bookmark / new folder here")},
            {"F2 / r / e", N_("Edit folder (name and icon)")},
            {"d / Delete", N_("Delete folder")},
            {"Shift+K / Shift+J", N_("Move folder up / down")},
        };
        add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    } else if (is_remote_folder) {
        static const SummaryShortcut keys[] = {
            {"Enter / l", N_("Expand / collapse folder")},
            {"a / Shift+A", N_("Add machine here / new folder")},
            {"F2 / r / e", N_("Edit folder (name and icon)")},
            {"d / Delete", N_("Delete folder (its machines move to the top level)")},
        };
        add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    } else {
        static const SummaryShortcut keys[] = {
            {"Shift+R", N_("Restore the selected trashed note to its notebook")},
            {"d / Delete", N_("Delete the selected trashed note permanently")},
            {NC_("key", "Trash toolbar button"), N_("Empty the trash")},
        };
        add_shortcuts(lines, keys, G_N_ELEMENTS(keys));
    }
    return lines;
}

/* ---- Plain text form ---- */

/* Append text padded with spaces to width characters */
static void append_padded(GString* s, const gchar* text, glong width) {
    g_string_append(s, text);
    for (glong n = g_utf8_strlen(text, -1); n < width; n++) {
        g_string_append_c(s, ' ');
    }
}

static gchar* with_colon(const gchar* label) {
    /* TRANSLATORS: a field label or a list heading followed by a colon, "Host:" */
    return g_strdup_printf(_("%s:"), label);
}

glong preview_summary_item_width(GPtrArray* lines, guint i) {
    glong width = 0;
    for (; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        if (line->kind != PREVIEW_SUMMARY_ITEM) {
            break;
        }
        width = MAX(width, MIN(g_utf8_strlen(line->text, -1), PREVIEW_SUMMARY_NAME_MAX));
    }
    return width;
}

gchar* preview_summary_to_text(GPtrArray* lines) {
    if (lines == NULL) {
        return NULL;
    }
    GString* s = g_string_new(NULL);
    glong item_width = 0;
    for (guint i = 0; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        switch (line->kind) {
        case PREVIEW_SUMMARY_TITLE:
            g_string_append_printf(s, "%s\n", line->text);
            for (glong n = g_utf8_strlen(line->text, -1); n > 0; n--) {
                g_string_append_c(s, '=');
            }
            g_string_append(s, "\n\n");
            break;
        case PREVIEW_SUMMARY_FIELD: {
            gchar* label = with_colon(line->text);
            g_string_append(s, "  ");
            append_padded(s, label, FIELD_WIDTH);
            g_string_append_printf(s, " %s\n", line->detail ? line->detail : "");
            g_free(label);
            break;
        }
        case PREVIEW_SUMMARY_HEADING: {
            gchar* heading = with_colon(line->text);
            g_string_append_printf(s, "\n%s\n", heading);
            g_free(heading);
            item_width = preview_summary_item_width(lines, i + 1);
            break;
        }
        case PREVIEW_SUMMARY_ITEM:
            g_string_append(s, "  • ");
            append_padded(s, line->text, item_width);
            if (line->detail != NULL) {
                g_string_append_printf(s, "   %s", line->detail);
            }
            g_string_append_c(s, '\n');
            break;
        case PREVIEW_SUMMARY_KEY:
            g_string_append(s, "  • ");
            append_padded(s, line->text, KEY_WIDTH);
            g_string_append_printf(s, " %s\n", line->detail ? line->detail : "");
            break;
        case PREVIEW_SUMMARY_NOTE:
            g_string_append_printf(s, "\n%s\n", line->text);
            break;
        case PREVIEW_SUMMARY_TEXT:
            g_string_append_printf(s, "\n%s", line->text);
            break;
        }
    }
    return g_string_free(s, FALSE);
}

static gchar* to_text_and_free(GPtrArray* lines) {
    gchar* text = preview_summary_to_text(lines);
    if (lines != NULL) {
        g_ptr_array_unref(lines);
    }
    return text;
}

gchar* preview_summary_remote(const RemoteMachine* rm) {
    return to_text_and_free(preview_summary_remote_lines(rm));
}

gchar* preview_summary_container(ExplorerRow* row) {
    return to_text_and_free(preview_summary_container_lines(row));
}

gchar* preview_summary_canvas(const gchar* name, const gchar* path) {
    return to_text_and_free(preview_summary_canvas_lines(name, path));
}

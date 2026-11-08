/* SQLite database preview (see sqlite_view.h). */
#include "sqlite_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

static guint32 be32(const guchar* p) {
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3];
}

gboolean sqlite_view_read_header(const guchar* head, gsize len, SqliteHeader* h) {
    if (len < 100 || memcmp(head, "SQLite format 3\0", 16) != 0) {
        return FALSE;
    }
    guint page = ((guint)head[16] << 8) | head[17];
    h->page_size = page == 1 ? 65536 : page;
    h->page_count = be32(head + 28);
    guint32 enc = be32(head + 56);
    h->encoding = enc == 2 ? "UTF-16le" : enc == 3 ? "UTF-16be" : "UTF-8";
    h->user_version = be32(head + 60);
    h->application_id = be32(head + 68);
    h->sqlite_version = be32(head + 96);
    h->wal = head[18] == 2 && head[19] == 2;
    return TRUE;
}

static void append_header(PreviewDoc* doc, const SqliteHeader* h) {
    gchar* count = g_strdup_printf("%" G_GUINT64_FORMAT, h->page_count);
    gchar* pages = g_strdup_printf(_("%1$s × %2$u bytes"), count, h->page_size);
    g_free(count);
    preview_doc_field(doc, _("Pages"), pages);
    g_free(pages);
    preview_doc_field(doc, _("Encoding"), h->encoding);
    preview_doc_field(doc, _("Journal"), h->wal ? "write-ahead log (WAL)" : "rollback journal");
    if (h->sqlite_version > 0) {
        gchar* version = g_strdup_printf("%u.%u.%u", h->sqlite_version / 1000000,
                                         (h->sqlite_version / 1000) % 1000, h->sqlite_version % 1000);
        preview_doc_field(doc, _("Written by"), version);
        g_free(version);
    }
    if (h->user_version != 0) {
        gchar* v = g_strdup_printf("%u", h->user_version);
        preview_doc_field(doc, _("User version"), v);
        g_free(v);
    }
    if (h->application_id != 0) {
        gchar* v = g_strdup_printf("0x%08X", h->application_id);
        preview_doc_field(doc, _("Application ID"), v);
        g_free(v);
    }
}

#ifdef HAVE_SQLITE

/* Queries stop after this long (a count(*) on a huge table) */
#define QUERY_BUDGET_US (60 * 1000)

static int progress(void* data) {
    gint64 deadline = *(gint64*)data;
    return g_get_monotonic_time() > deadline;
}

static gchar* quote_ident(const gchar* name) {
    GString* s = g_string_new("\"");
    for (const gchar* p = name; *p != '\0'; p++) {
        if (*p == '"') {
            g_string_append_c(s, '"');
        }
        g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    return g_string_free(s, FALSE);
}

/* Text of a result cell: blobs and NULLs named, long text cut by the table */
static gchar* cell_text(sqlite3_stmt* st, int col) {
    switch (sqlite3_column_type(st, col)) {
    case SQLITE_NULL:
        return g_strdup("NULL");
    case SQLITE_BLOB: {
        gchar* size = g_format_size((guint64)sqlite3_column_bytes(st, col));
        gchar* out = g_strdup_printf("<blob %s>", size);
        g_free(size);
        return out;
    }
    default: {
        const gchar* text = (const gchar*)sqlite3_column_text(st, col);
        return g_utf8_make_valid(text != NULL ? text : "", -1);
    }
    }
}

typedef struct {
    gchar* name;
    gchar* type;          /* "table" / "view" */
    gchar* sql;
} SchemaItem;

static void schema_item_free(gpointer data) {
    SchemaItem* s = data;
    g_free(s->name);
    g_free(s->type);
    g_free(s->sql);
    g_free(s);
}

/* Row count of a table, or -1 if it takes too long */
static gint64 count_rows(sqlite3* db, const gchar* table, gint64* deadline) {
    gchar* q = quote_ident(table);
    gchar* sql = g_strdup_printf("SELECT count(*) FROM %s", q);
    g_free(q);
    sqlite3_stmt* st = NULL;
    gint64 n = -1;
    *deadline = g_get_monotonic_time() + QUERY_BUDGET_US;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
        n = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    g_free(sql);
    return n;
}

static void append_rows(PreviewDoc* doc, sqlite3* db, const gchar* table, gint64* deadline) {
    gchar* q = quote_ident(table);
    gchar* sql = g_strdup_printf("SELECT * FROM %s LIMIT %d", q, SQLITE_VIEW_ROWS);
    g_free(q);
    sqlite3_stmt* st = NULL;
    *deadline = g_get_monotonic_time() + QUERY_BUDGET_US;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        preview_doc_note(doc, sqlite3_errmsg(db));
        g_free(sql);
        return;
    }
    gint n_cols = sqlite3_column_count(st);
    g_autoptr(PreviewTable) t = preview_table_new((guint)MAX(n_cols, 1));
    gchar** header = g_new0(gchar*, n_cols + 1);
    for (gint c = 0; c < n_cols; c++) {
        header[c] = g_strdup(sqlite3_column_name(st, c));
    }
    preview_table_set_header(t, (const gchar* const*)header);
    g_strfreev(header);
    while (sqlite3_step(st) == SQLITE_ROW) {
        gchar** cells = g_new0(gchar*, n_cols + 1);
        for (gint c = 0; c < n_cols; c++) {
            cells[c] = cell_text(st, c);
        }
        preview_table_add_row(t, (const gchar* const*)cells, (guint)n_cols);
        g_strfreev(cells);
    }
    sqlite3_finalize(st);
    g_free(sql);
    if (preview_table_get_n_rows(t) == 0) {
        preview_doc_note(doc, _("(empty)"));
        return;
    }
    preview_doc_add_table(doc, t);
}

/* Column names of a table, comma separated */
static gchar* table_columns(sqlite3* db, const gchar* table) {
    gchar* q = quote_ident(table);
    gchar* sql = g_strdup_printf("PRAGMA table_info(%s)", q);
    g_free(q);
    sqlite3_stmt* st = NULL;
    GString* cols = g_string_new(NULL);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const gchar* name = (const gchar*)sqlite3_column_text(st, 1);
            g_string_append_printf(cols, "%s%s", cols->len > 0 ? ", " : "", name != NULL ? name : "");
        }
    }
    sqlite3_finalize(st);
    g_free(sql);
    return g_string_free(cols, FALSE);
}

static gboolean append_database(PreviewDoc* doc, const gchar* path, GError** error) {
    gchar* uri_path = g_uri_escape_string(path, "/", FALSE);
    gchar* uri = g_strdup_printf("file:%s?mode=ro&immutable=1", uri_path);
    g_free(uri_path);
    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(uri, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, NULL);
    g_free(uri);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", db != NULL ? sqlite3_errmsg(db) : _("cannot open"));
        sqlite3_close(db);
        return FALSE;
    }
    gint64 deadline = g_get_monotonic_time() + QUERY_BUDGET_US;
    sqlite3_progress_handler(db, 1000, progress, &deadline);

    GPtrArray* items = g_ptr_array_new_with_free_func(schema_item_free);
    sqlite3_stmt* st = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT type, name, sql FROM sqlite_master WHERE type IN ('table', 'view') "
                                "AND name NOT LIKE 'sqlite_%' ORDER BY type, name", -1, &st, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        sqlite3_close(db);
        g_ptr_array_free(items, TRUE);
        return FALSE;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        SchemaItem* it = g_new0(SchemaItem, 1);
        it->type = g_strdup((const gchar*)sqlite3_column_text(st, 0));
        it->name = g_strdup((const gchar*)sqlite3_column_text(st, 1));
        it->sql = g_strdup((const gchar*)sqlite3_column_text(st, 2));
        g_ptr_array_add(items, it);
    }
    sqlite3_finalize(st);

    if (items->len == 0) {
        preview_doc_note(doc, _("\nNo tables yet."));
    } else {
        preview_doc_append_len(doc, "\n", 1);
        g_autoptr(PreviewTable) list = preview_table_new(4);
        const gchar* header[] = { _("Name"), _("Type"), _("Rows"), _("Columns") };
        preview_table_set_header(list, header);
        preview_table_set_right_aligned(list, 2);
        preview_table_set_max_cell_width(list, 60);
        for (guint i = 0; i < items->len; i++) {
            const SchemaItem* it = g_ptr_array_index(items, i);
            gint64 n = count_rows(db, it->name, &deadline);
            gchar* rows = n >= 0 ? g_strdup_printf("%" G_GINT64_FORMAT, n) : g_strdup("many");
            gchar* cols = table_columns(db, it->name);
            const gchar* row[] = { it->name, it->type, rows, cols };
            preview_table_add_row(list, row, 4);
            g_free(cols);
            g_free(rows);
        }
        preview_doc_add_table(doc, list);
        guint shown = 0;
        for (guint i = 0; i < items->len && shown < SQLITE_VIEW_TABLES; i++) {
            const SchemaItem* it = g_ptr_array_index(items, i);
            if (g_strcmp0(it->type, "table") != 0) {
                continue;
            }
            shown++;
            preview_doc_section(doc, it->name);
            if (it->sql != NULL) {
                gchar* one_line = g_strdup(it->sql);
                g_strdelimit(one_line, "\n\r\t", ' ');
                gchar* cut = g_utf8_strlen(one_line, -1) > 160 ? g_utf8_substring(one_line, 0, 160) : NULL;
                preview_doc_append(doc, PREVIEW_STYLE_COMMENT, cut != NULL ? cut : one_line);
                preview_doc_append(doc, PREVIEW_STYLE_COMMENT, cut != NULL ? "…\n" : "\n");
                g_free(cut);
                g_free(one_line);
            }
            append_rows(doc, db, it->name, &deadline);
        }
    }
    g_ptr_array_free(items, TRUE);
    sqlite3_close(db);
    return TRUE;
}

#endif /* HAVE_SQLITE */

gboolean sqlite_view_append(PreviewDoc* doc, const gchar* path, const guchar* head, gsize head_len,
                            GError** error) {
    SqliteHeader header;
    if (!sqlite_view_read_header(head, head_len, &header)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an SQLite database"));
        return FALSE;
    }
    append_header(doc, &header);
#ifdef HAVE_SQLITE
    return append_database(doc, path, error);
#else
    (void)path;
    preview_doc_note(doc, _("\nTables are listed when LookinFiles is built with SQLite (libsqlite3-dev)."));
    return TRUE;
#endif
}

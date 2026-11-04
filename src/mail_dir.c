/* Maildir and MH mail folders (see mail_dir.h) */
#include "mail_dir.h"
#include "mail_message.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define HEADER_BYTES (64 * 1024)
#define MAX_FILES 100000

typedef struct {
    gchar* path;
    gint64 mtime;
    gboolean unread;
    gboolean replied;
    gboolean flagged;
    gchar* from;
    gchar* subject;
    gint64 date;
} Item;

static void item_free(gpointer p) {
    Item* it = p;
    g_free(it->path);
    g_free(it->from);
    g_free(it->subject);
    g_free(it);
}

static gboolean is_dir(const gchar* dir, const gchar* child) {
    gchar* p = g_build_filename(dir, child, NULL);
    gboolean ok = g_file_test(p, G_FILE_TEST_IS_DIR);
    g_free(p);
    return ok;
}

static gboolean all_digits(const gchar* s) {
    if (s[0] == '\0') return FALSE;
    for (; *s != '\0'; s++) {
        if (!g_ascii_isdigit(*s)) return FALSE;
    }
    return TRUE;
}

MailDirKind mail_dir_kind(const gchar* path) {
    if (path == NULL) return MAIL_DIR_NONE;
    if (is_dir(path, "cur") && is_dir(path, "new")) return MAIL_DIR_MAILDIR;
    gchar* seq = g_build_filename(path, ".mh_sequences", NULL);
    gboolean mh = g_file_test(seq, G_FILE_TEST_EXISTS);
    g_free(seq);
    return mh ? MAIL_DIR_MH : MAIL_DIR_NONE;
}

/* Files of dir as Items (Maildir: unread in new/, flags after ":2,") */
static void list_files(const gchar* dir, gboolean maildir_new, gboolean mh, GPtrArray* out) {
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d == NULL) return;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL && out->len < MAX_FILES) {
        if (name[0] == '.' || (mh && !all_digits(name))) continue;
        gchar* path = g_build_filename(dir, name, NULL);
        GStatBuf st;
        if (g_stat(path, &st) != 0 || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_free(path);
            continue;
        }
        Item* it = g_new0(Item, 1);
        it->path = path;
        it->mtime = (gint64)st.st_mtime;
        it->date = -1;
        if (!mh) {
            /* Maildir names start with the delivery time */
            if (g_ascii_isdigit(name[0])) {
                gint64 t = g_ascii_strtoll(name, NULL, 10);
                if (t > 0) it->mtime = t;
            }
            const gchar* info = strstr(name, ":2,");
            if (info == NULL) info = strstr(name, ";2,");
            if (info == NULL) info = strstr(name, "!2,");
            const gchar* flags = info != NULL ? info + 3 : "";
            it->unread = maildir_new || strchr(flags, 'S') == NULL;
            it->replied = strchr(flags, 'R') != NULL;
            it->flagged = strchr(flags, 'F') != NULL;
        } else {
            it->mtime = (gint64)g_ascii_strtoll(name, NULL, 10); /* MH: the number orders */
        }
        g_ptr_array_add(out, it);
    }
    g_dir_close(d);
}

static gint newest_first(gconstpointer a, gconstpointer b) {
    const Item* x = *(Item* const*)a;
    const Item* y = *(Item* const*)b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

static gint oldest_first(gconstpointer a, gconstpointer b) {
    return -newest_first(a, b);
}

static void read_headers(Item* it) {
    FILE* f = g_fopen(it->path, "rb");
    if (f == NULL) return;
    gchar* buf = g_malloc(HEADER_BYTES);
    gsize n = fread(buf, 1, HEADER_BYTES, f);
    fclose(f);
    /* The header block only */
    gchar* end = g_strstr_len(buf, (gssize)n, "\n\n");
    gchar* end_crlf = g_strstr_len(buf, (gssize)n, "\r\n\r\n");
    gsize len = end != NULL ? (gsize)(end - buf) + 1 : n;
    if (end_crlf != NULL && (gsize)(end_crlf - buf) + 2 < len) len = (gsize)(end_crlf - buf) + 2;
    MailMessage* m = mail_message_parse(buf, len, 0);
    it->from = g_strdup(m->from);
    it->subject = g_strdup(m->subject);
    it->date = m->date >= 0 ? m->date : it->mtime;
    mail_message_free(m);
    g_free(buf);
}

/* MH "unseen: 1-3 7" sequence marks */
static void mh_unseen(const gchar* path, GPtrArray* items) {
    gchar* seq = g_build_filename(path, ".mh_sequences", NULL);
    gchar* text = NULL;
    if (g_file_get_contents(seq, &text, NULL, NULL)) {
        gchar** lines = g_strsplit(text, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            if (!g_str_has_prefix(lines[i], "unseen:")) continue;
            gchar** ranges = g_strsplit_set(lines[i] + 7, " \t", -1);
            for (gint r = 0; ranges[r] != NULL; r++) {
                if (ranges[r][0] == '\0') continue;
                gint64 lo = g_ascii_strtoll(ranges[r], NULL, 10);
                const gchar* dash = strchr(ranges[r], '-');
                gint64 hi = dash != NULL ? g_ascii_strtoll(dash + 1, NULL, 10) : lo;
                for (guint k = 0; k < items->len; k++) {
                    Item* it = g_ptr_array_index(items, k);
                    if (it->mtime >= lo && it->mtime <= hi) it->unread = TRUE;
                }
            }
            g_strfreev(ranges);
        }
        g_strfreev(lines);
    }
    g_free(text);
    g_free(seq);
}

static gchar* format_date(gint64 t) {
    if (t < 0) return g_strdup("");
    GDateTime* dt = g_date_time_new_from_unix_local(t);
    gchar* s = dt != NULL ? g_date_time_format(dt, "%Y-%m-%d %H:%M") : g_strdup("");
    if (dt != NULL) g_date_time_unref(dt);
    return s;
}

PreviewDoc* mail_dir_preview(const gchar* path, const gchar* name, guint max_messages, GCancellable* cancellable) {
    MailDirKind kind = mail_dir_kind(path);
    if (kind == MAIL_DIR_NONE) return NULL;
    GPtrArray* items = g_ptr_array_new_with_free_func(item_free);
    GPtrArray* folders = g_ptr_array_new_with_free_func(g_free);
    if (kind == MAIL_DIR_MAILDIR) {
        gchar* cur = g_build_filename(path, "cur", NULL);
        gchar* nw = g_build_filename(path, "new", NULL);
        list_files(cur, FALSE, FALSE, items);
        list_files(nw, TRUE, FALSE, items);
        g_free(cur);
        g_free(nw);
        /* Maildir++ subfolders: ".Sent", ".Archive.2026" */
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* entry;
        while (d != NULL && (entry = g_dir_read_name(d)) != NULL) {
            if (entry[0] == '.' && entry[1] != '\0' && is_dir(path, entry)) g_ptr_array_add(folders, g_strdup(entry + 1));
        }
        if (d != NULL) g_dir_close(d);
        g_ptr_array_sort_values(folders, (GCompareFunc)g_strcmp0);
    } else {
        list_files(path, FALSE, TRUE, items);
        mh_unseen(path, items);
    }
    guint total = items->len, unread = 0;
    for (guint i = 0; i < items->len; i++) unread += ((Item*)g_ptr_array_index(items, i))->unread ? 1 : 0;
    /* The newest messages, shown oldest first (newest last, like a mail client) */
    g_ptr_array_sort(items, newest_first);
    if (items->len > max_messages) g_ptr_array_set_size(items, max_messages);
    for (guint i = 0; i < items->len && !g_cancellable_is_cancelled(cancellable); i++) {
        read_headers(g_ptr_array_index(items, i));
    }
    for (guint i = 0; i < items->len; i++) {
        Item* it = g_ptr_array_index(items, i);
        it->mtime = it->date; /* sort by the message dates read */
    }
    g_ptr_array_sort(items, oldest_first);

    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* subtitle = g_strdup_printf("%s · %u %s, %u unread", kind == MAIL_DIR_MAILDIR ? "Maildir folder" : "MH folder",
                                      total, total == 1 ? "message" : "messages", unread);
    preview_doc_title(doc, name, subtitle);
    g_free(subtitle);
    if (folders->len > 0) {
        g_ptr_array_add(folders, NULL);
        gchar* list = g_strjoinv(" · ", (gchar**)folders->pdata);
        preview_doc_field(doc, "Folders", list);
        g_free(list);
    }
    if (items->len == 0) {
        preview_doc_note(doc, "\n(empty)");
    } else {
        g_autoptr(PreviewTable) table = preview_table_new(4);
        const gchar* header[] = {"Date", "From", "Subject", "Status"};
        preview_table_set_header(table, header);
        for (guint i = 0; i < items->len; i++) {
            Item* it = g_ptr_array_index(items, i);
            gchar* date = format_date(it->date);
            GString* status = g_string_new(it->unread ? "unread" : it->replied ? "replied" : "read");
            if (it->flagged) g_string_append(status, ", flagged");
            const gchar* row[] = {date, it->from != NULL ? it->from : "",
                                  it->subject != NULL ? it->subject : "(no subject)", status->str};
            preview_table_add_row(table, row, 4);
            g_string_free(status, TRUE);
            g_free(date);
        }
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_add_table(doc, table);
        if (total > items->len) {
            gchar* note = g_strdup_printf("The newest %u of %u messages are shown.", items->len, total);
            preview_doc_note(doc, note);
            g_free(note);
        }
    }
    g_ptr_array_unref(folders);
    g_ptr_array_unref(items);
    return doc;
}

/* mbox mailboxes (see mail_box.h) */
#include "mail_box.h"
#include <string.h>

#define MAX_ENTRIES 20000
#define HEADER_LIMIT (64 * 1024)

/* A "From " separator line at p: "From " then a sender and a date (a
 * digit somewhere: the year) */
static gboolean from_line(const gchar* p, const gchar* end) {
    if (end - p < 6 || memcmp(p, "From ", 5) != 0) return FALSE;
    const gchar* nl = memchr(p, '\n', (gsize)(end - p));
    const gchar* le = nl != NULL ? nl : end;
    if (le - p > 512) return FALSE;
    for (const gchar* c = p + 5; c < le; c++) {
        if (g_ascii_isdigit(*c)) return TRUE;
    }
    return FALSE;
}

gboolean mail_box_looks_like(const gchar* data, gsize len) {
    if (data == NULL || !from_line(data, data + len)) return FALSE;
    const gchar* nl = memchr(data, '\n', len);
    if (nl == NULL) return FALSE;
    /* The next line is a header */
    const gchar* p = nl + 1;
    const gchar* end = data + len;
    const gchar* colon = memchr(p, ':', (gsize)MIN(end - p, 80));
    if (colon == NULL || colon == p) return FALSE;
    for (const gchar* c = p; c < colon; c++) {
        if (*c <= ' ' || *c > '~') return FALSE;
    }
    return TRUE;
}

static void entry_free(gpointer p) {
    MailBoxEntry* e = p;
    g_free(e->from);
    g_free(e->subject);
    g_free(e);
}

void mail_box_free(MailBox* box) {
    if (box == NULL) return;
    g_ptr_array_unref(box->entries);
    g_free(box);
}

/* Header block of the message at p (after its From line); *body: start of
 * its body; returns its Content-Length or -1 */
static MailBoxEntry* read_entry(const gchar* data, const gchar* p, const gchar* end, const gchar** body,
                                gint64* content_length) {
    const gchar* nl = memchr(p, '\n', (gsize)(end - p));
    const gchar* h = nl != NULL ? nl + 1 : end;
    /* Headers end at the first blank line */
    const gchar* he = h;
    const gchar* limit = MIN(end, h + HEADER_LIMIT);
    while (he < limit) {
        const gchar* l = memchr(he, '\n', (gsize)(limit - he));
        if (l == NULL) {
            he = limit;
            break;
        }
        if (l == he || (l == he + 1 && *he == '\r')) {
            he = l + 1;
            break;
        }
        he = l + 1;
    }
    *body = he;
    MailMessage* m = mail_message_parse(h, (gsize)(he - h), 0);
    MailBoxEntry* e = g_new0(MailBoxEntry, 1);
    e->offset = (gsize)(p - data);
    e->from = g_strdup(m->from);
    e->subject = g_strdup(m->subject);
    e->date = m->date;
    if (e->date < 0 && nl != NULL) {
        /* No Date header: the date of the From line ("From a@b Mon Nov  2 08:00:00 2026") */
        gchar* line = g_strndup(p + 5, (gsize)(nl - p - 5));
        const gchar* sp = strchr(line, ' ');
        e->date = sp != NULL ? mail_parse_date(sp + 1) : -1;
        g_free(line);
    }
    const gchar* ct = mail_message_header(m, "Content-Type");
    e->attachments = ct != NULL && g_ascii_strncasecmp(ct, "multipart/mixed", 15) == 0;
    const gchar* cl = mail_message_header(m, "Content-Length");
    *content_length = cl != NULL ? g_ascii_strtoll(cl, NULL, 10) : -1;
    mail_message_free(m);
    return e;
}

/* Entries of [from, to) (whole messages only when to < end of data) */
static void scan_range(const gchar* data, gsize len, gsize from, gsize to, GPtrArray* out, guint max) {
    const gchar* end = data + len;
    const gchar* p = data + from;
    const gchar* stop = data + to;
    /* Start at a separator: after a blank line, or at the very start */
    if (from > 0) {
        const gchar* q = p;
        p = NULL;
        while (q < stop) {
            const gchar* hit = g_strstr_len(q, (gssize)(stop - q), "\n\nFrom ");
            if (hit == NULL) break;
            if (from_line(hit + 2, end)) {
                p = hit + 2;
                break;
            }
            q = hit + 1;
        }
        if (p == NULL) return;
    } else if (!from_line(p, end)) {
        return;
    }
    while (p != NULL && p < stop && out->len < max) {
        const gchar* body;
        gint64 clen = -1;
        MailBoxEntry* e = read_entry(data, p, end, &body, &clen);
        g_ptr_array_add(out, e);
        /* Next separator: after Content-Length bytes (mboxcl), else the next
         * "From " line after a blank line */
        const gchar* search = body;
        if (clen >= 0 && clen <= end - body) {
            const gchar* cand = body + clen;
            if (cand < end && *cand == '\n') cand++;
            if (cand == end || from_line(cand, end)) {
                e->length = (gsize)(cand - p);
                p = cand < end ? cand : NULL;
                continue;
            }
        }
        const gchar* next = NULL;
        const gchar* q = search;
        if (from_line(body, end) && body > data && body[-1] == '\n') next = body; /* empty body */
        while (next == NULL && q < end) {
            const gchar* hit = g_strstr_len(q, (gssize)(end - q), "\nFrom ");
            if (hit == NULL) break;
            /* A blank line before (or the message body is empty) */
            if (hit > data && hit[-1] == '\n') {
                if (from_line(hit + 1, end)) next = hit + 1;
            }
            q = hit + 1;
        }
        e->length = (gsize)((next != NULL ? next : end) - p);
        p = next;
    }
}

MailBox* mail_box_scan(const gchar* data, gsize len) {
    if (!mail_box_looks_like(data, len)) return NULL;
    MailBox* box = g_new0(MailBox, 1);
    box->entries = g_ptr_array_new_with_free_func(entry_free);
    if (len <= MAIL_BOX_FULL_SCAN) {
        scan_range(data, len, 0, len, box->entries, MAX_ENTRIES);
        box->complete = box->entries->len < MAX_ENTRIES;
        return box;
    }
    /* The beginning, then the end of a large mailbox */
    scan_range(data, len, 0, MAIL_BOX_PART_SCAN, box->entries, MAX_ENTRIES / 2);
    box->head_count = box->entries->len;
    GPtrArray* tail = g_ptr_array_new_with_free_func(entry_free);
    scan_range(data, len, len - MAIL_BOX_PART_SCAN, len, tail, MAX_ENTRIES / 2);
    for (guint i = 0; i < tail->len; i++) {
        MailBoxEntry* e = g_ptr_array_index(tail, i);
        MailBoxEntry* last = box->entries->len > 0 ? g_ptr_array_index(box->entries, box->entries->len - 1) : NULL;
        if (last != NULL && e->offset <= last->offset) continue;
        g_ptr_array_add(box->entries, g_ptr_array_steal_index(tail, i--));
    }
    g_ptr_array_unref(tail);
    /* The last head entry ran into the middle: stop it at the scanned part */
    if (box->head_count > 0) {
        MailBoxEntry* e = g_ptr_array_index(box->entries, box->head_count - 1);
        gsize next = box->head_count < box->entries->len
                         ? ((MailBoxEntry*)g_ptr_array_index(box->entries, box->head_count))->offset
                         : len;
        e->length = MIN(e->length, next - e->offset);
    }
    return box;
}

MailMessage* mail_box_message(const MailBox* box, guint i, const gchar* data, gsize len, gsize max_part) {
    if (box == NULL || i >= box->entries->len) return NULL;
    const MailBoxEntry* e = g_ptr_array_index(box->entries, i);
    if (e->offset >= len) return NULL;
    gsize n = MIN(e->length, len - e->offset);
    const gchar* p = data + e->offset;
    /* Skip the From line, unquote ">From " lines (mboxo / mboxrd) */
    const gchar* nl = memchr(p, '\n', n);
    const gchar* s = nl != NULL ? nl + 1 : p + n;
    const gchar* end = p + n;
    GString* text = g_string_sized_new((gsize)(end - s));
    while (s < end) {
        const gchar* l = memchr(s, '\n', (gsize)(end - s));
        const gchar* le = l != NULL ? l + 1 : end;
        const gchar* q = s;
        while (q < le && *q == '>') q++;
        if (q > s && le - q >= 5 && memcmp(q, "From ", 5) == 0) s++;
        g_string_append_len(text, s, (gssize)(le - s));
        s = le;
    }
    MailMessage* m = mail_message_parse(text->str, text->len, max_part);
    g_string_free(text, TRUE);
    return m;
}

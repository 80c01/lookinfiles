#include "dir_summary.h"

#include <string.h>

DirSummary* dir_summary_new(void) {
    DirSummary* s = g_new0(DirSummary, 1);
    s->entries = g_array_new(FALSE, FALSE, sizeof(DirSummaryEntry));
    return s;
}

void dir_summary_free(DirSummary* s) {
    if (s == NULL) {
        return;
    }
    for (guint i = 0; i < s->entries->len; i++) {
        g_free(g_array_index(s->entries, DirSummaryEntry, i).name);
    }
    g_array_unref(s->entries);
    g_free(s);
}

gboolean dir_summary_add(DirSummary* s, const gchar* name, gboolean is_dir,
                         guint64 size, gint64 mtime) {
    if (s->entries->len >= DIR_SUMMARY_SCAN_LIMIT) {
        s->truncated = TRUE;
        return FALSE;
    }
    DirSummaryEntry e = { g_strdup(name), is_dir, is_dir ? 0 : size, mtime };
    g_array_append_val(s->entries, e);
    if (is_dir) {
        s->n_dirs++;
    } else {
        s->n_files++;
        s->total_size += size;
    }
    if (mtime > s->newest_mtime) {
        s->newest_mtime = mtime;
    }
    return TRUE;
}

/* Extension without the dot, "" if none (a leading dot is not an extension) */
static const gchar* name_extension(const gchar* name) {
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

static gint compare_entries(gconstpointer a, gconstpointer b, gpointer user_data) {
    const DirSummaryEntry* ea = a;
    const DirSummaryEntry* eb = b;
    DirSummarySort sort = (DirSummarySort)GPOINTER_TO_INT(user_data);

    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    gint res = 0;
    switch (sort) {
    case DIR_SUMMARY_SORT_DATE:
        res = (ea->mtime > eb->mtime) ? -1 : (ea->mtime < eb->mtime) ? 1 : 0;
        break;
    case DIR_SUMMARY_SORT_SIZE:
        res = (ea->size > eb->size) ? -1 : (ea->size < eb->size) ? 1 : 0;
        break;
    case DIR_SUMMARY_SORT_EXT: {
        gchar* xa = g_utf8_casefold(name_extension(ea->name), -1);
        gchar* xb = g_utf8_casefold(name_extension(eb->name), -1);
        res = g_strcmp0(xa, xb);
        g_free(xa);
        g_free(xb);
        break;
    }
    default:
        break;
    }
    if (res == 0) {
        res = g_utf8_collate(ea->name, eb->name);
    }
    return res;
}

void dir_summary_sort(DirSummary* s, DirSummarySort sort) {
    g_array_sort_with_data(s->entries, compare_entries, GINT_TO_POINTER((gint)sort));
}

gchar* dir_summary_format_age(gint64 mtime, gint64 now) {
    if (mtime <= 0) {
        return g_strdup("");
    }
    gint64 delta = now - mtime;
    if (delta < 60) {
        return g_strdup("just now"); /* includes dates slightly in the future */
    }
    if (delta < 3600) {
        return g_strdup_printf("%d min ago", (gint)(delta / 60));
    }
    if (delta < 86400) {
        return g_strdup_printf("%d h ago", (gint)(delta / 3600));
    }
    if (delta < 2 * 86400) {
        return g_strdup("yesterday");
    }
    if (delta < 30 * 86400) {
        return g_strdup_printf("%d days ago", (gint)(delta / 86400));
    }
    GDateTime* dt = g_date_time_new_from_unix_local(mtime);
    if (dt == NULL) {
        return g_strdup("");
    }
    gchar* text = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);
    return text;
}

gchar* dir_summary_pad(const gchar* text, glong width, gboolean align_right) {
    if (text == NULL) {
        text = "";
    }
    if (width <= 0) {
        return g_strdup("");
    }
    glong len = g_utf8_strlen(text, -1);
    if (len > width) {
        /* Keep width - 1 characters and an ellipsis */
        const gchar* cut = g_utf8_offset_to_pointer(text, width - 1);
        GString* out = g_string_new_len(text, cut - text);
        g_string_append(out, "…");
        return g_string_free(out, FALSE);
    }
    GString* out = g_string_new(NULL);
    if (align_right) {
        for (glong i = len; i < width; i++) g_string_append_c(out, ' ');
        g_string_append(out, text);
    } else {
        g_string_append(out, text);
        for (glong i = len; i < width; i++) g_string_append_c(out, ' ');
    }
    return g_string_free(out, FALSE);
}

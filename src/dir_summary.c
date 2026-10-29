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
    g_free(s->readme);
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
        s->kinds[file_kind_from_name(name)]++;
        gint rank = dir_summary_readme_rank(name);
        if (rank > 0 && rank > (s->readme != NULL ? dir_summary_readme_rank(s->readme) : 0)) {
            g_free(s->readme);
            s->readme = g_strdup(name);
        }
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

gchar* dir_summary_kinds_line(const DirSummary* s) {
    guint docs = s->kinds[FILE_KIND_MARKDOWN] + s->kinds[FILE_KIND_HTML] +
                 s->kinds[FILE_KIND_ORG] + s->kinds[FILE_KIND_PDF];
    guint other = s->kinds[FILE_KIND_TEXT] + s->kinds[FILE_KIND_BINARY];
    const struct { guint n; const gchar* one; const gchar* many; } parts[] = {
        { docs, "document", "documents" },
        { s->kinds[FILE_KIND_IMAGE], "image", "images" },
        { s->kinds[FILE_KIND_VIDEO], "video", "videos" },
        { s->kinds[FILE_KIND_AUDIO], "audio file", "audio files" },
    };
    GString* out = g_string_new(NULL);
    for (gsize i = 0; i < G_N_ELEMENTS(parts); i++) {
        if (parts[i].n > 0) {
            g_string_append_printf(out, "%s%u %s", out->len > 0 ? " · " : "", parts[i].n,
                                   parts[i].n == 1 ? parts[i].one : parts[i].many);
        }
    }
    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    if (other > 0) {
        g_string_append_printf(out, " · %u other", other);
    }
    return g_string_free(out, FALSE);
}

gint dir_summary_readme_rank(const gchar* name) {
    if (name == NULL) {
        return 0;
    }
    if (g_ascii_strcasecmp(name, "README.md") == 0 || g_ascii_strcasecmp(name, "README.markdown") == 0) {
        return 4;
    }
    if (g_ascii_strcasecmp(name, "README.org") == 0) {
        return 3;
    }
    if (g_ascii_strcasecmp(name, "README") == 0) {
        return 2;
    }
    if (g_ascii_strcasecmp(name, "README.txt") == 0) {
        return 1;
    }
    return 0;
}

gchar dir_summary_git_letter(const gchar* badge) {
    if (badge == NULL) {
        return 0;
    }
    const gchar* gt = strchr(badge, '>');
    const gchar* p = (gt != NULL) ? gt + 1 : badge;
    return (p[0] != '\0' && p[0] != '<') ? p[0] : 0;
}

void dir_summary_count_git(GHashTable* statuses, const gchar* dir, DirGitCounts* counts) {
    memset(counts, 0, sizeof(*counts));
    if (statuses == NULL || dir == NULL) {
        return;
    }
    gchar* prefix = g_str_has_suffix(dir, "/") ? g_strdup(dir) : g_strconcat(dir, "/", NULL);

    /* Paths below dir, and the set of their parents (rolled-up directories) */
    GPtrArray* below = g_ptr_array_new();
    GHashTable* parents = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, statuses);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        const gchar* path = key;
        if (g_str_has_prefix(path, prefix) && path[strlen(prefix)] != '\0') {
            g_ptr_array_add(below, (gpointer)path);
            g_hash_table_add(parents, g_path_get_dirname(path));
        }
    }

    for (guint i = 0; i < below->len; i++) {
        const gchar* path = g_ptr_array_index(below, i);
        if (g_hash_table_contains(parents, path)) {
            continue; /* a directory with changes below it */
        }
        switch (dir_summary_git_letter(g_hash_table_lookup(statuses, path))) {
        case 'M': counts->modified++; break;
        case 'A': counts->added++; break;
        case 'U': counts->untracked++; break;
        case 'D': counts->deleted++; break;
        case 'R': counts->renamed++; break;
        case '!': counts->conflicts++; break;
        default: break;
        }
    }
    g_hash_table_destroy(parents);
    g_ptr_array_unref(below);
    g_free(prefix);
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

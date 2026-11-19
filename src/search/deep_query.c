/* The criteria of a deep search (see deep_query.h) */
#include "search/deep_query.h"
#include "core/i18n.h"
#include "core/path_globs.h"

#include <stdio.h>
#include <string.h>

void deep_query_init(DeepQuery* q) {
    memset(q, 0, sizeof(*q));
    q->min_size = -1;
    q->max_size = -1;
}

void deep_query_clear(DeepQuery* q) {
    if (q == NULL) return;
    g_free(q->name);
    g_free(q->extensions);
    g_free(q->exclude);
    g_free(q->content);
    deep_query_init(q);
}

void deep_query_copy(DeepQuery* dst, const DeepQuery* src) {
    *dst = *src;
    dst->name = g_strdup(src->name);
    dst->extensions = g_strdup(src->extensions);
    dst->exclude = g_strdup(src->exclude);
    dst->content = g_strdup(src->content);
}

static gboolean blank(const gchar* s) {
    if (s == NULL) return TRUE;
    for (; *s != '\0'; s++) {
        if (!g_ascii_isspace(*s)) return FALSE;
    }
    return TRUE;
}

/* Parsing */

gboolean deep_query_parse_size(const gchar* text, gint64* out) {
    *out = -1;
    if (blank(text)) return TRUE;
    gchar* s = g_ascii_strdown(text, -1);
    g_strstrip(s);
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(s, &end);
    gboolean ok = end != s && v >= 0.0;
    gdouble scale = 1.0;
    if (ok) {
        while (*end == ' ') end++;
        switch (*end) {
        case 'k': scale = 1024.0; end++; break;
        case 'm': scale = 1024.0 * 1024.0; end++; break;
        case 'g': scale = 1024.0 * 1024.0 * 1024.0; end++; break;
        case 't': scale = 1024.0 * 1024.0 * 1024.0 * 1024.0; end++; break;
        default: break;
        }
        if (scale > 1.0 && *end == 'i') end++; /* KiB, MiB */
        if (*end == 'b') end++;
        ok = *end == '\0' && v * scale < 9.0e18;
    }
    if (ok) *out = (gint64)(v * scale + 0.5);
    g_free(s);
    return ok;
}

/* "7d", "30min", "1 year"...: seconds back from now (months and years by
 * the calendar), or FALSE */
static gboolean time_ago(const gchar* s, GDateTime* now, gint64* out) {
    gchar* end = NULL;
    guint64 n = g_ascii_strtoull(s, &end, 10);
    if (end == s || n > 100000) return FALSE;
    while (*end == ' ') end++;
    static const struct {
        const gchar* unit;
        gint64 seconds; /* 0: months, -1: years */
    } UNITS[] = {
        {"s", 1}, {"sec", 1}, {"second", 1}, {"seconds", 1},
        {"min", 60}, {"mins", 60}, {"minute", 60}, {"minutes", 60},
        {"h", 3600}, {"hour", 3600}, {"hours", 3600},
        {"d", 86400}, {"day", 86400}, {"days", 86400},
        {"w", 604800}, {"week", 604800}, {"weeks", 604800},
        {"m", 0}, {"month", 0}, {"months", 0},
        {"y", -1}, {"year", -1}, {"years", -1},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(UNITS); i++) {
        if (strcmp(end, UNITS[i].unit) != 0) continue;
        GDateTime* t = NULL;
        if (UNITS[i].seconds == 0) {
            t = g_date_time_add_months(now, -(gint)n);
        } else if (UNITS[i].seconds < 0) {
            t = g_date_time_add_years(now, -(gint)n);
        } else {
            t = g_date_time_add_seconds(now, -(gdouble)n * (gdouble)UNITS[i].seconds);
        }
        if (t == NULL) return FALSE;
        *out = g_date_time_to_unix(t);
        g_date_time_unref(t);
        return TRUE;
    }
    return FALSE;
}

/* The start of the day of t, plus days */
static gint64 day_start(GDateTime* t, gint days) {
    GDateTime* d = g_date_time_new(g_date_time_get_timezone(t), g_date_time_get_year(t),
                                   g_date_time_get_month(t), g_date_time_get_day_of_month(t), 0, 0, 0.0);
    GDateTime* moved = g_date_time_add_days(d, days);
    gint64 out = g_date_time_to_unix(moved);
    g_date_time_unref(moved);
    g_date_time_unref(d);
    return out;
}

gboolean deep_query_parse_date(const gchar* text, GDateTime* now, gboolean end_of_day, gint64* out) {
    *out = 0;
    if (blank(text)) return TRUE;
    gchar* s = g_ascii_strdown(text, -1);
    g_strstrip(s);
    gboolean ok = FALSE;
    if (strcmp(s, "today") == 0) {
        *out = day_start(now, end_of_day ? 1 : 0);
        ok = TRUE;
    } else if (strcmp(s, "yesterday") == 0) {
        *out = day_start(now, end_of_day ? 0 : -1);
        ok = TRUE;
    } else if (g_ascii_isdigit(s[0]) && strchr(s, '-') == NULL) {
        ok = time_ago(s, now, out);
    } else {
        gint y = 0, mo = 0, d = 0, h = 0, mi = 0;
        gchar tail = '\0';
        gint n = sscanf(s, "%d-%d-%d %d:%d%c", &y, &mo, &d, &h, &mi, &tail);
        if ((n == 3 || n == 5) && y >= 1 && y <= 9999 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 &&
            h < 24 && mi >= 0 && mi < 60) {
            GDateTime* t = g_date_time_new(g_date_time_get_timezone(now), y, mo, d, h, mi, 0.0);
            if (t != NULL) {
                *out = g_date_time_to_unix(t);
                if (n == 3 && end_of_day) *out = day_start(t, 1);
                g_date_time_unref(t);
                ok = TRUE;
            }
        }
    }
    g_free(s);
    return ok;
}

gchar* deep_query_check(const DeepQuery* q) {
    if (q->min_size >= 0 && q->max_size >= 0 && q->min_size > q->max_size) {
        return g_strdup(_("The minimum size is over the maximum"));
    }
    if (q->after > 0 && q->before > 0 && q->after >= q->before) {
        return g_strdup(_("The first date is not before the second"));
    }
    if (!blank(q->content)) {
        gchar* why = text_search_check(q->content, &q->content_opts);
        if (why != NULL) {
            /* TRANSLATORS: %s is the error of the regular expression */
            gchar* text = g_strdup_printf(_("Invalid regular expression: %s"), why);
            g_free(why);
            return text;
        }
    }
    return NULL;
}

/* The compiled filter */

struct DeepFilter {
    DeepQuery q;
    gchar* name_fold;      /* substring of the name, case folded */
    GPatternSpec* name_glob; /* or a pattern of the whole name, case folded */
    GPtrArray* exts;       /* ".c", ".tar.gz": case folded */
    PathGlobs* exclude;
    gboolean content;
};

static gchar* fold(const gchar* s) {
    gchar* valid = g_utf8_make_valid(s, -1);
    gchar* folded = g_utf8_casefold(valid, -1);
    g_free(valid);
    return folded;
}

DeepFilter* deep_filter_new(const DeepQuery* q) {
    gchar* why = deep_query_check(q);
    if (why != NULL) {
        g_free(why);
        return NULL;
    }
    DeepFilter* f = g_new0(DeepFilter, 1);
    deep_query_copy(&f->q, q);
    if (!blank(q->name)) {
        gchar* name = g_strstrip(g_strdup(q->name));
        gchar* folded = fold(name);
        if (strpbrk(folded, "*?") != NULL) {
            f->name_glob = g_pattern_spec_new(folded);
            g_free(folded);
        } else {
            f->name_fold = folded;
        }
        g_free(name);
    }
    if (!blank(q->extensions)) {
        f->exts = g_ptr_array_new_with_free_func(g_free);
        gchar** parts = g_strsplit_set(q->extensions, ", ;", -1);
        for (gchar** p = parts; *p != NULL; p++) {
            const gchar* e = *p;
            while (*e == '*') e++; /* "*.c" */
            while (*e == '.') e++;
            if (*e == '\0') continue;
            gchar* folded = fold(e);
            g_ptr_array_add(f->exts, g_strconcat(".", folded, NULL));
            g_free(folded);
        }
        g_strfreev(parts);
        if (f->exts->len == 0) g_clear_pointer(&f->exts, g_ptr_array_unref);
    }
    f->exclude = path_globs_new(q->exclude);
    f->content = !blank(q->content);
    return f;
}

void deep_filter_free(DeepFilter* f) {
    if (f == NULL) return;
    deep_query_clear(&f->q);
    g_free(f->name_fold);
    if (f->name_glob != NULL) g_pattern_spec_free(f->name_glob);
    if (f->exts != NULL) g_ptr_array_unref(f->exts);
    path_globs_free(f->exclude);
    g_free(f);
}

gboolean deep_filter_excluded(const DeepFilter* f, const gchar* rel, gboolean is_dir) {
    if (f == NULL || f->exclude == NULL || rel == NULL) return FALSE;
    if (!is_dir) return path_globs_match(f->exclude, rel);
    /* "build/" matches a folder by what is under it */
    gchar* dir = g_strconcat(rel, "/", NULL);
    gboolean hit = path_globs_match(f->exclude, dir);
    g_free(dir);
    return hit;
}

gboolean deep_filter_has_content(const DeepFilter* f) {
    return f != NULL && f->content;
}

static gboolean ends_with(const gchar* s, gsize len, const gchar* end) {
    gsize n = strlen(end);
    return len > n && memcmp(s + len - n, end, n) == 0; /* not the whole name: ".c" is no C file */
}

gboolean deep_filter_match_entry(const DeepFilter* f, const gchar* name, gboolean is_dir, guint64 size, gint64 mtime) {
    if (f == NULL || name == NULL) return FALSE;
    const DeepQuery* q = &f->q;
    /* Criteria of files only: folders never pass them */
    gboolean files_only = q->kind == DEEP_KIND_FILES || f->exts != NULL || f->content || q->min_size >= 0 ||
                          q->max_size >= 0;
    if (is_dir ? files_only : q->kind == DEEP_KIND_FOLDERS) return FALSE;
    if (!is_dir) {
        if (q->min_size >= 0 && size < (guint64)q->min_size) return FALSE;
        if (q->max_size >= 0 && size > (guint64)q->max_size) return FALSE;
    }
    if (q->after > 0 && mtime < q->after) return FALSE;
    if (q->before > 0 && mtime >= q->before) return FALSE;
    if (f->name_fold == NULL && f->name_glob == NULL && f->exts == NULL) return TRUE;
    gchar* folded = fold(name);
    gsize len = strlen(folded);
    gboolean ok = TRUE;
    if (f->name_fold != NULL) ok = strstr(folded, f->name_fold) != NULL;
    if (ok && f->name_glob != NULL) ok = g_pattern_spec_match(f->name_glob, len, folded, NULL);
    if (ok && f->exts != NULL) {
        ok = FALSE;
        for (guint i = 0; i < f->exts->len && !ok; i++) ok = ends_with(folded, len, g_ptr_array_index(f->exts, i));
    }
    g_free(folded);
    return ok;
}

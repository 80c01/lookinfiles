/* Log file preview (see log_view.h). */
#include "formats/log_view.h"

#include <string.h>

static gboolean digits(const gchar* p, gsize n, gsize len) {
    if (n > len) {
        return FALSE;
    }
    for (gsize i = 0; i < n; i++) {
        if (!g_ascii_isdigit(p[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* "10:00:00" with optional fraction, at p; returns its length or 0 */
static gsize clock_length(const gchar* p, gsize len) {
    if (!(digits(p, 2, len) && len > 2 && p[2] == ':' && digits(p + 3, 2, len - 3))) {
        return 0;
    }
    gsize n = 5;
    if (n < len && p[n] == ':' && digits(p + n + 1, 2, len - n - 1)) {
        n += 3;
    }
    if (n < len && (p[n] == '.' || p[n] == ',')) {
        gsize k = n + 1;
        while (k < len && g_ascii_isdigit(p[k])) {
            k++;
        }
        n = k > n + 1 ? k : n;
    }
    return n;
}

/* Time zone after a time: "Z", "+02:00", "+0200", " UTC" */
static gsize zone_length(const gchar* p, gsize len) {
    if (len >= 1 && p[0] == 'Z') {
        return 1;
    }
    if (len >= 5 && (p[0] == '+' || p[0] == '-') && digits(p + 1, 2, len - 1)) {
        if (len >= 6 && p[3] == ':' && digits(p + 4, 2, len - 4)) {
            return 6;
        }
        return digits(p + 3, 2, len - 3) ? 5 : 0;
    }
    if (len >= 4 && memcmp(p, " UTC", 4) == 0) {
        return 4;
    }
    return 0;
}

static const gchar* const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

gsize log_view_timestamp_length(const gchar* line, gsize len) {
    gsize start = 0;
    gboolean bracket = len > 0 && line[0] == '[';
    if (bracket) {
        start = 1;
    }
    const gchar* p = line + start;
    gsize rest = len - start;
    gsize n = 0;
    /* 2026-10-29[T ]10:00:00[.123][Z] */
    if (digits(p, 4, rest) && rest > 10 && (p[4] == '-' || p[4] == '/') && digits(p + 5, 2, rest - 5) &&
        p[7] == p[4] && digits(p + 8, 2, rest - 8)) {
        n = 10;
        if (n < rest && (p[n] == 'T' || p[n] == ' ')) {
            gsize c = clock_length(p + n + 1, rest - n - 1);
            if (c > 0) {
                n += 1 + c;
                n += zone_length(p + n, rest - n);
            }
        }
    } else if (rest >= 15) {
        /* syslog: "Oct 29 10:00:00" */
        for (gsize m = 0; m < G_N_ELEMENTS(MONTHS); m++) {
            if (memcmp(p, MONTHS[m], 3) == 0 && p[3] == ' ' && (p[4] == ' ' || g_ascii_isdigit(p[4])) &&
                g_ascii_isdigit(p[5]) && p[6] == ' ') {
                gsize c = clock_length(p + 7, rest - 7);
                n = c > 0 ? 7 + c : 0;
                break;
            }
        }
    }
    if (n == 0) {
        /* 10:00:00.123 alone, or an epoch time 1761732000[.123] */
        n = clock_length(p, rest);
        if (n == 0 && digits(p, 10, rest) && (rest == 10 || !g_ascii_isdigit(p[10]))) {
            n = 10;
            if (n < rest && p[n] == '.') {
                gsize k = n + 1;
                while (k < rest && g_ascii_isdigit(p[k])) {
                    k++;
                }
                n = k;
            }
        }
    }
    if (n == 0) {
        return 0;
    }
    if (bracket) {
        return (start + n < len && line[start + n] == ']') ? start + n + 1 : 0;
    }
    return n;
}

static const struct {
    const gchar* word;
    LogLevel level;
} LEVELS[] = {
    { "ERROR", LOG_LEVEL_ERROR }, { "ERR", LOG_LEVEL_ERROR }, { "FATAL", LOG_LEVEL_ERROR },
    { "CRITICAL", LOG_LEVEL_ERROR }, { "CRIT", LOG_LEVEL_ERROR }, { "PANIC", LOG_LEVEL_ERROR },
    { "SEVERE", LOG_LEVEL_ERROR }, { "EMERG", LOG_LEVEL_ERROR }, { "ALERT", LOG_LEVEL_ERROR },
    { "FAILED", LOG_LEVEL_ERROR }, { "FAIL", LOG_LEVEL_ERROR },
    { "WARNING", LOG_LEVEL_WARNING }, { "WARN", LOG_LEVEL_WARNING },
    { "INFO", LOG_LEVEL_INFO }, { "NOTICE", LOG_LEVEL_INFO },
    { "DEBUG", LOG_LEVEL_DEBUG }, { "TRACE", LOG_LEVEL_DEBUG }, { "VERBOSE", LOG_LEVEL_DEBUG },
};

LogLevel log_view_line_level(const gchar* line, gsize len, gsize* word_start, gsize* word_len) {
    gsize from = log_view_timestamp_length(line, len);
    gsize to = MIN(len, from + 80);
    for (gsize i = from; i < to; i++) {
        if (!g_ascii_isalpha(line[i]) || (i > 0 && g_ascii_isalnum(line[i - 1]))) {
            continue;
        }
        gsize j = i;
        while (j < len && g_ascii_isalpha(line[j])) {
            j++;
        }
        gsize n = j - i;
        if (j < len && (g_ascii_isalnum(line[j]) || line[j] == '_')) {
            i = j;
            continue;
        }
        /* Upper case words, or Title / lower case inside brackets / before ':' */
        gboolean upper = TRUE;
        for (gsize k = i; k < j; k++) {
            upper &= g_ascii_isupper(line[k]) != 0;
        }
        gboolean marked = (i > 0 && (line[i - 1] == '[' || line[i - 1] == '<')) ||
                          (j < len && (line[j] == ']' || line[j] == ':' || line[j] == '>'));
        for (gsize l = 0; l < G_N_ELEMENTS(LEVELS); l++) {
            if (strlen(LEVELS[l].word) == n && g_ascii_strncasecmp(line + i, LEVELS[l].word, n) == 0 &&
                (upper || marked)) {
                if (word_start != NULL) {
                    *word_start = i;
                }
                if (word_len != NULL) {
                    *word_len = n;
                }
                return LEVELS[l].level;
            }
        }
        i = j;
    }
    return LOG_LEVEL_NONE;
}

void log_view_append(PreviewDoc* doc, const gchar* text, gsize len, LogStats* stats) {
    /* Start of the last LOG_VIEW_MAX_LINES lines */
    gsize start = len;
    guint count = 0;
    while (start > 0) {
        gsize k = start - 1;
        if (text[k] == '\n' && k + 1 < len) {
            if (++count >= LOG_VIEW_MAX_LINES) {
                start = k + 1;
                break;
            }
        }
        start = k;
    }
    LogStats s = { 0, 0, 0 };
    const gchar* p = text + start;
    const gchar* end = text + len;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        gsize n = nl != NULL ? (gsize)(nl - p) : (gsize)(end - p);
        gsize shown = (n > 0 && p[n - 1] == '\r') ? n - 1 : n;
        gint line_start = preview_doc_offset(doc);
        preview_doc_append_len(doc, p, (gssize)shown);
        preview_doc_append_len(doc, "\n", 1);
        s.lines++;
        gsize ts = log_view_timestamp_length(p, shown);
        if (ts > 0) {
            glong chars = g_utf8_strlen(p, (gssize)ts);
            preview_doc_add_span(doc, line_start, line_start + (gint)chars, PREVIEW_STYLE_DIM);
        }
        gsize ws = 0, wl = 0;
        LogLevel level = log_view_line_level(p, shown, &ws, &wl);
        if (level != LOG_LEVEL_NONE) {
            gint a = line_start + (gint)g_utf8_strlen(p, (gssize)ws);
            PreviewStyle style = level == LOG_LEVEL_ERROR ? PREVIEW_STYLE_ERROR
                                 : level == LOG_LEVEL_WARNING ? PREVIEW_STYLE_WARNING
                                 : level == LOG_LEVEL_INFO ? PREVIEW_STYLE_INFO : PREVIEW_STYLE_DIM;
            preview_doc_add_span(doc, a, a + (gint)wl, style);
            if (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_WARNING) {
                preview_doc_add_span(doc, a, a + (gint)wl, PREVIEW_STYLE_BOLD);
            }
            s.errors += level == LOG_LEVEL_ERROR;
            s.warnings += level == LOG_LEVEL_WARNING;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    if (stats != NULL) {
        *stats = s;
    }
}

/* CSV / TSV parsing (see csv.h). */
#include "csv.h"
#include "core/i18n.h"

#include <string.h>

#include "preview_doc.h"

static const gchar CANDIDATES[] = { ',', ';', '\t', '|' };

/* Separator counts per line outside quotes, for the first lines */
gchar csv_sniff_separator(const gchar* text, gsize len, const gchar* name) {
    const gchar* dot = name != NULL ? strrchr(name, '.') : NULL;
    if (dot != NULL && (g_ascii_strcasecmp(dot, ".tsv") == 0 || g_ascii_strcasecmp(dot, ".tab") == 0)) {
        return '\t';
    }
    if (dot != NULL && g_ascii_strcasecmp(dot, ".psv") == 0) {
        return '|';
    }
    enum { LINES = 20 };
    guint counts[G_N_ELEMENTS(CANDIDATES)][LINES] = { { 0 } };
    guint n_lines = 0;
    gboolean quoted = FALSE;
    for (gsize i = 0; i < len && n_lines < LINES; i++) {
        gchar c = text[i];
        if (c == '"') {
            quoted = !quoted;
        } else if (!quoted && c == '\n') {
            n_lines++;
        } else if (!quoted) {
            for (gsize k = 0; k < G_N_ELEMENTS(CANDIDATES); k++) {
                if (c == CANDIDATES[k]) {
                    counts[k][n_lines]++;
                }
            }
        }
    }
    if (n_lines < LINES && len > 0 && text[len - 1] != '\n') {
        n_lines++; /* last line without a line break */
    }
    gchar best = ',';
    guint best_score = 0;
    for (gsize k = 0; k < G_N_ELEMENTS(CANDIDATES); k++) {
        /* Lines with the same non-zero count as the first line */
        guint first = counts[k][0];
        if (first == 0) {
            continue;
        }
        guint same = 0;
        for (guint l = 0; l < n_lines; l++) {
            same += (counts[k][l] == first);
        }
        guint score = same * 4 + MIN(first, 3u);
        if (score > best_score) {
            best_score = score;
            best = CANDIDATES[k];
        }
    }
    return best;
}

CsvData* csv_parse(const gchar* text, gsize len, gchar separator, guint max_rows) {
    CsvData* data = g_new0(CsvData, 1);
    data->rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
    GPtrArray* cells = g_ptr_array_new();
    GString* cell = g_string_new(NULL);
    gboolean quoted = FALSE;
    gboolean keep = max_rows > 0;
    gboolean row_has_content = FALSE;
    gsize i = 0;
    if (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        i = 3; /* byte order mark */
    }
    for (;; i++) {
        gboolean at_end = (i >= len);
        gchar c = at_end ? '\n' : text[i];
        if (quoted && !at_end) {
            if (c == '"') {
                if (i + 1 < len && text[i + 1] == '"') {
                    if (keep) {
                        g_string_append_c(cell, '"');
                    }
                    i++;
                } else {
                    quoted = FALSE;
                }
            } else if (keep) {
                g_string_append_c(cell, c);
            }
            continue;
        }
        if (c == '"' && !at_end) {
            quoted = TRUE;
            row_has_content = TRUE;
        } else if (c == separator && !at_end) {
            row_has_content = TRUE;
            if (keep) {
                g_ptr_array_add(cells, g_strdup(cell->str));
            }
            g_string_truncate(cell, 0);
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < len && text[i + 1] == '\n') {
                i++;
            }
            if (row_has_content || cell->len > 0) {
                if (keep) {
                    g_ptr_array_add(cells, g_strdup(cell->str));
                    data->n_cols = MAX(data->n_cols, cells->len);
                    g_ptr_array_add(cells, NULL);
                    g_ptr_array_add(data->rows, g_ptr_array_steal(cells, NULL));
                    keep = data->rows->len < max_rows;
                }
                data->n_rows++;
            }
            g_string_truncate(cell, 0);
            row_has_content = FALSE;
            if (at_end) {
                break;
            }
        } else {
            row_has_content = TRUE;
            if (keep) {
                g_string_append_c(cell, c);
            }
        }
    }
    g_ptr_array_free(cells, TRUE);
    g_string_free(cell, TRUE);
    return data;
}

void csv_data_free(CsvData* data) {
    if (data == NULL) {
        return;
    }
    g_ptr_array_free(data->rows, TRUE);
    g_free(data);
}

static gboolean is_blank(const gchar* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return *s == '\0';
}

gboolean csv_has_header(const CsvData* data) {
    if (data->rows->len < 2) {
        return FALSE;
    }
    gchar** first = g_ptr_array_index(data->rows, 0);
    guint n = g_strv_length(first);
    if (n == 0) {
        return FALSE;
    }
    for (guint c = 0; c < n; c++) {
        if (is_blank(first[c]) || preview_cell_is_number(first[c])) {
            return FALSE;
        }
    }
    for (guint r = 1; r < data->rows->len; r++) {
        gchar** row = g_ptr_array_index(data->rows, r);
        for (guint c = 0; row[c] != NULL; c++) {
            if (preview_cell_is_number(row[c])) {
                return TRUE;
            }
        }
    }
    /* Text only: a header when its names are all different */
    for (guint a = 0; a < n; a++) {
        for (guint b = a + 1; b < n; b++) {
            if (g_strcmp0(first[a], first[b]) == 0) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

const gchar* csv_separator_name(gchar separator) {
    switch (separator) {
    case '\t': return NC_("csv separator", "tab");
    case ';':  return NC_("csv separator", "semicolon");
    case '|':  return NC_("csv separator", "pipe");
    default:   return NC_("csv separator", "comma");
    }
}

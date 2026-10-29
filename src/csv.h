#ifndef CSV_H
#define CSV_H

#include <glib.h>

/* Comma / tab / semicolon / pipe separated values (RFC 4180): quoted fields
 * with doubled quotes, separators and line breaks inside quotes, CRLF line
 * ends. Pure GLib, unit-tested. */

/* Separator of a text: '\t' for .tsv / .tab names, '|' for .psv, else the
 * candidate (',', ';', '\t', '|') found the same number of times on most of
 * the first lines (outside quotes). ',' when nothing stands out. */
gchar csv_sniff_separator(const gchar* text, gsize len, const gchar* name);

typedef struct {
    GPtrArray* rows;      /* GStrv per row (first max_rows rows) */
    guint n_rows;         /* rows in the whole text */
    guint n_cols;         /* most cells in a kept row */
} CsvData;

/* Parse the text, keeping the cells of the first max_rows rows only (all rows
 * are counted). A last line cut by the end of the text is kept. */
CsvData* csv_parse(const gchar* text, gsize len, gchar separator, guint max_rows);
void csv_data_free(CsvData* data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CsvData, csv_data_free)

/* TRUE if the first row looks like column names: no numeric and no empty
 * cell, while another row has a numeric cell, or all its names differ */
gboolean csv_has_header(const CsvData* data);

/* "comma", "tab", "semicolon", "pipe" */
const gchar* csv_separator_name(gchar separator);

#endif /* CSV_H */

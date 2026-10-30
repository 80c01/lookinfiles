#ifndef SPREADSHEET_H
#define SPREADSHEET_H

#include <glib.h>

#include "preview_doc.h"

/* Spreadsheets for the preview: Excel (.xlsx, Office Open XML) and
 * OpenDocument (.ods). Every sheet's name, and the cells of the first rows
 * of the first sheets as the application shows them: shared and inline
 * strings, numbers with their number format (decimals, thousands, percent),
 * dates and times, booleans, formula results. Reading stops after max_rows
 * rows of a sheet. Pure GLib, unit-tested. */

typedef struct {
    gchar* name;
    GPtrArray* rows;       /* GStrv of n_cols cells ("" for empty), trailing empty rows dropped */
    guint n_cols;          /* columns up to the last non-empty cell */
    gboolean truncated;    /* more rows than were read */
    gboolean loaded;       /* cells read (only the first sheets are) */
} Sheet;

typedef struct {
    const gchar* format;   /* "Excel workbook", "OpenDocument spreadsheet" */
    GPtrArray* sheets;     /* Sheet* */
} Workbook;

/* Read a workbook: the cells of the first max_sheets sheets, at most max_rows
 * rows and SPREADSHEET_MAX_COLS columns each. NULL with error set if the file
 * is not a readable spreadsheet. */
Workbook* spreadsheet_read(const gchar* path, guint max_sheets, guint max_rows, GError** error);
void workbook_free(Workbook* book);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Workbook, workbook_free)

#define SPREADSHEET_MAX_COLS 100

/* The loaded sheets as tables (column letters, row numbers), each under its
 * name when there are several, with the truncation notes (max_rows: the
 * rows read per sheet, shown: the sheets read) */
void spreadsheet_append_sheets(PreviewDoc* doc, const Workbook* book, guint shown, guint max_rows);

/* Empty workbook (format: a static string) and a new sheet in it, for the
 * readers of other formats (xls_reader) */
Workbook* spreadsheet_workbook_new(const gchar* format);
Sheet* spreadsheet_sheet_add(Workbook* book, const gchar* name);

/* Column letters: 0 -> "A", 25 -> "Z", 26 -> "AA". Caller frees. */
gchar* spreadsheet_column_name(guint index);

/* Column index of a cell reference ("C12" -> 2), -1 if invalid */
gint spreadsheet_column_index(const gchar* ref);

/* Display text of an Excel number with a number format: format_code is the
 * custom code or NULL, builtin_id the numFmtId. Dates use the 1900 system. */
gchar* spreadsheet_format_number(gdouble value, guint builtin_id, const gchar* format_code);

#endif /* SPREADSHEET_H */

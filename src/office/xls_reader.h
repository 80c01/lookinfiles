#ifndef XLS_READER_H
#define XLS_READER_H

/* Excel 97-2003 (BIFF8, "Workbook" stream) and Excel 5.0 / 95 (BIFF5,
 * "Book" stream) workbooks, as the same Workbook as .xlsx for the preview:
 * sheet names (BOUNDSHEET), shared strings (SST with CONTINUE), LABELSST,
 * LABEL, NUMBER, RK, MULRK, BOOLERR and cached FORMULA results, displayed
 * with their number formats (FORMAT / XF: decimals, percents, dates, the
 * 1904 date system). Pure GLib, unit-tested. */
#include <glib.h>

#include "office/ole_reader.h"
#include "office/spreadsheet.h"

/* The cells of the first max_sheets worksheets, at most max_rows rows and
 * SPREADSHEET_MAX_COLS columns each. NULL with error set when the workbook
 * cannot be read; *encrypted (may be NULL) tells when it is
 * password-protected (FILEPASS). */
Workbook* xls_reader_read(OleReader* ole, guint max_sheets, guint max_rows, gboolean* encrypted, GError** error);

#endif /* XLS_READER_H */

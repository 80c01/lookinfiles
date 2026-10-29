#ifndef ORG_TABLE_H
#define ORG_TABLE_H

/* Org-mode tables ("| a | b |" rows and "|---+---|" rules): alignment, cell
 * navigation, row and column editing. Works on the same lines and cursor as
 * org_doc.h; every command first re-aligns the table. Pure GLib. */
#include <glib.h>
#include "org_doc.h"

gboolean org_table_is_line(const gchar* line);
/* Table rows around line: [*start, *end). FALSE if line is not in a table. */
gboolean org_table_bounds(GPtrArray* lines, gint line, gint* start, gint* end);

gboolean org_table_align(GPtrArray* lines, OrgPos* pos);
/* TAB / Shift+TAB: next / previous cell (a new row after the last one) */
gboolean org_table_next_field(GPtrArray* lines, OrgPos* pos);
gboolean org_table_prev_field(GPtrArray* lines, OrgPos* pos);
/* RET: same column in the next row (inserted when needed) */
gboolean org_table_next_row(GPtrArray* lines, OrgPos* pos);
/* Move the row / column under the cursor (dir -1 up/left, +1 down/right) */
gboolean org_table_move_row(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg);
gboolean org_table_move_column(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg);
/* Insert an empty row above the cursor / delete the cursor row */
gboolean org_table_insert_row(GPtrArray* lines, OrgPos* pos);
gboolean org_table_delete_row(GPtrArray* lines, OrgPos* pos);
/* Insert an empty column left of the cursor / delete the cursor column */
gboolean org_table_insert_column(GPtrArray* lines, OrgPos* pos);
gboolean org_table_delete_column(GPtrArray* lines, OrgPos* pos);
/* Insert a horizontal rule below the cursor row */
gboolean org_table_insert_hline(GPtrArray* lines, OrgPos* pos);

#endif /* ORG_TABLE_H */

#ifndef LOG_VIEW_H
#define LOG_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* Log files for the preview: timestamps at the start of lines dimmed (ISO
 * 8601, syslog "Oct 29 10:00:00", bracketed or epoch times), level words
 * coloured (errors red, warnings yellow, info blue, debug dimmed), and counts
 * of errors and warnings. The viewer shows the end of a large log: that is
 * where the latest entries are. Pure GLib, unit-tested. */

typedef enum {
    LOG_LEVEL_NONE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
} LogLevel;

typedef struct {
    guint lines;
    guint errors;
    guint warnings;
} LogStats;

/* Most lines shown (the last ones) */
#define LOG_VIEW_MAX_LINES 5000

/* Length of the timestamp at the start of a line (0 if none) */
gsize log_view_timestamp_length(const gchar* line, gsize len);

/* Level of a line and the position of its level word (*word_start,
 * *word_len), searched in the first 80 characters after the timestamp */
LogLevel log_view_line_level(const gchar* line, gsize len, gsize* word_start, gsize* word_len);

/* Append the last LOG_VIEW_MAX_LINES lines of text, coloured; stats (may be
 * NULL) counts the lines shown */
void log_view_append(PreviewDoc* doc, const gchar* text, gsize len, LogStats* stats);

#endif /* LOG_VIEW_H */

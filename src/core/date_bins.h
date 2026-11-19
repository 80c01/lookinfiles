#ifndef DATE_BINS_H
#define DATE_BINS_H

/* Calendar groups of dates for a histogram: days, months or years. A day
 * is an integer YYYYMMDD (local time), a group a key: YYYYMMDD for a day,
 * YYYYMM for a month, YYYY for a year. Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    DATE_BIN_DAY = 0,
    DATE_BIN_MONTH,
    DATE_BIN_YEAR,
} DateBinUnit;

/* The local day of t (Unix seconds) as YYYYMMDD */
gint date_bins_day_of(gint64 t);
/* The same, with the start (included) and the end (excluded) of that day
 * in Unix seconds, so that the next times of the same day need no call */
gint date_bins_day_span(gint64 t, gint64* start, gint64* end);

/* The finest unit showing the days first..last (first <= last) in at most
 * max groups; years when even they are more (the caller merges the oldest) */
DateBinUnit date_bins_unit(gint first, gint last, guint max);

/* The group of day */
gint date_bins_key(gint day, DateBinUnit unit);
/* The group after key */
gint date_bins_next(gint key, DateBinUnit unit);
/* Number of groups from key a to key b (both included; 0 when b < a) */
guint date_bins_span(gint a, gint b, DateBinUnit unit);
/* The first and the last day of the group key */
void date_bins_days(gint key, DateBinUnit unit, gint* first, gint* last);

/* Short label of a group: "11-04" (a day: month-day), "2025-03", "2025" */
gchar* date_bins_label(gint key, DateBinUnit unit);
/* Full name of a group: "2025-11-04", "2025-03", "2025" */
gchar* date_bins_name(gint key, DateBinUnit unit);

#endif /* DATE_BINS_H */

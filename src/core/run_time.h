#ifndef RUN_TIME_H
#define RUN_TIME_H

/* The time a run took, as shown after a notebook cell: "0.4s", "12.3s",
 * "1m 05s", "2h 03m". Pure logic, no widgets: unit-tested in tests/. */
#include <glib.h>

/* usec: microseconds (negative: 0). Under a minute: tenths of a second
 * (rounded down, so a run never shows longer than it took); then whole
 * seconds, then whole minutes. Caller frees. */
gchar* run_time_format(gint64 usec);

#endif /* RUN_TIME_H */

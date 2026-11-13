#ifndef SYSMON_INDICATOR_H
#define SYSMON_INDICATOR_H

/* The CPU chart of the top bar: the use of all CPUs over the last minute
 * (a sparkline read every 2 seconds from /proc/stat) and its last value; the
 * tooltip adds the load average and the memory in use. A click opens the
 * System Monitor tab (sysmon_tab.h), or closes it when shown. Hidden where
 * /proc is missing (not Linux). With LOOKIN_SYSMON_FROZEN set (UI tests),
 * nothing is read, so screenshots stay the same. */
#include "core/app_types.h"

GtkWidget* sysmon_indicator_new(AppData* app);
/* Lit while the System Monitor tab is open (the top bar's pill-active) */
void sysmon_indicator_sync(GtkWidget* indicator, AppData* app);

#endif /* SYSMON_INDICATOR_H */

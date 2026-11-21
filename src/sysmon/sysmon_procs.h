#ifndef SYSMON_PROCS_H
#define SYSMON_PROCS_H

/* The process list of the System Monitor tab: a GtkColumnView (name, PID,
 * user, CPU %, memory, threads, state) sorted by a click on a column header,
 * by CPU at first, filtered by the words of a search field. Read only: the rows are kept from one update to the
 * next (by PID) and changed in place, the selection and the scroll position
 * stay. */
#include "core/app_types.h"
#include "core/sysstat.h"

/* The list in its scrolled window; the navigation keys of the global mode
 * move in it (focus_route_attach_list()) */
GtkWidget* sysmon_procs_new(AppData* app);
/* The column view inside, to focus */
GtkWidget* sysmon_procs_view(GtkWidget* procs);
/* Memory page size of the machine shown (its rss_pages are in pages) */
void sysmon_procs_set_page_size(GtkWidget* procs, guint64 page_size);
/* Show these processes (SysProc*, read only) */
void sysmon_procs_update(GtkWidget* procs, GPtrArray* list);
/* Show only the processes whose name, user or PID hold every word of text
 * (any case); NULL or blank: every process */
void sysmon_procs_set_filter(GtkWidget* procs, const gchar* text);
/* A filter is set */
gboolean sysmon_procs_filtered(GtkWidget* procs);
/* Rows shown (after the filter) and processes listed */
void sysmon_procs_counts(GtkWidget* procs, guint* shown, guint* total);

#endif /* SYSMON_PROCS_H */

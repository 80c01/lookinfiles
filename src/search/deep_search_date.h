#ifndef DEEP_SEARCH_DATE_H
#define DEEP_SEARCH_DATE_H

/* Date picker button of the Deep Search form: a GtkCalendar popover next to
 * a date entry; picking a day writes YYYY-MM-DD into the entry, and the
 * button below clears it. */
#include <gtk/gtk.h>

GtkWidget* deep_search_calendar_button(GtkWidget* entry);

#endif /* DEEP_SEARCH_DATE_H */

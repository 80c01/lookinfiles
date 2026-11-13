#ifndef CLOCK_PILL_H
#define CLOCK_PILL_H

/* The clock and the timer of the top bar.
 *
 * The clock shows the time (HH:MM, the date in its tooltip). The timer
 * starts at launch with the duration of the preferences (settings.ini,
 * [Clock] timer-minutes) and counts down (countdown.h): its last minute is
 * shown in reverse colours, its last ten seconds blink (steady when GTK
 * animations are off), and when it expires it restarts at once. Click:
 * pause / resume; right click: restart. LOOKIN_CLOCK_FROZEN in the
 * environment shows a fixed time and a stopped timer (UI screenshots). */
#include <gtk/gtk.h>

/* The row of both; minutes 0: no timer */
GtkWidget* clock_pill_new(gint minutes);

/* Restart the timer of pill with minutes (0 hides it) */
void clock_pill_set_timer(GtkWidget* pill, gint minutes);

#endif /* CLOCK_PILL_H */

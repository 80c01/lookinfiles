#ifndef CLOCK_PILL_H
#define CLOCK_PILL_H

/* The clock and the three timers of the top bar.
 *
 * The clock shows the time (24-hour or 12-hour, with or without the seconds,
 * as the preferences say; the date in its tooltip), or, when the time is
 * hidden, an icon while no timer shows and nothing while one does. Each timer
 * counts down (countdown.h): its last minute is shown in reverse colours, its
 * last ten seconds blink (steady when GTK animations are off), and when it
 * expires it restarts at once or, when its expiry action is Stop, stays at
 * 0:00 in reverse colours, or, when it is Count On, counts on below 0
 * (-0:01, -0:02...) in reverse colours. Timer 1 starts at launch with the duration and the
 * expiry action of the preferences (settings.ini, [Clock] timer-minutes,
 * timer-expiry); timers 2 and 3 start
 * off. Only the timers that run show. A click on a timer (or on the clock)
 * opens a popover with a page per timer: its state, preset durations (5, 15,
 * 25, 60 min), a free duration and Start, the expiry action (Restart /
 * Stop / Count On), Pause / Reset / Off (what is picked there lasts until the next
 * launch, or the next Preferences change for timer 1); a right click on a
 * timer pauses / resumes it. LOOKIN_CLOCK_FROZEN in the environment shows a
 * fixed time and stopped timers (UI screenshots). */
#include "core/settings_store.h"

#include <gtk/gtk.h>

/* The row of the clock and the timers, with the [Clock] values of settings */
GtkWidget* clock_pill_new(const Settings* settings);

/* Restart timer 1 of pill with minutes (0 hides it) */
void clock_pill_set_timer(GtkWidget* pill, gint minutes);

/* The expiry action of timer 1, without restarting it */
void clock_pill_set_expiry(GtkWidget* pill, SettingsExpiry expiry);

/* Show the time as the [Clock] values of settings say */
void clock_pill_set_display(GtkWidget* pill, const Settings* settings);

#endif /* CLOCK_PILL_H */

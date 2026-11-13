#ifndef COUNTDOWN_H
#define COUNTDOWN_H

/* A countdown (the timers of the top bar, clock_pill.h): it counts down
 * from its duration and, when it expires, restarts at once (repeat) or stays
 * at 0:00 (stop). Times are
 * wall-clock microseconds (g_get_real_time()), so the countdown stays right
 * across a suspend of the computer. Pure logic, no widgets: unit-tested in
 * tests/. */
#include <glib.h>

#define COUNTDOWN_SECOND G_GINT64_CONSTANT(1000000)
#define COUNTDOWN_WARNING_SECONDS 60 /* last minute: reverse colours */
#define COUNTDOWN_BLINK_SECONDS 10   /* last seconds: blinking */

typedef struct {
    gint64 duration; /* > 0 */
    gint64 deadline; /* running: end of the current cycle */
    gint64 left;     /* paused: time left */
    gboolean paused;
    guint cycle;     /* 1 during the first run, 2 after the first restart... */
    gboolean stop;   /* on expiry: stay at 0 (TRUE) or restart (FALSE) */
} Countdown;

/* Start a first cycle of duration (clamped to at least one second) at now,
 * restarting on expiry */
void countdown_start(Countdown* c, gint64 duration, gint64 now);

/* Time left at now, in (0, duration]. Past the deadline the countdown has
 * restarted (cycle grows by the number of cycles that ended), or, when it
 * stops on expiry, 0. */
gint64 countdown_left(Countdown* c, gint64 now);

/* Expired and stopped at 0 */
gboolean countdown_expired(Countdown* c, gint64 now);

/* Stop at 0 or restart on expiry; switched to restart while expired, a new
 * cycle starts at now */
void countdown_set_stop(Countdown* c, gboolean stop, gint64 now);

/* Freeze or restart the count (no effect when already in that state) */
void countdown_pause(Countdown* c, gint64 now);
void countdown_resume(Countdown* c, gint64 now);

/* Whole seconds shown for left (rounded up: the first second shows the
 * full duration, the last one shows 0:01) */
gint64 countdown_seconds(gint64 left);

/* Reverse colours for left: the whole last minute; in the last seconds only
 * the first half of each second when blink (otherwise steady); steady at 0 */
gboolean countdown_inverted(gint64 left, gboolean blink);

/* Microseconds from left until the text or the colours shown change */
gint64 countdown_next_change(gint64 left, gboolean blink);

/* "4:59", "59:59", or "1:59:59" when duration is 100 minutes or more (the
 * width stays the same during a cycle). Caller frees. */
gchar* countdown_format(gint64 left, gint64 duration);

#endif /* COUNTDOWN_H */

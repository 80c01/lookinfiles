#ifndef COUNTDOWN_H
#define COUNTDOWN_H

/* A countdown (the timers of the top bar, clock_pill.h): it counts down
 * from its duration and, when it expires, restarts at once (repeat), stays
 * at 0:00 (stop) or counts on below 0 (overtime: -0:01, -0:02...). Times are
 * wall-clock microseconds (g_get_real_time()), so the countdown stays right
 * across a suspend of the computer. Pure logic, no widgets: unit-tested in
 * tests/. */
#include <glib.h>

#define COUNTDOWN_SECOND G_GINT64_CONSTANT(1000000)
#define COUNTDOWN_WARNING_SECONDS 60 /* last minute: reverse colours */
#define COUNTDOWN_BLINK_SECONDS 10   /* last seconds: blinking */

typedef enum {
    COUNTDOWN_RESTART,  /* a new cycle at once */
    COUNTDOWN_STOP,     /* stays at 0 */
    COUNTDOWN_OVERTIME, /* counts on below 0 */
} CountdownExpiry;

typedef struct {
    gint64 duration; /* > 0 */
    gint64 deadline; /* running: end of the current cycle */
    gint64 left;     /* paused: time left (< 0 in overtime) */
    gboolean paused;
    guint cycle;     /* 1 during the first run, 2 after the first restart... */
    CountdownExpiry expiry; /* what happens at 0 */
} Countdown;

/* Start a first cycle of duration (clamped to at least one second) at now,
 * restarting on expiry */
void countdown_start(Countdown* c, gint64 duration, gint64 now);

/* Time left at now, in (0, duration]. Past the deadline the countdown has
 * restarted (cycle grows by the number of cycles that ended), or it is 0
 * when it stops on expiry, or minus the time since the deadline in
 * overtime. */
gint64 countdown_left(Countdown* c, gint64 now);

/* Expired and stopped at 0 */
gboolean countdown_expired(Countdown* c, gint64 now);

/* Expired and counting on below 0 */
gboolean countdown_overtime(Countdown* c, gint64 now);

/* What happens on expiry; switched to restart while at or below 0, a new
 * cycle starts at now; switched to stop while in overtime, it shows 0 */
void countdown_set_expiry(Countdown* c, CountdownExpiry expiry, gint64 now);

/* Freeze or restart the count (no effect when already in that state) */
void countdown_pause(Countdown* c, gint64 now);
void countdown_resume(Countdown* c, gint64 now);

/* Whole seconds shown for left (rounded up: the first second shows the
 * full duration, the last one shows 0:01, the first second of overtime
 * 0:00, the next one -0:01) */
gint64 countdown_seconds(gint64 left);

/* Reverse colours for left: the whole last minute; in the last seconds only
 * the first half of each second when blink (otherwise steady); steady at 0
 * and below */
gboolean countdown_inverted(gint64 left, gboolean blink);

/* Microseconds from left until the text or the colours shown change */
gint64 countdown_next_change(gint64 left, gboolean blink);

/* "4:59", "59:59", or "1:59:59" when duration is 100 minutes or more (the
 * width stays the same during a cycle); "-0:59" in overtime, "-1:00:00"
 * from an hour over whatever the duration. Caller frees. */
gchar* countdown_format(gint64 left, gint64 duration);

#endif /* COUNTDOWN_H */

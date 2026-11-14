/* A countdown that restarts, stops or counts on below 0 on expiry (see countdown.h). */
#include "core/countdown.h"

#define HALF_SECOND (COUNTDOWN_SECOND / 2)

void countdown_start(Countdown* c, gint64 duration, gint64 now) {
    c->duration = MAX(duration, COUNTDOWN_SECOND);
    c->deadline = now + c->duration;
    c->left = c->duration;
    c->paused = FALSE;
    c->cycle = 1;
    c->expiry = COUNTDOWN_RESTART;
}

gint64 countdown_left(Countdown* c, gint64 now) {
    if (c->paused) return c->left;
    if (now >= c->deadline) {
        if (c->expiry == COUNTDOWN_STOP) return 0;
        if (c->expiry == COUNTDOWN_OVERTIME) return c->deadline - now;
        gint64 ended = (now - c->deadline) / c->duration + 1;
        c->deadline += ended * c->duration;
        c->cycle += (guint)MIN(ended, (gint64)G_MAXUINT - c->cycle);
    } else if (c->deadline - now > c->duration) {
        c->deadline = now + c->duration; /* the clock went back: restart this cycle */
    }
    return c->deadline - now;
}

gboolean countdown_expired(Countdown* c, gint64 now) {
    return c->expiry == COUNTDOWN_STOP && countdown_left(c, now) <= 0;
}

gboolean countdown_overtime(Countdown* c, gint64 now) {
    return c->expiry == COUNTDOWN_OVERTIME && countdown_left(c, now) <= 0;
}

void countdown_set_expiry(Countdown* c, CountdownExpiry expiry, gint64 now) {
    gboolean at_zero = (countdown_left(c, now) <= 0);
    if (expiry == COUNTDOWN_RESTART && at_zero) {
        c->deadline = now + c->duration;
        c->left = c->duration;
        c->cycle += (c->cycle < G_MAXUINT) ? 1 : 0;
    } else if (expiry == COUNTDOWN_STOP && at_zero) {
        c->left = 0; /* paused in overtime */
    }
    c->expiry = expiry;
}

void countdown_pause(Countdown* c, gint64 now) {
    if (c->paused) return;
    c->left = countdown_left(c, now);
    c->paused = TRUE;
}

void countdown_resume(Countdown* c, gint64 now) {
    if (!c->paused) return;
    c->deadline = now + c->left;
    c->paused = FALSE;
}

gint64 countdown_seconds(gint64 left) {
    if (left <= 0) return -(-left / COUNTDOWN_SECOND); /* C division rounds toward 0: up here */
    return (left + COUNTDOWN_SECOND - 1) / COUNTDOWN_SECOND;
}

/* Time left within the second shown, in (0, COUNTDOWN_SECOND] */
static gint64 within_second(gint64 left) {
    return left - (countdown_seconds(left) - 1) * COUNTDOWN_SECOND;
}

gboolean countdown_inverted(gint64 left, gboolean blink) {
    gint64 secs = countdown_seconds(left);
    if (secs <= 0) return TRUE;
    if (secs > COUNTDOWN_WARNING_SECONDS) return FALSE;
    if (!blink || secs > COUNTDOWN_BLINK_SECONDS) return TRUE;
    return within_second(left) > HALF_SECOND;
}

gint64 countdown_next_change(gint64 left, gboolean blink) {
    if (left <= 0) return COUNTDOWN_SECOND - (-left) % COUNTDOWN_SECOND;
    gint64 rest = within_second(left);
    if (blink && countdown_seconds(left) <= COUNTDOWN_BLINK_SECONDS && rest > HALF_SECOND) {
        return rest - HALF_SECOND;
    }
    return rest;
}

gchar* countdown_format(gint64 left, gint64 duration) {
    gint64 secs = countdown_seconds(left);
    const gchar* sign = (secs < 0) ? "-" : "";
    secs = ABS(secs);
    if (countdown_seconds(duration) >= 100 * 60) {
        return g_strdup_printf("%s%" G_GINT64_FORMAT ":%02d:%02d", sign, secs / 3600, (gint)(secs / 60 % 60),
                               (gint)(secs % 60));
    }
    return g_strdup_printf("%s%" G_GINT64_FORMAT ":%02d", sign, secs / 60, (gint)(secs % 60));
}

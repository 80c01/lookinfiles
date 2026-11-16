/* A countdown that restarts, stops or counts on below 0 on expiry (see countdown.h). */
#include "core/countdown.h"

#include <string.h>

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
    if (countdown_seconds(duration) >= 100 * 60 || (*sign != '\0' && secs >= 3600)) {
        return g_strdup_printf("%s%" G_GINT64_FORMAT ":%02d:%02d", sign, secs / 3600, (gint)(secs / 60 % 60),
                               (gint)(secs % 60));
    }
    return g_strdup_printf("%s%" G_GINT64_FORMAT ":%02d", sign, secs / 60, (gint)(secs % 60));
}

gint64 countdown_until(GTimeZone* tz, gint hour, gint minute, gint64 now) {
    GDateTime* utc = g_date_time_new_from_unix_utc_usec(now);
    GDateTime* local = g_date_time_to_timezone(utc, tz);
    gint64 deadline = now + 24 * 3600 * COUNTDOWN_SECOND;
    for (gint day = 0; day <= 2; day++) {
        GDateTime* base = g_date_time_add_days(local, day);
        GDateTime* target = g_date_time_new(tz, g_date_time_get_year(base), g_date_time_get_month(base),
                                            g_date_time_get_day_of_month(base), hour, minute, 0);
        gint64 at = (target != NULL) ? g_date_time_to_unix_usec(target) : now;
        if (target != NULL) g_date_time_unref(target);
        g_date_time_unref(base);
        if (at > now) {
            deadline = at;
            break;
        }
    }
    g_date_time_unref(local);
    g_date_time_unref(utc);
    return deadline;
}

/* Strips a trailing "am" / "pm" ("a.m.", "p", any case) from text; returns
 * 1 for am, 2 for pm, 0 for none */
static gint strip_meridiem(gchar* text) {
    gchar* end = text + strlen(text);
    gchar* letters = end;
    while (letters > text && (g_ascii_isalpha(letters[-1]) || letters[-1] == '.')) letters--;
    gchar word[8] = {0};
    gsize n = 0;
    for (const gchar* s = letters; s < end && n < sizeof(word) - 1; s++) {
        if (*s != '.') word[n++] = g_ascii_tolower(*s);
    }
    if (end - letters >= (gssize)sizeof(word)) return 0;
    gint meridiem = (g_strcmp0(word, "am") == 0 || g_strcmp0(word, "a") == 0)   ? 1
                    : (g_strcmp0(word, "pm") == 0 || g_strcmp0(word, "p") == 0) ? 2
                                                                                : 0;
    if (meridiem != 0) {
        *letters = '\0';
        g_strchomp(text);
    }
    return meridiem;
}

gboolean countdown_parse_time(const gchar* text, gint* hour, gint* minute) {
    if (text == NULL) return FALSE;
    gchar* copy = g_strstrip(g_strdup(text));
    gint meridiem = strip_meridiem(copy);
    gint h = -1, m = 0, digits = 0;
    gboolean ok = TRUE;
    const gchar* s = copy;
    gint value = 0;
    while (g_ascii_isdigit(*s) && digits < 4) {
        value = value * 10 + (*s++ - '0');
        digits++;
    }
    if (digits == 0) {
        ok = FALSE;
    } else if (*s == '\0') {
        /* "9", "14", "930", "1430" */
        h = (digits <= 2) ? value : value / 100;
        m = (digits <= 2) ? 0 : value % 100;
    } else if (digits <= 2 && (*s == ':' || *s == 'h' || *s == 'H' || *s == '.')) {
        h = value;
        s++;
        gint md = 0;
        while (g_ascii_isdigit(*s) && md < 2) {
            m = m * 10 + (*s++ - '0');
            md++;
        }
        ok = (*s == '\0' && (md == 2 || (md == 0 && (s[-1] == 'h' || s[-1] == 'H'))));
    } else {
        ok = FALSE;
    }
    g_free(copy);
    if (!ok || m > 59) return FALSE;
    if (meridiem != 0) {
        if (h < 1 || h > 12) return FALSE;
        h = (h % 12) + ((meridiem == 2) ? 12 : 0);
    } else if (h > 23) {
        return FALSE;
    }
    *hour = h;
    *minute = m;
    return TRUE;
}

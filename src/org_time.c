/* Org-mode timestamps: parsing, formatting, arithmetic, repeaters, date prompt. */
#include "org_time.h"

#include <string.h>

static const gchar* DAY_NAMES[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

const gchar* org_day_name(gint year, gint month, gint day) {
    if (!g_date_valid_dmy((GDateDay)day, (GDateMonth)month, (GDateYear)year)) return "???";
    GDate d;
    g_date_clear(&d, 1);
    g_date_set_dmy(&d, (GDateDay)day, (GDateMonth)month, (GDateYear)year);
    return DAY_NAMES[g_date_get_weekday(&d) - 1];
}

/* Read exactly n digits at s[*i] */
static gboolean read_digits(const gchar* s, gsize len, gsize* i, gint n, gint* out) {
    gint v = 0;
    for (gint k = 0; k < n; k++) {
        if (*i >= len || !g_ascii_isdigit(s[*i])) return FALSE;
        v = v * 10 + (s[*i] - '0');
        (*i)++;
    }
    *out = v;
    return TRUE;
}

/* Read 1 or 2 digit hour then ":MM" */
static gboolean read_time(const gchar* s, gsize len, gsize* i, gint* h, gint* m) {
    gsize j = *i;
    gint v = 0, nd = 0;
    while (j < len && g_ascii_isdigit(s[j]) && nd < 2) {
        v = v * 10 + (s[j] - '0');
        j++;
        nd++;
    }
    if (nd == 0 || j >= len || s[j] != ':') return FALSE;
    j++;
    gint mm;
    if (!read_digits(s, len, &j, 2, &mm)) return FALSE;
    if (v > 24 || mm > 59) return FALSE;
    *h = v;
    *m = mm;
    *i = j;
    return TRUE;
}

static gboolean is_unit(gchar c) {
    return c == 'h' || c == 'd' || c == 'w' || c == 'm' || c == 'y';
}

gboolean org_timestamp_parse(const gchar* s, gsize len, OrgTimestamp* ts) {
    memset(ts, 0, sizeof(*ts));
    ts->off_year = ts->off_month = ts->off_day = ts->off_dayname = -1;
    ts->off_hour = ts->off_minute = ts->off_end_hour = ts->off_end_minute = -1;
    ts->off_repeat = -1;
    if (s == NULL || len < 12) return FALSE;
    if (s[0] == '<') {
        if (s[len - 1] != '>') return FALSE;
        ts->active = TRUE;
    } else if (s[0] == '[') {
        if (s[len - 1] != ']') return FALSE;
        ts->active = FALSE;
    } else {
        return FALSE;
    }
    gsize end = len - 1;
    gsize i = 1;
    ts->off_year = (gint)i;
    if (!read_digits(s, end, &i, 4, &ts->year) || i >= end || s[i] != '-') return FALSE;
    i++;
    ts->off_month = (gint)i;
    if (!read_digits(s, end, &i, 2, &ts->month) || i >= end || s[i] != '-') return FALSE;
    i++;
    ts->off_day = (gint)i;
    if (!read_digits(s, end, &i, 2, &ts->day)) return FALSE;
    if (!g_date_valid_dmy((GDateDay)ts->day, (GDateMonth)ts->month, (GDateYear)ts->year)) {
        return FALSE;
    }

    while (i < end) {
        if (s[i] == ' ') {
            i++;
            continue;
        }
        gsize tok = i;
        gint h, m;
        if (g_ascii_isdigit(s[i]) && read_time(s, end, &i, &h, &m)) {
            if (ts->has_time) return FALSE;
            ts->has_time = TRUE;
            ts->hour = h;
            ts->minute = m;
            ts->off_hour = (gint)tok;
            ts->off_minute = (gint)i - 2;
            if (i < end && s[i] == '-') {
                gsize t2 = i + 1;
                if (read_time(s, end, &t2, &h, &m)) {
                    ts->has_end = TRUE;
                    ts->end_hour = h;
                    ts->end_minute = m;
                    ts->off_end_hour = (gint)i + 1;
                    ts->off_end_minute = (gint)t2 - 2;
                    i = t2;
                }
            }
        } else if (s[i] == '+' || (s[i] == '.' && i + 1 < end && s[i + 1] == '+')) {
            gsize k = i;
            gchar kind[3] = {0};
            if (s[k] == '.') {
                kind[0] = '.';
                kind[1] = '+';
                k += 2;
            } else if (k + 1 < end && s[k + 1] == '+') {
                kind[0] = kind[1] = '+';
                k += 2;
            } else {
                kind[0] = '+';
                k += 1;
            }
            gint n = 0, nd = 0;
            while (k < end && g_ascii_isdigit(s[k])) {
                n = n * 10 + (s[k] - '0');
                k++;
                nd++;
            }
            if (nd == 0 || k >= end || !is_unit(s[k])) return FALSE;
            memcpy(ts->repeat_kind, kind, sizeof(kind));
            ts->repeat_n = n;
            ts->repeat_unit = s[k];
            ts->off_repeat = (gint)tok;
            i = k + 1;
        } else if (s[i] == '-') {
            gsize k = i + 1;
            gchar kind[3] = {'-', 0, 0};
            if (k < end && s[k] == '-') {
                kind[1] = '-';
                k++;
            }
            gint n = 0, nd = 0;
            while (k < end && g_ascii_isdigit(s[k])) {
                n = n * 10 + (s[k] - '0');
                k++;
                nd++;
            }
            if (nd == 0 || k >= end || !is_unit(s[k])) return FALSE;
            memcpy(ts->warn_kind, kind, sizeof(kind));
            ts->warn_n = n;
            ts->warn_unit = s[k];
            i = k + 1;
        } else if (g_ascii_isalpha(s[i]) || ((guchar)s[i] >= 0x80)) {
            /* day name (any language): skipped, recomputed when formatting */
            if (ts->off_dayname < 0) ts->off_dayname = (gint)i;
            while (i < end && s[i] != ' ' && s[i] != '>' && s[i] != ']') i++;
        } else {
            return FALSE;
        }
    }
    return TRUE;
}

gchar* org_timestamp_format(const OrgTimestamp* ts) {
    GString* out = g_string_new(NULL);
    g_string_append_c(out, ts->active ? '<' : '[');
    g_string_append_printf(out, "%04d-%02d-%02d %s", ts->year, ts->month, ts->day,
                           org_day_name(ts->year, ts->month, ts->day));
    if (ts->has_time) {
        g_string_append_printf(out, " %02d:%02d", ts->hour, ts->minute);
        if (ts->has_end) {
            g_string_append_printf(out, "-%02d:%02d", ts->end_hour, ts->end_minute);
        }
    }
    if (ts->repeat_kind[0] != '\0') {
        g_string_append_printf(out, " %s%d%c", ts->repeat_kind, ts->repeat_n, ts->repeat_unit);
    }
    if (ts->warn_kind[0] != '\0') {
        g_string_append_printf(out, " %s%d%c", ts->warn_kind, ts->warn_n, ts->warn_unit);
    }
    g_string_append_c(out, ts->active ? '>' : ']');
    return g_string_free(out, FALSE);
}

gboolean org_timestamp_find(const gchar* line, gint col, gint* start, gint* end,
                            OrgTimestamp* ts) {
    if (line == NULL) return FALSE;
    gint len = (gint)strlen(line);
    for (gint i = 0; i < len; i++) {
        if (line[i] != '<' && line[i] != '[') continue;
        if (i > col) break;
        gchar close = line[i] == '<' ? '>' : ']';
        const gchar* c = strchr(line + i + 1, close);
        if (c == NULL) continue;
        gint e = (gint)(c - line) + 1;
        if (col > e) continue;
        OrgTimestamp tmp;
        if (org_timestamp_parse(line + i, (gsize)(e - i), &tmp)) {
            if (start) *start = i;
            if (end) *end = e;
            if (ts) *ts = tmp;
            return TRUE;
        }
    }
    return FALSE;
}

OrgTsField org_timestamp_field_at(const OrgTimestamp* ts, gint rel) {
    if (ts->off_repeat >= 0 && rel >= ts->off_repeat) return ORG_TS_FIELD_REPEAT;
    if (ts->off_end_minute >= 0 && rel >= ts->off_end_minute) return ORG_TS_FIELD_MINUTE;
    if (ts->off_end_hour >= 0 && rel >= ts->off_end_hour) return ORG_TS_FIELD_HOUR;
    if (ts->off_minute >= 0 && rel >= ts->off_minute) return ORG_TS_FIELD_MINUTE;
    if (ts->off_hour >= 0 && rel >= ts->off_hour) return ORG_TS_FIELD_HOUR;
    if (rel >= ts->off_day) return ORG_TS_FIELD_DAY;
    if (rel >= ts->off_month) return ORG_TS_FIELD_MONTH;
    if (rel >= ts->off_year && rel < ts->off_month) return ORG_TS_FIELD_YEAR;
    return ORG_TS_FIELD_DAY;
}

static void ts_get_date(const OrgTimestamp* ts, GDate* d) {
    g_date_clear(d, 1);
    g_date_set_dmy(d, (GDateDay)ts->day, (GDateMonth)ts->month, (GDateYear)ts->year);
}

static void ts_set_date(OrgTimestamp* ts, const GDate* d) {
    ts->year = g_date_get_year(d);
    ts->month = g_date_get_month(d);
    ts->day = g_date_get_day(d);
}

static void add_days(OrgTimestamp* ts, gint n) {
    GDate d;
    ts_get_date(ts, &d);
    if (n >= 0) g_date_add_days(&d, (guint)n);
    else g_date_subtract_days(&d, (guint)-n);
    ts_set_date(ts, &d);
}

static void add_months(OrgTimestamp* ts, gint n) {
    GDate d;
    ts_get_date(ts, &d);
    if (n >= 0) g_date_add_months(&d, (guint)n);
    else g_date_subtract_months(&d, (guint)-n);
    ts_set_date(ts, &d);
}

/* Add minutes to the start time (and the end time), carrying into days */
static void add_minutes(OrgTimestamp* ts, gint n) {
    gint total = ts->hour * 60 + ts->minute + n;
    gint days = 0;
    while (total < 0) {
        total += 24 * 60;
        days--;
    }
    while (total >= 24 * 60) {
        total -= 24 * 60;
        days++;
    }
    if (ts->has_end) {
        gint e = ts->end_hour * 60 + ts->end_minute + n;
        e = ((e % (24 * 60)) + 24 * 60) % (24 * 60);
        ts->end_hour = e / 60;
        ts->end_minute = e % 60;
    }
    ts->hour = total / 60;
    ts->minute = total % 60;
    if (days != 0) add_days(ts, days);
}

void org_timestamp_add(OrgTimestamp* ts, OrgTsField field, gint n) {
    switch (field) {
        case ORG_TS_FIELD_YEAR: add_months(ts, 12 * n); break;
        case ORG_TS_FIELD_MONTH: add_months(ts, n); break;
        case ORG_TS_FIELD_DAY: add_days(ts, n); break;
        case ORG_TS_FIELD_HOUR:
            if (ts->has_time) add_minutes(ts, 60 * n);
            else add_days(ts, n);
            break;
        case ORG_TS_FIELD_MINUTE:
            if (ts->has_time) add_minutes(ts, n);
            else add_days(ts, n);
            break;
        case ORG_TS_FIELD_REPEAT:
            if (ts->repeat_kind[0] != '\0') {
                ts->repeat_n = MAX(0, ts->repeat_n + n);
            }
            break;
    }
}

void org_timestamp_from_datetime(OrgTimestamp* ts, GDateTime* now,
                                 gboolean active, gboolean with_time) {
    memset(ts, 0, sizeof(*ts));
    ts->off_year = ts->off_month = ts->off_day = ts->off_dayname = -1;
    ts->off_hour = ts->off_minute = ts->off_end_hour = ts->off_end_minute = -1;
    ts->off_repeat = -1;
    ts->active = active;
    ts->year = g_date_time_get_year(now);
    ts->month = g_date_time_get_month(now);
    ts->day = g_date_time_get_day_of_month(now);
    if (with_time) {
        ts->has_time = TRUE;
        ts->hour = g_date_time_get_hour(now);
        ts->minute = g_date_time_get_minute(now);
    }
}

/* Add the repeater interval once */
static void add_interval(OrgTimestamp* ts, gint n, gchar unit) {
    switch (unit) {
        case 'h': if (ts->has_time) add_minutes(ts, 60 * n); else add_days(ts, (n + 23) / 24); break;
        case 'd': add_days(ts, n); break;
        case 'w': add_days(ts, 7 * n); break;
        case 'm': add_months(ts, n); break;
        case 'y': add_months(ts, 12 * n); break;
        default: break;
    }
}

/* Comparable key of a timestamp (minutes since year 0, roughly ordered) */
static gint64 ts_key(const OrgTimestamp* ts) {
    GDate d;
    ts_get_date(ts, &d);
    gint64 days = g_date_get_julian(&d);
    return days * 24 * 60 + (ts->has_time ? ts->hour * 60 + ts->minute : 0);
}

gboolean org_timestamp_apply_repeater(OrgTimestamp* ts, GDateTime* now) {
    if (ts->repeat_kind[0] == '\0' || ts->repeat_n <= 0) return FALSE;
    OrgTimestamp cur;
    org_timestamp_from_datetime(&cur, now, ts->active, TRUE);
    if (strcmp(ts->repeat_kind, ".+") == 0) {
        /* From today, keeping the time of day */
        ts->year = cur.year;
        ts->month = cur.month;
        ts->day = cur.day;
        add_interval(ts, ts->repeat_n, ts->repeat_unit);
    } else if (strcmp(ts->repeat_kind, "++") == 0) {
        gint guard = 0;
        do {
            add_interval(ts, ts->repeat_n, ts->repeat_unit);
        } while (ts_key(ts) <= ts_key(&cur) && ++guard < 100000);
    } else {
        add_interval(ts, ts->repeat_n, ts->repeat_unit);
    }
    return TRUE;
}

gint64 org_timestamp_minutes_between(const OrgTimestamp* a, const OrgTimestamp* b) {
    return ts_key(b) - ts_key(a);
}

gboolean org_timestamp_read(const gchar* input, GDateTime* now, OrgTimestamp* ts) {
    gboolean active = ts->active;
    org_timestamp_from_datetime(ts, now, active, FALSE);
    gchar* s = g_strstrip(g_ascii_strdown(input != NULL ? input : "", -1));
    gboolean ok = TRUE;
    gsize len = strlen(s);
    gsize i = 0;

    if (len == 0 || g_strcmp0(s, "today") == 0 || g_strcmp0(s, ".") == 0) {
        goto done;
    }
    if (g_strcmp0(s, "now") == 0) {
        org_timestamp_from_datetime(ts, now, active, TRUE);
        goto done;
    }
    if (g_strcmp0(s, "tomorrow") == 0) {
        add_days(ts, 1);
        goto done;
    }
    if (g_strcmp0(s, "yesterday") == 0) {
        add_days(ts, -1);
        goto done;
    }
    /* Relative: +3, -2, +2w, ++1m */
    if (s[0] == '+' || s[0] == '-') {
        gint sign = s[0] == '-' ? -1 : 1;
        i = 1;
        if (i < len && (s[i] == '+' || s[i] == '-')) i++;
        gint n = 0, nd = 0;
        while (i < len && g_ascii_isdigit(s[i])) {
            n = n * 10 + (s[i] - '0');
            i++;
            nd++;
        }
        if (nd == 0) n = 1;
        gchar unit = i < len ? s[i] : 'd';
        if (!is_unit(unit) || (i < len && i + 1 != len)) {
            ok = FALSE;
            goto done;
        }
        add_interval(ts, sign * n, unit);
        goto done;
    }
    /* Day name: next such day (today excluded) */
    for (gint d = 0; d < 7; d++) {
        gchar* low = g_ascii_strdown(DAY_NAMES[d], -1);
        gboolean match = g_str_has_prefix(s, low) && len <= 9 && g_ascii_isalpha(s[len - 1]);
        g_free(low);
        if (match) {
            GDate gd;
            ts_get_date(ts, &gd);
            gint today = g_date_get_weekday(&gd) - 1;
            gint delta = (d - today + 7) % 7;
            add_days(ts, delta == 0 ? 7 : delta);
            goto done;
        }
    }
    /* Date: YYYY-MM-DD or MM-DD, then optional HH:MM */
    if (len >= 4 && g_ascii_isdigit(s[0])) {
        gint y = ts->year, m, d;
        gsize j = 0;
        if (len >= 10 && s[4] == '-' && read_digits(s, len, &j, 4, &y)) {
            j++;
        } else {
            j = 0;
        }
        gsize mark = j;
        if (read_digits(s, len, &j, 2, &m) && j < len && s[j] == '-') {
            j++;
            if (!read_digits(s, len, &j, 2, &d) ||
                !g_date_valid_dmy((GDateDay)d, (GDateMonth)m, (GDateYear)y)) {
                ok = FALSE;
                goto done;
            }
            ts->year = y;
            ts->month = m;
            ts->day = d;
            i = j;
        } else {
            i = mark;
        }
    }
    while (i < len && s[i] == ' ') i++;
    if (i < len) {
        gint h, mi;
        gsize j = i;
        if (read_time(s, len, &j, &h, &mi)) {
            ts->has_time = TRUE;
            ts->hour = h;
            ts->minute = mi;
            if (j < len && s[j] == '-') {
                gsize k = j + 1;
                if (read_time(s, len, &k, &h, &mi)) {
                    ts->has_end = TRUE;
                    ts->end_hour = h;
                    ts->end_minute = mi;
                    j = k;
                }
            }
            i = j;
        }
        while (i < len && s[i] == ' ') i++;
        if (i < len) ok = FALSE;
    }

done:
    g_free(s);
    return ok;
}

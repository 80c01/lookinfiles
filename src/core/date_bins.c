/* Calendar groups of dates for a histogram (see date_bins.h) */
#include "core/date_bins.h"

gint date_bins_day_of(gint64 t) {
    GDateTime* dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL) return 19700101;
    gint y, m, d;
    g_date_time_get_ymd(dt, &y, &m, &d);
    g_date_time_unref(dt);
    return y * 10000 + m * 100 + d;
}

gint date_bins_day_span(gint64 t, gint64* start, gint64* end) {
    GDateTime* dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL) {
        *start = t;
        *end = t + 1;
        return 19700101;
    }
    gint y, m, d;
    g_date_time_get_ymd(dt, &y, &m, &d);
    g_date_time_unref(dt);
    GDateTime* midnight = g_date_time_new_local(y, m, d, 0, 0, 0);
    GDateTime* next = midnight != NULL ? g_date_time_add_days(midnight, 1) : NULL;
    *start = midnight != NULL ? g_date_time_to_unix(midnight) : t;
    *end = next != NULL ? g_date_time_to_unix(next) : t + 1;
    if (*start > t || *end <= t) { /* a day without its midnight (a time change) */
        *start = t;
        *end = t + 1;
    }
    if (next != NULL) g_date_time_unref(next);
    if (midnight != NULL) g_date_time_unref(midnight);
    return y * 10000 + m * 100 + d;
}

/* Days from 0001-01-01 of day */
static guint32 julian(gint day) {
    GDate* d = g_date_new_dmy((GDateDay)(day % 100), (GDateMonth)(day / 100 % 100), (GDateYear)(day / 10000));
    guint32 j = g_date_valid(d) ? g_date_get_julian(d) : 1;
    g_date_free(d);
    return j;
}

guint date_bins_span(gint a, gint b, DateBinUnit unit) {
    if (b < a) return 0;
    switch (unit) {
    case DATE_BIN_DAY: return julian(b) - julian(a) + 1;
    case DATE_BIN_MONTH: return (guint)((b / 100 - a / 100) * 12 + (b % 100 - a % 100) + 1);
    case DATE_BIN_YEAR: break;
    }
    return (guint)(b - a + 1);
}

DateBinUnit date_bins_unit(gint first, gint last, guint max) {
    if (date_bins_span(first, last, DATE_BIN_DAY) <= max) return DATE_BIN_DAY;
    if (date_bins_span(first / 100, last / 100, DATE_BIN_MONTH) <= max) return DATE_BIN_MONTH;
    return DATE_BIN_YEAR;
}

gint date_bins_key(gint day, DateBinUnit unit) {
    switch (unit) {
    case DATE_BIN_DAY: return day;
    case DATE_BIN_MONTH: return day / 100;
    case DATE_BIN_YEAR: break;
    }
    return day / 10000;
}

gint date_bins_next(gint key, DateBinUnit unit) {
    switch (unit) {
    case DATE_BIN_DAY: {
        GDate* d = g_date_new_dmy((GDateDay)(key % 100), (GDateMonth)(key / 100 % 100), (GDateYear)(key / 10000));
        if (!g_date_valid(d)) {
            g_date_free(d);
            return key + 1;
        }
        g_date_add_days(d, 1);
        gint next = g_date_get_year(d) * 10000 + g_date_get_month(d) * 100 + g_date_get_day(d);
        g_date_free(d);
        return next;
    }
    case DATE_BIN_MONTH: return key % 100 >= 12 ? (key / 100 + 1) * 100 + 1 : key + 1;
    case DATE_BIN_YEAR: break;
    }
    return key + 1;
}

void date_bins_days(gint key, DateBinUnit unit, gint* first, gint* last) {
    gint f = key, l = key;
    switch (unit) {
    case DATE_BIN_DAY: break;
    case DATE_BIN_MONTH: {
        gint y = key / 100, m = key % 100;
        f = key * 100 + 1;
        l = key * 100 + (g_date_valid_dmy(1, (GDateMonth)m, (GDateYear)y)
                             ? g_date_get_days_in_month((GDateMonth)m, (GDateYear)y)
                             : 31);
        break;
    }
    case DATE_BIN_YEAR:
        f = key * 10000 + 101;
        l = key * 10000 + 1231;
        break;
    }
    if (first != NULL) *first = f;
    if (last != NULL) *last = l;
}

gchar* date_bins_label(gint key, DateBinUnit unit) {
    switch (unit) {
    case DATE_BIN_DAY: return g_strdup_printf("%02d-%02d", key / 100 % 100, key % 100);
    case DATE_BIN_MONTH: return g_strdup_printf("%04d-%02d", key / 100, key % 100);
    case DATE_BIN_YEAR: break;
    }
    return g_strdup_printf("%d", key);
}

gchar* date_bins_name(gint key, DateBinUnit unit) {
    if (unit == DATE_BIN_DAY) return g_strdup_printf("%04d-%02d-%02d", key / 10000, key / 100 % 100, key % 100);
    return date_bins_label(key, unit);
}

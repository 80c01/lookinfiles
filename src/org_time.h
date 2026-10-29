#ifndef ORG_TIME_H
#define ORG_TIME_H

/* Org-mode timestamps: <2026-10-29 Thu 10:00-11:30 +1w -2d> (active) and
 * [2026-10-29 Thu] (inactive). Parsing, formatting, field arithmetic,
 * repeaters and the date prompt. Pure GLib, no widgets. */
#include <glib.h>

typedef enum {
    ORG_TS_FIELD_YEAR = 0,
    ORG_TS_FIELD_MONTH,
    ORG_TS_FIELD_DAY,
    ORG_TS_FIELD_HOUR,
    ORG_TS_FIELD_MINUTE,
    ORG_TS_FIELD_REPEAT
} OrgTsField;

typedef struct {
    gboolean active;          /* <...> rather than [...] */
    gint year, month, day;
    gboolean has_time;
    gint hour, minute;
    gboolean has_end;         /* 10:00-11:30 */
    gint end_hour, end_minute;
    gchar repeat_kind[3];     /* "+", "++", ".+" or "" (no repeater) */
    gint repeat_n;
    gchar repeat_unit;        /* h d w m y */
    gchar warn_kind[3];       /* "-", "--" or "" */
    gint warn_n;
    gchar warn_unit;
    /* Byte offsets of the fields in the parsed text (from the opening
     * bracket), -1 when absent: used to pick the field under the cursor */
    gint off_year, off_month, off_day, off_dayname, off_hour, off_minute,
         off_end_hour, off_end_minute, off_repeat;
} OrgTimestamp;

/* Parse the timestamp text s (len bytes, from '<' or '[' to the closing
 * bracket included). */
gboolean org_timestamp_parse(const gchar* s, gsize len, OrgTimestamp* ts);

/* Canonical text: "<2026-10-29 Thu 10:00 +1w>" (newly allocated) */
gchar* org_timestamp_format(const OrgTimestamp* ts);

/* Find a timestamp on line that contains byte offset col (or ends right
 * before it). Sets its byte range [*start, *end). */
gboolean org_timestamp_find(const gchar* line, gint col, gint* start, gint* end,
                            OrgTimestamp* ts);

/* Field of ts at byte offset rel (relative to the opening bracket) */
OrgTsField org_timestamp_field_at(const OrgTimestamp* ts, gint rel);

/* Add n units of field (minutes in 5-minute steps are the caller's choice).
 * Days, months and years keep a valid date; times wrap into the next day. */
void org_timestamp_add(OrgTimestamp* ts, OrgTsField field, gint n);

/* Timestamp for now (date only unless with_time) */
void org_timestamp_from_datetime(OrgTimestamp* ts, GDateTime* now,
                                 gboolean active, gboolean with_time);

/* Apply the repeater after the entry is marked done: "+1w" adds one week,
 * "++1w" adds weeks until the date is after now, ".+1w" counts from now.
 * FALSE when ts has no repeater. */
gboolean org_timestamp_apply_repeater(OrgTimestamp* ts, GDateTime* now);

/* Read a date typed at a prompt into ts (keeps ts->active):
 * "" today, "+3" / "+2w" / "-1m" relative to today, "2026-11-02",
 * "11-02", "2026-11-02 14:30", "14:30" (today), "tomorrow", "mon".."sun"
 * (next such day). */
gboolean org_timestamp_read(const gchar* input, GDateTime* now, OrgTimestamp* ts);

/* Minutes between two timestamps that have a time (b - a) */
gint64 org_timestamp_minutes_between(const OrgTimestamp* a, const OrgTimestamp* b);

/* English day abbreviation of a date ("Mon".."Sun") */
const gchar* org_day_name(gint year, gint month, gint day);

#endif /* ORG_TIME_H */

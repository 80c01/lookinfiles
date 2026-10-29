#ifndef CARD_VIEW_H
#define CARD_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* Readable cards for small structured text formats:
 *   - iCalendar (.ics): events sorted by start (when, where, repeat rule,
 *     organizer, attendees, description) and to-dos;
 *   - vCard (.vcf): one card per contact (name, organization, e-mails,
 *     phones, addresses, birthday...) with its embedded photo;
 *   - freedesktop .desktop entries: name, command, icon, categories, actions.
 * Content lines are unfolded and unescaped (RFC 5545 / 6350). Pure GLib,
 * unit-tested. */

/* A content line of iCalendar / vCard: NAME;PARAM=x:VALUE */
typedef struct {
    gchar* name;          /* upper case, group prefix ("item1.") removed */
    gchar* params;        /* raw parameters ("TYPE=CELL;VALUE=DATE"), or "" */
    gchar* value;         /* raw value (escapes kept) */
} VLine;

/* Unfolded content lines of a vCard / iCalendar text */
GArray* vformat_parse(const gchar* text, gsize len);  /* VLine */

/* Value with \n, \, \; and \\ unescaped. Caller frees. */
gchar* vformat_unescape(const gchar* value);

/* Parameter value ("TYPE" of "TYPE=work,voice;PREF=1" -> "work,voice"), or NULL */
gchar* vformat_param(const gchar* params, const gchar* key);

/* iCalendar date or date-time ("20261029", "20261029T100000Z") to Unix
 * time; UTC times are converted, floating and TZID times are taken as
 * local. *all_day is set for dates. -1 if invalid. */
gint64 vformat_parse_time(const gchar* value, gboolean* all_day);

/* Append the cards; FALSE (doc unchanged) if there is nothing to show */
gboolean card_view_append_calendar(PreviewDoc* doc, const gchar* text, gsize len);
gboolean card_view_append_contacts(PreviewDoc* doc, const gchar* text, gsize len);
gboolean card_view_append_desktop(PreviewDoc* doc, const gchar* text, gsize len);

/* Most events, to-dos or contacts shown */
#define CARD_VIEW_MAX_ITEMS 300

#endif /* CARD_VIEW_H */

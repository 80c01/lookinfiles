/* Calendars, contacts and desktop entries for the preview (see card_view.h). */
#include "card_view.h"

#include <stdio.h>
#include <string.h>

/* --- Content lines --- */

static void vline_clear(gpointer data) {
    VLine* l = data;
    g_free(l->name);
    g_free(l->params);
    g_free(l->value);
}

GArray* vformat_parse(const gchar* text, gsize len) {
    GArray* lines = g_array_new(FALSE, TRUE, sizeof(VLine));
    g_array_set_clear_func(lines, vline_clear);
    /* Unfold: a line break followed by a space or tab continues the line */
    GString* unfolded = g_string_sized_new(len);
    for (gsize i = 0; i < len; i++) {
        gchar c = text[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n' && i + 1 < len && (text[i + 1] == ' ' || text[i + 1] == '\t')) {
            i++;
            continue;
        }
        g_string_append_c(unfolded, c);
    }
    gchar** raw = g_strsplit(unfolded->str, "\n", -1);
    g_string_free(unfolded, TRUE);
    for (gint i = 0; raw[i] != NULL; i++) {
        const gchar* line = raw[i];
        /* The value starts after the first ':' outside a quoted parameter */
        gboolean quoted = FALSE;
        const gchar* colon = NULL;
        for (const gchar* p = line; *p != '\0'; p++) {
            if (*p == '"') {
                quoted = !quoted;
            } else if (*p == ':' && !quoted) {
                colon = p;
                break;
            }
        }
        if (colon == NULL || colon == line) {
            continue;
        }
        gchar* head = g_strndup(line, (gsize)(colon - line));
        gchar* semi = strchr(head, ';');
        gchar* params = g_strdup(semi != NULL ? semi + 1 : "");
        if (semi != NULL) {
            *semi = '\0';
        }
        const gchar* dot = strrchr(head, '.');
        VLine l = { g_ascii_strup(dot != NULL ? dot + 1 : head, -1), params, g_strdup(colon + 1) };
        g_array_append_val(lines, l);
        g_free(head);
    }
    g_strfreev(raw);
    return lines;
}

gchar* vformat_unescape(const gchar* value) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = value; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
            g_string_append_c(out, (*p == 'n' || *p == 'N') ? '\n' : *p);
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

gchar* vformat_param(const gchar* params, const gchar* key) {
    gchar** parts = g_strsplit(params, ";", -1);
    GString* out = NULL;
    gsize klen = strlen(key);
    for (gint i = 0; parts[i] != NULL; i++) {
        const gchar* v = NULL;
        if (g_ascii_strncasecmp(parts[i], key, klen) == 0 && parts[i][klen] == '=') {
            v = parts[i] + klen + 1;
        } else if (strcmp(key, "TYPE") == 0 && strchr(parts[i], '=') == NULL && parts[i][0] != '\0') {
            v = parts[i]; /* vCard 2.1: bare types ("TEL;CELL;WORK:") */
        }
        if (v != NULL) {
            if (out == NULL) {
                out = g_string_new(NULL);
            } else {
                g_string_append_c(out, ',');
            }
            gchar* unquoted = g_strdup(v);
            g_strdelimit(unquoted, "\"", ' ');
            g_string_append(out, g_strstrip(unquoted));
            g_free(unquoted);
        }
    }
    g_strfreev(parts);
    return out != NULL ? g_string_free(out, FALSE) : NULL;
}

gint64 vformat_parse_time(const gchar* value, gboolean* all_day) {
    gint y, mo, d, h = 0, mi = 0, s = 0;
    gsize n = strlen(value);
    if (n < 8 || sscanf(value, "%4d%2d%2d", &y, &mo, &d) != 3) {
        return -1;
    }
    gboolean date_only = (n == 8);
    if (!date_only && (value[8] != 'T' || sscanf(value + 9, "%2d%2d%2d", &h, &mi, &s) < 2)) {
        return -1;
    }
    if (all_day != NULL) {
        *all_day = date_only;
    }
    gboolean utc = !date_only && value[n - 1] == 'Z';
    GDateTime* dt = utc ? g_date_time_new_utc(y, mo, d, h, mi, s) : g_date_time_new_local(y, mo, d, h, mi, s);
    if (dt == NULL) {
        return -1;
    }
    gint64 t = g_date_time_to_unix(dt);
    g_date_time_unref(dt);
    return t;
}

/* Value of the first line with this name, unescaped, or NULL */
static gchar* first_value(GArray* lines, guint from, guint to, const gchar* name) {
    for (guint i = from; i < to; i++) {
        const VLine* l = &g_array_index(lines, VLine, i);
        if (strcmp(l->name, name) == 0) {
            return vformat_unescape(l->value);
        }
    }
    return NULL;
}

static const VLine* first_line(GArray* lines, guint from, guint to, const gchar* name) {
    for (guint i = from; i < to; i++) {
        const VLine* l = &g_array_index(lines, VLine, i);
        if (strcmp(l->name, name) == 0) {
            return l;
        }
    }
    return NULL;
}

/* Text cut to max characters on its first lines */
static gchar* shorten(const gchar* text, guint max) {
    if (text == NULL) {
        return NULL;
    }
    if ((guint)g_utf8_strlen(text, -1) <= max) {
        return g_strstrip(g_strdup(text));
    }
    gchar* cut = g_utf8_substring(text, 0, max);
    gchar* out = g_strconcat(g_strstrip(cut), "…", NULL);
    g_free(cut);
    return out;
}

/* --- iCalendar --- */

typedef struct {
    guint from;            /* lines of the component, after BEGIN */
    guint to;
    gboolean todo;
    gint64 start;
} CalItem;

static gint compare_items(gconstpointer a, gconstpointer b) {
    const CalItem* x = a;
    const CalItem* y = b;
    if (x->todo != y->todo) {
        return x->todo ? 1 : -1;
    }
    return (x->start > y->start) - (x->start < y->start);
}

static gchar* format_when(const VLine* start_line, const VLine* end_line) {
    gboolean all_day = FALSE;
    gint64 start = vformat_parse_time(start_line->value, &all_day);
    if (start < 0) {
        return g_strdup(start_line->value);
    }
    GDateTime* s = g_date_time_new_from_unix_local(start);
    gchar* out;
    if (all_day) {
        out = g_date_time_format(s, "%Y-%m-%d %a (all day)");
    } else {
        out = g_date_time_format(s, "%Y-%m-%d %a %H:%M");
    }
    if (end_line != NULL && !all_day) {
        gint64 end = vformat_parse_time(end_line->value, NULL);
        if (end > start) {
            GDateTime* e = g_date_time_new_from_unix_local(end);
            gboolean same_day = g_date_time_get_day_of_year(e) == g_date_time_get_day_of_year(s) &&
                                g_date_time_get_year(e) == g_date_time_get_year(s);
            gchar* end_text = g_date_time_format(e, same_day ? "%H:%M" : "%Y-%m-%d %a %H:%M");
            gchar* joined = g_strdup_printf("%s – %s", out, end_text);
            g_free(end_text);
            g_free(out);
            out = joined;
            g_date_time_unref(e);
        }
    }
    gchar* tz = vformat_param(start_line->params, "TZID");
    if (tz != NULL && !all_day) {
        gchar* with_tz = g_strdup_printf("%s (%s)", out, tz);
        g_free(out);
        out = with_tz;
    }
    g_free(tz);
    g_date_time_unref(s);
    return out;
}

/* "FREQ=WEEKLY;BYDAY=MO,WE;COUNT=10" -> "weekly on MO, WE, 10 times" */
static gchar* format_rrule(const gchar* rule) {
    GString* out = g_string_new(NULL);
    gchar** parts = g_strsplit(rule, ";", -1);
    const gchar* freq = NULL;
    const gchar* interval = NULL;
    for (gint i = 0; parts[i] != NULL; i++) {
        if (g_str_has_prefix(parts[i], "FREQ=")) {
            freq = parts[i] + 5;
        } else if (g_str_has_prefix(parts[i], "INTERVAL=")) {
            interval = parts[i] + 9;
        }
    }
    if (freq != NULL) {
        const gchar* unit = g_ascii_strcasecmp(freq, "DAILY") == 0 ? "day"
                            : g_ascii_strcasecmp(freq, "WEEKLY") == 0 ? "week"
                            : g_ascii_strcasecmp(freq, "MONTHLY") == 0 ? "month"
                            : g_ascii_strcasecmp(freq, "YEARLY") == 0 ? "year" : NULL;
        if (unit != NULL && interval != NULL && strcmp(interval, "1") != 0) {
            g_string_append_printf(out, "every %s %ss", interval, unit);
        } else if (unit != NULL) {
            g_string_append_printf(out, "every %s", unit);
        } else {
            g_string_append(out, freq);
        }
    }
    for (gint i = 0; parts[i] != NULL; i++) {
        if (g_str_has_prefix(parts[i], "BYDAY=")) {
            gchar* days = g_strdup(parts[i] + 6);
            g_strdelimit(days, ",", '/');
            g_string_append_printf(out, " on %s", days);
            g_free(days);
        } else if (g_str_has_prefix(parts[i], "COUNT=")) {
            g_string_append_printf(out, ", %s times", parts[i] + 6);
        } else if (g_str_has_prefix(parts[i], "UNTIL=")) {
            gint64 until = vformat_parse_time(parts[i] + 6, NULL);
            if (until >= 0) {
                GDateTime* u = g_date_time_new_from_unix_local(until);
                gchar* text = g_date_time_format(u, "%Y-%m-%d");
                g_string_append_printf(out, ", until %s", text);
                g_free(text);
                g_date_time_unref(u);
            }
        }
    }
    g_strfreev(parts);
    return g_string_free(out, FALSE);
}

/* "mailto:a@b" -> "a@b", with the CN parameter when there is one */
static gchar* format_person(const VLine* line) {
    const gchar* v = line->value;
    if (g_ascii_strncasecmp(v, "mailto:", 7) == 0) {
        v += 7;
    }
    gchar* cn = vformat_param(line->params, "CN");
    gchar* out = cn != NULL ? g_strdup_printf("%s <%s>", cn, v) : g_strdup(v);
    g_free(cn);
    return out;
}

gboolean card_view_append_calendar(PreviewDoc* doc, const gchar* text, gsize len) {
    GArray* lines = vformat_parse(text, len);
    GArray* items = g_array_new(FALSE, TRUE, sizeof(CalItem));
    gint depth_start = -1;
    gboolean todo = FALSE;
    for (guint i = 0; i < lines->len; i++) {
        const VLine* l = &g_array_index(lines, VLine, i);
        if (strcmp(l->name, "BEGIN") == 0 && (g_ascii_strcasecmp(l->value, "VEVENT") == 0 ||
                                              g_ascii_strcasecmp(l->value, "VTODO") == 0)) {
            depth_start = (gint)i + 1;
            todo = g_ascii_strcasecmp(l->value, "VTODO") == 0;
        } else if (strcmp(l->name, "END") == 0 && depth_start >= 0 &&
                   (g_ascii_strcasecmp(l->value, "VEVENT") == 0 || g_ascii_strcasecmp(l->value, "VTODO") == 0)) {
            CalItem item = { (guint)depth_start, i, todo, G_MAXINT64 };
            const VLine* start = first_line(lines, item.from, item.to, todo ? "DUE" : "DTSTART");
            if (start != NULL) {
                item.start = vformat_parse_time(start->value, NULL);
            }
            g_array_append_val(items, item);
            depth_start = -1;
        }
    }
    if (items->len == 0) {
        g_array_free(items, TRUE);
        g_array_free(lines, TRUE);
        return FALSE;
    }
    g_array_sort(items, compare_items);

    gchar* cal_name = first_value(lines, 0, lines->len, "X-WR-CALNAME");
    guint events = 0;
    for (guint i = 0; i < items->len; i++) {
        events += !g_array_index(items, CalItem, i).todo;
    }
    guint todos = items->len - events;
    if (cal_name != NULL) {
        preview_doc_append(doc, PREVIEW_STYLE_HEADING2, cal_name);
        preview_doc_append_len(doc, "\n", 1);
    }
    GString* counts = g_string_new(NULL);
    if (events > 0) {
        g_string_append_printf(counts, "%u %s", events, events == 1 ? "event" : "events");
    }
    if (todos > 0) {
        g_string_append_printf(counts, "%s%u %s", counts->len > 0 ? " · " : "", todos,
                               todos == 1 ? "to-do" : "to-dos");
    }
    preview_doc_note(doc, counts->str);
    g_string_free(counts, TRUE);
    g_free(cal_name);

    for (guint i = 0; i < items->len && i < CARD_VIEW_MAX_ITEMS; i++) {
        const CalItem* it = &g_array_index(items, CalItem, i);
        gchar* summary = first_value(lines, it->from, it->to, "SUMMARY");
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_append(doc, PREVIEW_STYLE_SECTION, summary != NULL ? summary : "(no title)");
        if (it->todo) {
            preview_doc_append(doc, PREVIEW_STYLE_DIM, "  to-do");
        }
        preview_doc_append_len(doc, "\n", 1);
        g_free(summary);
        const VLine* start = first_line(lines, it->from, it->to, it->todo ? "DUE" : "DTSTART");
        if (start != NULL) {
            gchar* when = format_when(start, first_line(lines, it->from, it->to, "DTEND"));
            preview_doc_field(doc, it->todo ? "Due" : "When", when);
            g_free(when);
        }
        gchar* location = first_value(lines, it->from, it->to, "LOCATION");
        preview_doc_field(doc, "Where", location);
        g_free(location);
        const VLine* rrule = first_line(lines, it->from, it->to, "RRULE");
        if (rrule != NULL) {
            gchar* repeat = format_rrule(rrule->value);
            preview_doc_field(doc, "Repeats", repeat);
            g_free(repeat);
        }
        const VLine* organizer = first_line(lines, it->from, it->to, "ORGANIZER");
        if (organizer != NULL) {
            gchar* who = format_person(organizer);
            preview_doc_field(doc, "Organizer", who);
            g_free(who);
        }
        GString* attendees = g_string_new(NULL);
        guint n_attendees = 0;
        for (guint k = it->from; k < it->to; k++) {
            const VLine* l = &g_array_index(lines, VLine, k);
            if (strcmp(l->name, "ATTENDEE") == 0 && n_attendees++ < 10) {
                gchar* who = format_person(l);
                g_string_append_printf(attendees, "%s%s", attendees->len > 0 ? "\n" : "", who);
                g_free(who);
            }
        }
        if (n_attendees > 10) {
            g_string_append_printf(attendees, "\n… %u more", n_attendees - 10);
        }
        preview_doc_field(doc, "Attendees", attendees->str);
        g_string_free(attendees, TRUE);
        gchar* status = first_value(lines, it->from, it->to, "STATUS");
        if (status != NULL) {
            gchar* lower = g_ascii_strdown(status, -1);
            preview_doc_field(doc, "Status", lower);
            g_free(lower);
        }
        g_free(status);
        gchar* description = first_value(lines, it->from, it->to, "DESCRIPTION");
        gchar* short_desc = shorten(description, 400);
        preview_doc_field(doc, "Notes", short_desc);
        g_free(short_desc);
        g_free(description);
    }
    if (items->len > CARD_VIEW_MAX_ITEMS) {
        preview_doc_note(doc, "\n(more items not shown)");
    }
    g_array_free(items, TRUE);
    g_array_free(lines, TRUE);
    return TRUE;
}

/* --- vCard --- */

/* Embedded photo of a vCard ("PHOTO;ENCODING=b:..." or a data: URI) */
static GBytes* card_photo(const VLine* l) {
    const gchar* v = l->value;
    gchar* encoding = vformat_param(l->params, "ENCODING");
    gboolean base64 = encoding != NULL && (g_ascii_strcasecmp(encoding, "b") == 0 ||
                                           g_ascii_strcasecmp(encoding, "BASE64") == 0);
    g_free(encoding);
    if (g_str_has_prefix(v, "data:")) {
        const gchar* comma = strchr(v, ',');
        base64 = comma != NULL && g_strstr_len(v, comma - v, ";base64") != NULL;
        v = comma != NULL ? comma + 1 : v;
    }
    if (!base64) {
        return NULL;
    }
    gsize n = 0;
    guchar* data = g_base64_decode(v, &n);
    if (n == 0) {
        g_free(data);
        return NULL;
    }
    return g_bytes_new_take(data, n);
}

/* Label of a TYPE parameter: "work, voice" -> "work" (first type, lower case) */
static gchar* type_label(const VLine* l) {
    gchar* type = vformat_param(l->params, "TYPE");
    if (type == NULL) {
        return NULL;
    }
    gchar** types = g_strsplit(type, ",", -1);
    gchar* out = NULL;
    for (gint i = 0; types[i] != NULL && out == NULL; i++) {
        g_strstrip(types[i]);
        if (g_ascii_strcasecmp(types[i], "pref") != 0 && g_ascii_strcasecmp(types[i], "internet") != 0 &&
            g_ascii_strcasecmp(types[i], "voice") != 0 && types[i][0] != '\0') {
            out = g_ascii_strdown(types[i], -1);
        }
    }
    g_strfreev(types);
    g_free(type);
    return out;
}

/* Structured value ("a;b;;c") joined with sep, empty parts skipped */
static gchar* join_parts(const gchar* value, const gchar* sep) {
    gchar** parts = g_strsplit(value, ";", -1);
    GString* out = g_string_new(NULL);
    for (gint i = 0; parts[i] != NULL; i++) {
        gchar* p = vformat_unescape(parts[i]);
        g_strstrip(p);
        if (p[0] != '\0') {
            g_string_append_printf(out, "%s%s", out->len > 0 ? sep : "", p);
        }
        g_free(p);
    }
    g_strfreev(parts);
    return g_string_free(out, FALSE);
}

static void append_multi(PreviewDoc* doc, GArray* lines, guint from, guint to, const gchar* name,
                         const gchar* label, gboolean address) {
    GString* values = g_string_new(NULL);
    for (guint i = from; i < to; i++) {
        const VLine* l = &g_array_index(lines, VLine, i);
        if (strcmp(l->name, name) != 0) {
            continue;
        }
        gchar* value = address ? join_parts(l->value, ", ") : vformat_unescape(l->value);
        gchar* type = type_label(l);
        g_string_append_printf(values, "%s%s%s%s%s", values->len > 0 ? "\n" : "", value,
                               type != NULL ? " (" : "", type != NULL ? type : "", type != NULL ? ")" : "");
        g_free(type);
        g_free(value);
    }
    preview_doc_field(doc, label, values->str);
    g_string_free(values, TRUE);
}

gboolean card_view_append_contacts(PreviewDoc* doc, const gchar* text, gsize len) {
    GArray* lines = vformat_parse(text, len);
    guint shown = 0;
    guint total = 0;
    gint begin = -1;
    for (guint i = 0; i < lines->len; i++) {
        const VLine* l = &g_array_index(lines, VLine, i);
        if (strcmp(l->name, "BEGIN") == 0 && g_ascii_strcasecmp(l->value, "VCARD") == 0) {
            begin = (gint)i + 1;
            continue;
        }
        if (strcmp(l->name, "END") != 0 || g_ascii_strcasecmp(l->value, "VCARD") != 0 || begin < 0) {
            continue;
        }
        guint from = (guint)begin;
        guint to = i;
        begin = -1;
        total++;
        if (shown >= CARD_VIEW_MAX_ITEMS) {
            continue;
        }
        shown++;
        gchar* fn = first_value(lines, from, to, "FN");
        if (fn == NULL) {
            const VLine* n = first_line(lines, from, to, "N");
            fn = n != NULL ? join_parts(n->value, " ") : g_strdup("(no name)");
        }
        if (doc->text->len > 0) {
            preview_doc_append_len(doc, "\n", 1);
        }
        const VLine* photo = first_line(lines, from, to, "PHOTO");
        GBytes* image = photo != NULL ? card_photo(photo) : NULL;
        if (image != NULL) {
            preview_doc_add_image(doc, image, 96);
            preview_doc_append_len(doc, "\n", 1);
            g_bytes_unref(image);
        }
        preview_doc_append(doc, PREVIEW_STYLE_HEADING3, fn);
        preview_doc_append_len(doc, "\n", 1);
        g_free(fn);
        const VLine* org = first_line(lines, from, to, "ORG");
        if (org != NULL) {
            gchar* text_org = join_parts(org->value, ", ");
            preview_doc_field(doc, "Organization", text_org);
            g_free(text_org);
        }
        gchar* title = first_value(lines, from, to, "TITLE");
        preview_doc_field(doc, "Title", title);
        g_free(title);
        append_multi(doc, lines, from, to, "EMAIL", "E-mail", FALSE);
        append_multi(doc, lines, from, to, "TEL", "Phone", FALSE);
        append_multi(doc, lines, from, to, "ADR", "Address", TRUE);
        append_multi(doc, lines, from, to, "URL", "Web", FALSE);
        gchar* bday = first_value(lines, from, to, "BDAY");
        preview_doc_field(doc, "Birthday", bday);
        g_free(bday);
        gchar* note = first_value(lines, from, to, "NOTE");
        gchar* short_note = shorten(note, 300);
        preview_doc_field(doc, "Note", short_note);
        g_free(short_note);
        g_free(note);
    }
    g_array_free(lines, TRUE);
    if (total == 0) {
        return FALSE;
    }
    if (total > shown) {
        gchar* more = g_strdup_printf("\n%u more contacts not shown.", total - shown);
        preview_doc_note(doc, more);
        g_free(more);
    }
    return TRUE;
}

/* --- Desktop entries --- */

gboolean card_view_append_desktop(PreviewDoc* doc, const gchar* text, gsize len) {
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_data(kf, text, len, G_KEY_FILE_NONE, NULL) ||
        !g_key_file_has_group(kf, "Desktop Entry")) {
        g_key_file_free(kf);
        return FALSE;
    }
    const gchar* g = "Desktop Entry";
    gchar* name = g_key_file_get_locale_string(kf, g, "Name", NULL, NULL);
    preview_doc_append(doc, PREVIEW_STYLE_HEADING2, name != NULL ? name : "(no name)");
    preview_doc_append_len(doc, "\n", 1);
    g_free(name);
    gchar* generic = g_key_file_get_locale_string(kf, g, "GenericName", NULL, NULL);
    gchar* comment = g_key_file_get_locale_string(kf, g, "Comment", NULL, NULL);
    if (generic != NULL || comment != NULL) {
        gchar* line = g_strdup_printf("%s%s%s", generic != NULL ? generic : "",
                                      generic != NULL && comment != NULL ? " — " : "",
                                      comment != NULL ? comment : "");
        preview_doc_note(doc, line);
        g_free(line);
    }
    g_free(generic);
    g_free(comment);
    preview_doc_append_len(doc, "\n", 1);

    static const struct {
        const gchar* key;
        const gchar* label;
    } FIELDS[] = {
        { "Type", "Type" }, { "Exec", "Command" }, { "TryExec", "Requires" }, { "Path", "Directory" },
        { "Icon", "Icon" }, { "Terminal", "Terminal" }, { "StartupWMClass", "Window class" },
        { "NoDisplay", "Hidden" }, { "OnlyShowIn", "Only in" }, { "NotShowIn", "Not in" },
        { "URL", "URL" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(FIELDS); i++) {
        gchar* v = g_key_file_get_string(kf, g, FIELDS[i].key, NULL);
        if (v != NULL && g_str_has_suffix(v, ";")) {
            v[strlen(v) - 1] = '\0';
        }
        preview_doc_field_styled(doc, FIELDS[i].label, v,
                                 strcmp(FIELDS[i].key, "Exec") == 0 ? PREVIEW_STYLE_MONO : PREVIEW_STYLE_PLAIN);
        g_free(v);
    }
    gsize n = 0;
    gchar** cats = g_key_file_get_string_list(kf, g, "Categories", &n, NULL);
    if (cats != NULL && n > 0) {
        gchar* joined = g_strjoinv(", ", cats);
        preview_doc_field(doc, "Categories", joined);
        g_free(joined);
    }
    g_strfreev(cats);
    gchar** keywords = g_key_file_get_locale_string_list(kf, g, "Keywords", NULL, &n, NULL);
    if (keywords != NULL && n > 0) {
        gchar* joined = g_strjoinv(", ", keywords);
        preview_doc_field(doc, "Keywords", joined);
        g_free(joined);
    }
    g_strfreev(keywords);
    gchar** mimes = g_key_file_get_string_list(kf, g, "MimeType", &n, NULL);
    if (mimes != NULL && n > 0) {
        GString* list = g_string_new(NULL);
        for (gsize i = 0; i < n && i < 8; i++) {
            g_string_append_printf(list, "%s%s", i > 0 ? "\n" : "", mimes[i]);
        }
        if (n > 8) {
            g_string_append_printf(list, "\n… %" G_GSIZE_FORMAT " more", n - 8);
        }
        preview_doc_field(doc, "Opens", list->str);
        g_string_free(list, TRUE);
    }
    g_strfreev(mimes);

    gchar** groups = g_key_file_get_groups(kf, NULL);
    gboolean header = FALSE;
    for (gint i = 0; groups[i] != NULL; i++) {
        if (!g_str_has_prefix(groups[i], "Desktop Action ")) {
            continue;
        }
        if (!header) {
            preview_doc_section(doc, "Actions");
            header = TRUE;
        }
        gchar* action = g_key_file_get_locale_string(kf, groups[i], "Name", NULL, NULL);
        gchar* exec = g_key_file_get_string(kf, groups[i], "Exec", NULL);
        preview_doc_field_styled(doc, action != NULL ? action : groups[i] + 15, exec, PREVIEW_STYLE_MONO);
        g_free(exec);
        g_free(action);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
    return TRUE;
}

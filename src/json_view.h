#ifndef JSON_VIEW_H
#define JSON_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* JSON pretty-printer for the preview: parses a document (tolerant: comments,
 * trailing commas, unquoted keys, NaN / Infinity, as in JSONC and JSON5) or a
 * JSON Lines file (one value per line), and appends it re-indented with two
 * spaces and coloured like VS Code: keys, strings, numbers, keywords. Short
 * arrays of scalars stay on one line. Strings are kept as written (escapes
 * included). Pure GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define JSON_VIEW_MAX_LINES 10000

typedef struct {
    gboolean lines;        /* JSON Lines: several top-level values */
    guint n_values;        /* top-level values */
    gchar root;            /* '{', '[' or 0 (scalar) of the first value */
    guint n_root_items;    /* keys or items of the first value */
} JsonViewInfo;

/* Append the pretty-printed text to doc. FALSE with error set (line and
 * column of the problem) when the text is not JSON; doc is then unchanged. */
gboolean json_view_append(PreviewDoc* doc, const gchar* text, gsize len, JsonViewInfo* info,
                          GError** error);

/* --- Reading JSON values (notebooks...) --- */

typedef struct JsonValue JsonValue;

typedef enum {
    JSON_VALUE_OBJECT,
    JSON_VALUE_ARRAY,
    JSON_VALUE_STRING,
    JSON_VALUE_NUMBER,
    JSON_VALUE_KEYWORD   /* true, false, null */
} JsonValueType;

/* Parse one JSON value (same tolerance as json_view_append()); NULL with
 * error set if invalid. The tree points into text: keep text alive until
 * json_value_free(). */
JsonValue* json_value_parse(const gchar* text, gsize len, GError** error);
void json_value_free(JsonValue* value);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JsonValue, json_value_free)

JsonValueType json_value_get_type(const JsonValue* value);
/* Member of an object (NULL if missing or value is not an object) */
const JsonValue* json_value_get(const JsonValue* value, const gchar* key);
/* Items of an array or members of an object */
guint json_value_get_length(const JsonValue* value);
const JsonValue* json_value_get_item(const JsonValue* value, guint index);
/* String value unescaped, or the raw text of a number / keyword. Caller frees. */
gchar* json_value_dup_string(const JsonValue* value);
/* Text of a string, or the concatenation of an array of strings (Jupyter
 * "source" fields), NULL otherwise. Caller frees. */
gchar* json_value_dup_text(const JsonValue* value);
/* Number value (0 if not a number) */
gdouble json_value_get_number(const JsonValue* value);

#endif /* JSON_VIEW_H */

#ifndef GETTEXT_VIEW_H
#define GETTEXT_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* gettext translation catalogs for the preview: compiled .mo files and .po /
 * .pot sources. The catalog header (project, language, translator, revision
 * date, plural forms), the message counts (translated, fuzzy, untranslated),
 * then a table of the first messages and their translations. Pure GLib,
 * unit-tested. */

typedef struct {
    gchar* context;          /* msgctxt, or NULL */
    gchar* id;               /* msgid */
    gchar* plural;           /* msgid_plural, or NULL */
    gchar* translation;      /* msgstr (msgstr[0] for plurals) */
    gboolean fuzzy;
} GettextMessage;

typedef struct {
    GArray* messages;        /* GettextMessage, the header entry excluded */
    gchar* header;           /* the header entry's text ("Project-Id-Version: ...") */
} GettextCatalog;

/* Parse a .mo file (both byte orders) or a .po text; NULL with error set if
 * the data is neither */
GettextCatalog* gettext_parse_mo(const guchar* data, gsize len, GError** error);
GettextCatalog* gettext_parse_po(const gchar* text, gsize len, GError** error);
void gettext_catalog_free(GettextCatalog* catalog);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GettextCatalog, gettext_catalog_free)

/* Value of a header field ("Language"), or NULL. Caller frees. */
gchar* gettext_header_field(const GettextCatalog* catalog, const gchar* field);

/* Messages shown in the table */
#define GETTEXT_VIEW_MAX_ROWS 500

/* Append the header card, counts and messages table */
void gettext_view_append(PreviewDoc* doc, const GettextCatalog* catalog);

#endif /* GETTEXT_VIEW_H */

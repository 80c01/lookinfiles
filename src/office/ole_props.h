#ifndef OLE_PROPS_H
#define OLE_PROPS_H

/* Document properties of OLE2 compound files: the "\005SummaryInformation"
 * and "\005DocumentSummaryInformation" property sets written by Office
 * 97-2003 and LibreOffice (title, subject, author, last saved by, dates,
 * page / word / slide counts, application, company...), with their code
 * page. Shown as the summary card of the old Office previews. Pure GLib,
 * unit-tested. */
#include <glib.h>

#include "office/ole_reader.h"
#include "preview/preview_doc.h"

typedef struct {
    gchar* title;
    gchar* subject;
    gchar* author;
    gchar* keywords;
    gchar* comments;
    gchar* last_author;
    gchar* revision;
    gchar* app_name;
    gchar* category;
    gchar* manager;
    gchar* company;
    gint64 created;      /* Unix time (UTC), -1 when absent */
    gint64 modified;
    gint pages;          /* -1 when absent */
    gint words;
    gint chars;
    gint slides;
    gint notes;
    gboolean password;   /* security flag: password protected */
} OleProperties;

/* Both property sets of a compound file; NULL when it has none */
OleProperties* ole_properties_read(OleReader* ole);
void ole_properties_free(OleProperties* props);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OleProperties, ole_properties_free)

/* Parse one property set stream into props (fields already set are kept
 * unless the stream has them). FALSE when it is not a property set. */
gboolean ole_properties_parse(const guint8* data, gsize len, OleProperties* props);
OleProperties* ole_properties_new(void);

/* "Title / Author / Created / Modified / Pages..." fields of the card */
void ole_properties_card(PreviewDoc* doc, const OleProperties* props);

/* Subtitle of the old Office previews: kind ("Word 97-2003 document"),
 * then the author, the counts and the date modified. props may be NULL. */
gchar* ole_properties_subtitle(const OleProperties* props, const gchar* kind);

#endif /* OLE_PROPS_H */

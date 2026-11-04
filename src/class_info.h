#ifndef CLASS_INFO_H
#define CLASS_INFO_H

#include <glib.h>
#include "preview_doc.h"

/* Java bytecode (.class) parser and preview builder.
 * Extracts class name, superclass, interfaces, version, fields, and methods.
 * Pure GLib, Layer 1 foundation module (no widgets, unit-tested). */

typedef struct {
    guint16 major;
    guint16 minor;
    guint16 access_flags;
    gchar* this_class;
    gchar* super_class;
    GPtrArray* interfaces; /* gchar* */
    gchar* source_file;
    GPtrArray* fields;     /* gchar* formatted */
    GPtrArray* methods;    /* gchar* formatted */
} ClassInfo;

/* Parse Java class file bytes. Returns NULL with error set on invalid data. */
ClassInfo* class_info_parse(const guchar* data, gsize len, GError** error);
void class_info_free(ClassInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ClassInfo, class_info_free)

/* Appends structured Java class preview to doc */
gboolean class_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* CLASS_INFO_H */

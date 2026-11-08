#ifndef PYC_INFO_H
#define PYC_INFO_H

#include <glib.h>
#include "preview/preview_doc.h"

/* Python compiled bytecode (.pyc, .pyo) parser and preview builder.
 * Extracts Python target version, PEP 552 validation mode (timestamp / hash),
 * source file path, code object flags, functions/classes defined, and referenced names.
 * Layer 1 foundation module (pure GLib, no widgets, unit-tested). */

typedef struct {
    gchar* name;
    gchar* qualname;
    guint32 firstlineno;
    guint32 argcount;
    guint32 flags;
} PycFunction;

typedef struct {
    guint16 magic;
    const gchar* python_version;
    guint32 flags;
    gboolean hash_based;
    gboolean check_hash;
    gint64 source_mtime;
    guint32 source_size;
    gchar* hash_hex;
    gchar* filename;
    gchar* main_name;
    guint32 main_firstlineno;
    guint32 main_argcount;
    guint32 main_flags;
    GPtrArray* functions; /* PycFunction */
    GPtrArray* names;     /* gchar* */
    GPtrArray* varnames;  /* gchar* */
} PycInfo;

PycInfo* pyc_info_parse(const guchar* data, gsize len, GError** error);
void pyc_info_free(PycInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PycInfo, pyc_info_free)

/* Appends structured Python bytecode preview to doc */
gboolean pyc_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* PYC_INFO_H */

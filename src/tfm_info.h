#ifndef TFM_INFO_H
#define TFM_INFO_H

#include <glib.h>
#include "preview_doc.h"

/* TeX font metric (.tfm), virtual font (.vf) and PostScript Type 1 (.pfb) parser
 * and preview builder.
 * Extracts font family, coding scheme, design size, character counts, font parameters
 * (space, stretch, shrink, ex, em), and table dimensions.
 * Layer 1 foundation module (pure GLib, no widgets, unit-tested). */

typedef struct {
    const gchar* format;     /* "TeX Font Metric (.tfm)", "TeX Virtual Font (.vf)", "PostScript Type 1 (.pfb)" */
    gchar* font_family;      /* e.g. "CMR", "TCRM", "Times-Roman" */
    gchar* coding_scheme;    /* e.g. "TeX text companion symbols", "StandardEncoding" */
    gchar* full_name;        /* for .pfb */
    gchar* comment;          /* for .vf */
    guint32 checksum;
    gdouble design_size;     /* in TeX points (pt) */
    guint bc;                /* first char code */
    guint ec;                /* last char code */
    guint n_chars;           /* character count */
    guint nw;                /* width table entries */
    guint nh;                /* height table entries */
    guint nd;                /* depth table entries */
    guint ni;                /* italic table entries */
    guint nl;                /* lig/kern table entries */
    guint nk;                /* kern table entries */
    guint ne;                /* extensible table entries */
    guint np;                /* font parameters count */
    gboolean has_params;
    gdouble slant;
    gdouble space;
    gdouble space_stretch;
    gdouble space_shrink;
    gdouble x_height;
    gdouble quad;
    gdouble extra_space;
} TfmInfo;

TfmInfo* tfm_info_parse(const guchar* data, gsize len, GError** error);
void tfm_info_free(TfmInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(TfmInfo, tfm_info_free)

/* Appends structured TeX font preview to doc */
gboolean tfm_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* TFM_INFO_H */

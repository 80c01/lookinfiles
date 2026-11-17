#ifndef ODP_STYLES_H
#define ODP_STYLES_H

#include <glib.h>

#include "formats/zip_reader.h"

/* OpenDocument drawings (.odp) as a small element tree, and their styles:
 * the named, automatic and default styles of styles.xml and content.xml
 * with their parents, list styles (also those inside presentation styles),
 * font faces, gradients, fill images, page layouts and master pages;
 * lengths, colours and percentages. Pure GLib, unit-tested. */

/* An element, or a run of text (name NULL) */
typedef struct OdpNode {
    gchar* name;            /* qualified ("draw:frame"); NULL for text */
    gchar** attr_names;     /* qualified */
    gchar** attr_values;
    gchar* text;            /* text nodes only */
    GPtrArray* children;    /* OdpNode* */
} OdpNode;

/* Tree of a part of the archive; NULL with error set if it is missing (and
 * not optional: then NULL, error untouched), too large or not well-formed */
OdpNode* odp_node_parse(ZipReader* zip, const gchar* part, gsize max_size, gboolean optional, GError** error);
/* Tree of an XML text (tests) */
OdpNode* odp_node_parse_text(const gchar* xml, gssize len, GError** error);
void odp_node_free(OdpNode* node);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OdpNode, odp_node_free)

/* Value of an attribute by qualified name, or NULL */
const gchar* odp_node_attr(const OdpNode* node, const gchar* name);
/* First child element of that name, or NULL */
OdpNode* odp_node_child(const OdpNode* node, const gchar* name);
/* First element of that name at any depth below node, or NULL */
OdpNode* odp_node_find(const OdpNode* node, const gchar* name);
/* Whether the node is the element name */
gboolean odp_node_is(const OdpNode* node, const gchar* name);
/* All the text below a node, paragraphs on their own lines (caller frees) */
gchar* odp_node_text(const OdpNode* node);

/* ---- Values ---- */

/* A length in points ("2.54cm", "10mm", "1in", "12pt", "16px"); fallback
 * if missing or not a length */
gdouble odp_length(const gchar* value, gdouble fallback);
/* "#rrggbb" as 0xRRGGBB; fallback if missing or not a colour */
guint32 odp_color(const gchar* value, guint32 fallback);
/* "45%" as 0.45; -1 if not a percentage */
gdouble odp_percent(const gchar* value);
/* Unquoted first family of a font family list ("'Noto Sans', Arial") */
gchar* odp_first_family(const gchar* families);

/* ---- Styles ---- */

typedef struct OdpStyle OdpStyle;

#define ODP_LIST_LEVELS 10

typedef struct {
    gboolean set;
    gboolean numbered;
    gchar* bullet;          /* bullet character; NULL for none */
    gchar* num_format;      /* "1", "a", "A", "i", "I", "" */
    gchar* num_prefix;
    gchar* num_suffix;
    gint start;
    gdouble space_before;   /* label position from the paragraph margin */
    gdouble min_label_width;
    gboolean label_alignment; /* margins given as margin_left / text_indent */
    gdouble margin_left;
    gdouble text_indent;
    gchar* font;            /* bullet font family, NULL: the text's */
    gdouble size_percent;   /* bullet size relative to the text, 0: same */
    guint32 color;          /* 0xFFFFFFFF: the text's colour */
} OdpListLevel;

typedef struct {
    OdpListLevel levels[ODP_LIST_LEVELS];
} OdpList;

typedef struct {
    guint32 start, end;
    gdouble angle;          /* degrees, counter-clockwise, 0: top to bottom */
    gboolean axial;         /* end colour in the middle */
    gboolean radial;
} OdpGradient;

typedef struct {
    GHashTable* styles;     /* "family/name" -> OdpStyle* */
    GHashTable* defaults;   /* family -> OdpStyle* */
    GHashTable* lists;      /* name -> OdpList* */
    GHashTable* fonts;      /* font face name -> family */
    GHashTable* gradients;  /* name -> OdpGradient* */
    GHashTable* images;     /* fill image name -> href */
    GHashTable* layouts;    /* page layout name -> gdouble[2] (points) */
    GHashTable* masters;    /* master page name -> OdpNode* (in the trees) */
    GHashTable* templates;  /* table template name -> GHashTable* (part -> style name) */
    gchar* first_master;
} OdpStyles;

OdpStyles* odp_styles_new(void);
void odp_styles_free(OdpStyles* styles);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OdpStyles, odp_styles_free)

/* Add the styles of a document root (office:document-styles or
 * office:document-content); later ones override earlier names. The tree
 * must outlive the styles (masters point into it). */
void odp_styles_add(OdpStyles* styles, const OdpNode* root);

/* Font family of a font face name (style:font-name), or NULL */
const gchar* odp_styles_font(const OdpStyles* styles, const gchar* face);
/* List style by name, or NULL */
const OdpList* odp_styles_list(const OdpStyles* styles, const gchar* name);
/* List style inside a style or its parents (presentation styles), or NULL */
const OdpList* odp_styles_style_list(const OdpStyles* styles, const gchar* family, const gchar* name);
/* Size of the page layout of a master page (points); FALSE if unknown */
gboolean odp_styles_page_size(const OdpStyles* styles, const gchar* master, gdouble* width, gdouble* height);
/* Margins of the page layout of a master page (points: top, bottom, left,
 * right); FALSE if unknown */
gboolean odp_styles_page_margins(const OdpStyles* styles, const gchar* master, gdouble margins[4]);
/* Label of item n (from 1) of a numbered list level ("3.", "c)", "iv") */
gchar* odp_list_label(const OdpListLevel* level, gint n);
/* Style name of a part ("first-row", "body"...) of a table template, or NULL */
const gchar* odp_styles_template_part(const OdpStyles* styles, const gchar* name, const gchar* part);

/* A cascade: styles looked up in order, each followed by its parents, then
 * the default styles. The most specific style comes first. */
#define ODP_CASCADE_MAX 48

typedef struct {
    const OdpStyle* chain[ODP_CASCADE_MAX];
    guint n;
} OdpCascade;

/* Append a style and its parents (nothing if name is NULL or unknown) */
void odp_cascade_add(OdpCascade* c, const OdpStyles* styles, const gchar* family, const gchar* name);
/* Append the default style of a family */
void odp_cascade_add_default(OdpCascade* c, const OdpStyles* styles, const gchar* family);
/* Append another cascade */
void odp_cascade_append(OdpCascade* c, const OdpCascade* other);
/* First value of a property (qualified attribute name of a *-properties
 * element), or NULL */
const gchar* odp_cascade_get(const OdpCascade* c, const gchar* key);
/* Value of whichever of two properties the most specific style sets
 * (*second tells which), or NULL */
const gchar* odp_cascade_get_either(const OdpCascade* c, const gchar* key, const gchar* other, gboolean* second);
/* Same as odp_cascade_get(), skipping the default styles */
const gchar* odp_cascade_get_explicit(const OdpCascade* c, const gchar* key);
/* Font size in points with relative sizes ("120%") applied; fallback if none */
gdouble odp_cascade_font_size(const OdpCascade* c, gdouble fallback);

#endif /* ODP_STYLES_H */

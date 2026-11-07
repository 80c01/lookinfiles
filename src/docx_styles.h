#ifndef DOCX_STYLES_H
#define DOCX_STYLES_H

#include <glib.h>

#include "doc_writer.h"
#include "zip_reader.h"

/* Formatting of Word documents (.docx): run and paragraph properties
 * (w:rPr, w:pPr), the styles with their basedOn chains and document
 * defaults (styles.xml), the theme fonts (theme1.xml) and the list
 * numbering with its labels (numbering.xml). Used by docx. Pure GLib. */

/* Properties set on a run (a bit per property: unset ones are inherited) */
enum {
    DOCX_RP_BOLD = 1 << 0,
    DOCX_RP_ITALIC = 1 << 1,
    DOCX_RP_UNDERLINE = 1 << 2,
    DOCX_RP_STRIKE = 1 << 3,
    DOCX_RP_CAPS = 1 << 4,
    DOCX_RP_HIDDEN = 1 << 5,
    DOCX_RP_VERT = 1 << 6,
    DOCX_RP_SIZE = 1 << 7,
    DOCX_RP_COLOR = 1 << 8,
    DOCX_RP_HIGHLIGHT = 1 << 9,
    DOCX_RP_FONT = 1 << 10,
};

typedef struct {
    guint set;
    gboolean bold, italic, underline, strike, caps, hidden;
    gint vert;            /* 1 superscript, -1 subscript */
    gdouble size;         /* points */
    guint32 color;        /* DOC_COLOR_NONE: auto */
    guint32 highlight;    /* DOC_COLOR_NONE: none */
    gchar* font;
} DocxRunProps;

enum {
    DOCX_PP_ALIGN = 1 << 0,
    DOCX_PP_LEFT = 1 << 1,
    DOCX_PP_RIGHT = 1 << 2,
    DOCX_PP_FIRST = 1 << 3,
    DOCX_PP_BEFORE = 1 << 4,
    DOCX_PP_AFTER = 1 << 5,
    DOCX_PP_NUM = 1 << 6,
    DOCX_PP_LEVEL = 1 << 7,
    DOCX_PP_PAGE_BREAK = 1 << 8,
    DOCX_PP_OUTLINE = 1 << 9,
};

typedef struct {
    guint set;
    PreviewAlign align;
    gdouble left, right, first, before, after;   /* points */
    gint num_id;          /* 0: no numbering */
    gint ilvl;
    gboolean page_break_before;
    gint outline;         /* heading level 1-9 (Heading N style, w:outlineLvl), 0: body text */
} DocxParaProps;

void docx_run_props_clear(DocxRunProps* p);
/* Apply the properties set in src over dst */
void docx_run_props_merge(DocxRunProps* dst, const DocxRunProps* src);
void docx_para_props_merge(DocxParaProps* dst, const DocxParaProps* src);
/* The run as the writer takes it (font points into p) */
void docx_run_props_to_run(const DocxRunProps* p, DocRun* run);
void docx_para_props_to_para(const DocxParaProps* p, DocPara* para);

typedef struct DocxStyles DocxStyles;

/* Theme fonts and styles of a document (parts may be missing or NULL) */
DocxStyles* docx_styles_read(ZipReader* zip, const gchar* styles_part, const gchar* theme_part);
void docx_styles_free(DocxStyles* styles);

/* One child element of a w:rPr / w:pPr (the element name with its prefix):
 * FALSE when it is not a property read here */
gboolean docx_run_property(const DocxStyles* styles, DocxRunProps* p, const gchar* name, const gchar** names,
                           const gchar** values);
gboolean docx_para_property(DocxParaProps* p, const gchar* name, const gchar** names, const gchar** values);

/* Effective properties of a paragraph of style para_style (NULL: the
 * default paragraph style), and of a run of style run_style in it (out
 * is overwritten: clear it first when it holds a font) */
void docx_styles_para(const DocxStyles* styles, const gchar* para_style, DocxParaProps* out);
void docx_styles_run(const DocxStyles* styles, const gchar* para_style, const gchar* run_style, DocxRunProps* out);

typedef struct DocxNumbering DocxNumbering;

DocxNumbering* docx_numbering_read(ZipReader* zip, const gchar* part, const DocxStyles* styles);
void docx_numbering_free(DocxNumbering* num);

/* Label of the next item of list num_id at level ilvl ("•", "2.", "1.3."),
 * counting items like Word (a level restarts after a higher one); NULL when
 * the list is unknown or has no label. The level's indents are applied on
 * para (when the paragraph does not set its own) and its label font on
 * label_run. Caller frees. */
gchar* docx_numbering_label(DocxNumbering* num, gint num_id, gint ilvl, DocxParaProps* para,
                            DocxRunProps* label_run);

#endif /* DOCX_STYLES_H */

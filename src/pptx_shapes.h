#ifndef PPTX_SHAPES_H
#define PPTX_SHAPES_H

#include <glib.h>

#include "slide_render.h"
#include "formats/zip_reader.h"

/* DrawingML of PowerPoint presentations (.pptx): the theme (colour scheme,
 * fonts), the shape trees of slide masters, layouts, slides and notes
 * (shapes, pictures, groups, connectors, tables; placeholders; text with
 * list styles; fills, outlines and style references; backgrounds), and
 * the slides built from them with PowerPoint's inheritance (slide, then
 * its layout, then its master, then built-in defaults). Used by pptx.
 * Pure GLib (and the slide model). */

typedef struct PptxTheme PptxTheme;

/* Theme part (NULL or unreadable: the Office theme) */
PptxTheme* pptx_theme_read(ZipReader* zip, const gchar* part);
void pptx_theme_free(PptxTheme* theme);
/* Minor (body) font of the theme, NULL if unknown */
const gchar* pptx_theme_font(const PptxTheme* theme);

typedef struct PptxTree PptxTree;

/* Shape tree of a master, layout, slide or notes part. master gives the
 * colour map of the others (NULL when reading a master). */
PptxTree* pptx_tree_read(ZipReader* zip, const gchar* part, const PptxTheme* theme, const PptxTree* master);
void pptx_tree_free(PptxTree* tree);

/* The slide drawn from its tree, its layout and master (both may be NULL)
 * on a page width x height points */
Slide* pptx_build_slide(const PptxTree* slide, const PptxTree* layout, const PptxTree* master,
                        const PptxTheme* theme, gdouble width, gdouble height);

/* Text of the body placeholder of a notes tree (NULL if none) */
gchar* pptx_tree_notes(const PptxTree* notes);

#endif /* PPTX_SHAPES_H */

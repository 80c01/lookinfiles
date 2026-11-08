#ifndef CANVAS_BIND_H
#define CANVAS_BIND_H

/* Lines and arrows bound to blocks on the Canvas page: an end drawn on a
 * block is bound to it (CanvasItem start_id / end_id) and stays on its
 * outline when the block moves or changes size. */
#include "canvas/canvas_item.h"

/* A line or an arrow was just drawn: bind its ends to the blocks under
 * them, and put them on their outlines */
void canvas_bind_attach(GtkWidget* surface, CanvasItem* line);

/* An end of a line or an arrow (end: the last one, else the first) was just
 * moved: bind it to the block under it, if any, and put it on its outline */
void canvas_bind_attach_end(GtkWidget* surface, CanvasItem* line, gboolean end);

/* Put the bound ends of every line and arrow on the page back on their
 * blocks */
void canvas_bind_follow(GtkWidget* surface);

/* The blocks of moved (CanvasItem*) were moved together: the lines among
 * them let go of the blocks left behind */
void canvas_bind_release(GPtrArray* moved);

/* The block id left the page: the ends bound to it are free */
void canvas_bind_forget(GtkWidget* surface, gint id);

#endif /* CANVAS_BIND_H */

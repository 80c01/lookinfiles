#ifndef CANVAS_TOOLBAR_H
#define CANVAS_TOOLBAR_H

#include "canvas_doc.h"
#include "canvas_geometry.h"
#include "canvas_text.h"
#include <gtk/gtk.h>

typedef struct CanvasToolbar CanvasToolbar;

typedef struct {
    void (*on_add_text)(gpointer udata);
    void (*on_add_shape)(CanvasShapeType type, gpointer udata);
    void (*on_add_table)(gint rows, gint cols, gpointer udata);
    void (*on_crop_image)(gpointer udata);
    void (*on_format_style)(guint style_flag, gpointer udata);
    void (*on_format_preset)(CanvasPreset preset, gpointer udata);
    void (*on_format_list)(const gchar* prefix, gpointer udata);
    void (*on_format_indent)(gint step, gpointer udata);
    void (*on_format_checkbox)(gpointer udata);
    void (*on_toggle_grid)(gboolean enabled, gpointer udata);
    void (*on_align)(CanvasAlignOp op, gpointer udata);
} CanvasToolbarCallbacks;

CanvasToolbar* canvas_toolbar_new(const CanvasToolbarCallbacks* cbs, gpointer user_data);
GtkWidget* canvas_toolbar_get_widget(CanvasToolbar* tb);
void canvas_toolbar_free(CanvasToolbar* tb);

#endif /* CANVAS_TOOLBAR_H */

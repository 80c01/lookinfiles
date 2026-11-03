#ifndef CANVAS_MEDIA_H
#define CANVAS_MEDIA_H

#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

typedef void (*CanvasNewPictureFunc)(CanvasPage* p, const gchar* src, gint x, gint y, gint w, gint h);

/* Read picture texture from clipboard, save to media/ and invoke callback */
gboolean canvas_media_paste_picture(GtkWidget* root, const gchar* dir,
                                    gint x, gint y,
                                    CanvasNewPictureFunc on_new_picture,
                                    CanvasPage* page);

/* Process dropped image files, save to media/ and invoke callback */
gboolean canvas_media_drop_files(const GValue* value, const gchar* dir,
                                 gint start_x, gint start_y, gint offset_step,
                                 CanvasNewPictureFunc on_new_picture,
                                 CanvasPage* page);

#endif /* CANVAS_MEDIA_H */

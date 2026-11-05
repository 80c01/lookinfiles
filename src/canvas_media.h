#ifndef CANVAS_MEDIA_H
#define CANVAS_MEDIA_H

#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

typedef void (*CanvasNewPictureFunc)(CanvasPage* p, const gchar* src, gint x, gint y, gint w, gint h);

/* TRUE if the clipboard holds a picture (read it with
 * gdk_clipboard_read_texture_async()) */
gboolean canvas_media_clipboard_has_picture(GdkClipboard* clipboard);

/* TRUE if the clipboard also holds text */
gboolean canvas_media_clipboard_has_text(GdkClipboard* clipboard);

/* Save a picture as a new "media/NNNN.png" of the note in dir: its path
 * relative to dir (free it), or NULL if it cannot be written */
gchar* canvas_media_save_texture(GdkTexture* texture, const gchar* dir);

/* Process dropped image files, save to media/ and invoke callback */
gboolean canvas_media_drop_files(const GValue* value, const gchar* dir,
                                 gint start_x, gint start_y, gint offset_step,
                                 CanvasNewPictureFunc on_new_picture,
                                 CanvasPage* page);

#endif /* CANVAS_MEDIA_H */

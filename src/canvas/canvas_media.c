#include "canvas/canvas_media.h"
#include "canvas/canvas_doc.h"

#include <string.h>

static gboolean add_single_file(GFile* file, const gchar* dir, gint x, gint y,
                                CanvasNewPictureFunc on_new_picture, CanvasPage* page) {
    if (dir == NULL || file == NULL) return FALSE;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, &error);
    const gchar* type = (info != NULL) ? g_file_info_get_content_type(info) : NULL;
    if (type == NULL || !g_content_type_is_a(type, "image/*")) return FALSE;

    gchar* path = g_file_get_path(file);
    if (path == NULL) return FALSE;
    GdkTexture* texture = gdk_texture_new_from_filename(path, &error);
    g_free(path);
    if (texture == NULL) return FALSE;

    gchar* base = g_file_get_basename(file);
    const gchar* dot = strrchr(base, '.');
    gchar* src = canvas_doc_new_media_path(dir, (dot != NULL) ? dot + 1 : "png");
    gchar* full = g_build_filename(dir, src, NULL);
    gchar* media = g_path_get_dirname(full);
    g_mkdir_with_parents(media, 0755);

    GFile* dest = g_file_new_for_path(full);
    gboolean ok = g_file_copy(file, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    if (ok && on_new_picture != NULL) {
        on_new_picture(page, src, x, y, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
    }
    g_object_unref(dest);
    g_free(media);
    g_free(full);
    g_free(src);
    g_free(base);
    g_object_unref(texture);
    return ok;
}

gboolean canvas_media_clipboard_has_picture(GdkClipboard* clipboard) {
    return clipboard != NULL &&
           gdk_content_formats_contain_gtype(gdk_clipboard_get_formats(clipboard), GDK_TYPE_TEXTURE);
}

gboolean canvas_media_clipboard_has_text(GdkClipboard* clipboard) {
    return clipboard != NULL &&
           gdk_content_formats_contain_gtype(gdk_clipboard_get_formats(clipboard), G_TYPE_STRING);
}

gchar* canvas_media_save_texture(GdkTexture* texture, const gchar* dir) {
    if (texture == NULL || dir == NULL) return NULL;
    gchar* src = canvas_doc_new_media_path(dir, "png");
    gchar* full = g_build_filename(dir, src, NULL);
    gchar* media = g_path_get_dirname(full);
    g_mkdir_with_parents(media, 0755);
    g_free(media);
    gboolean ok = gdk_texture_save_to_png(texture, full);
    g_free(full);
    if (!ok) g_clear_pointer(&src, g_free);
    return src;
}

gboolean canvas_media_drop_files(const GValue* value, const gchar* dir,
                                 gint start_x, gint start_y, gint offset_step,
                                 CanvasNewPictureFunc on_new_picture,
                                 CanvasPage* page) {
    if (dir == NULL || !G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;
    gboolean any = FALSE;
    gint offset = 0;
    GSList* files = gdk_file_list_get_files(g_value_get_boxed(value));
    for (GSList* l = files; l != NULL; l = l->next) {
        if (add_single_file(G_FILE(l->data), dir, start_x + offset, start_y + offset, on_new_picture, page)) {
            any = TRUE;
            offset += offset_step;
        }
    }
    g_slist_free(files);
    return any;
}

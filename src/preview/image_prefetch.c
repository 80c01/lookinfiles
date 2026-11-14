/* Decoding ahead the images next to the previewed one (see image_prefetch.h) */
#include "preview/image_prefetch.h"

#include "core/file_types.h"
#include "explorer/archive_tree.h"
#include "explorer/explorer_row.h"
#include "explorer/explorer_tree.h"
#include "media/image_preview.h"

/* Rows decoded ahead, from the selection: going down first */
static const gint neighbour_offsets[] = {1, -1, 2};

static gboolean prefetchable(ExplorerRow* row) {
    return row != NULL && g_strcmp0(row->type, "file") == 0 && row->path[0] != '\0' &&
           file_kind_from_name(row->name) == FILE_KIND_IMAGE &&
           g_ascii_strcasecmp(file_extension(row->name), "gif") != 0 && !archive_tree_is_entry(row);
}

void image_prefetch_neighbours(AppData* app) {
    ExplorerRow* sel = app->tree != NULL ? explorer_tree_get_selected(app->tree) : NULL;
    gint pos = sel != NULL ? explorer_tree_position(app->tree, sel) : -1;
    GPtrArray* paths = g_ptr_array_new();
    if (pos >= 0) {
        gint n = (gint)explorer_tree_n_visible(app->tree);
        for (guint i = 0; i < G_N_ELEMENTS(neighbour_offsets); i++) {
            gint p = pos + neighbour_offsets[i];
            ExplorerRow* row = (p >= 0 && p < n) ? explorer_tree_visible_row(app->tree, (guint)p) : NULL;
            if (prefetchable(row)) {
                g_ptr_array_add(paths, row->path);
            }
        }
    }
    g_ptr_array_add(paths, NULL);
    image_preview_prefetch(app, (const gchar* const*)paths->pdata);
    g_ptr_array_free(paths, TRUE);
}

/* Code navigation history (see jump_list.h) */
#include "jump_list.h"
#include <string.h>

struct JumpList {
    GArray* items;  /* JumpLocation */
    guint pos;      /* index of the location shown, items->len when not walking */
    guint max;
};

static void clear_location(gpointer p) {
    JumpLocation* loc = p;
    g_free(loc->path);
}

JumpList* jump_list_new(guint max) {
    JumpList* list = g_new0(JumpList, 1);
    list->items = g_array_new(FALSE, TRUE, sizeof(JumpLocation));
    g_array_set_clear_func(list->items, clear_location);
    list->max = max > 1 ? max : 2;
    return list;
}

void jump_list_free(JumpList* list) {
    if (list == NULL) return;
    g_array_free(list->items, TRUE);
    g_free(list);
}

static JumpLocation* at(JumpList* list, guint i) {
    return &g_array_index(list->items, JumpLocation, i);
}

static gboolean same_place(const JumpLocation* loc, const gchar* path, gint line) {
    return loc->line == line && g_strcmp0(loc->path, path) == 0;
}

static void append(JumpList* list, const gchar* path, gint line, gint column) {
    if (list->items->len > 0 && same_place(at(list, list->items->len - 1), path, line)) {
        g_array_remove_index(list->items, list->items->len - 1);
    }
    JumpLocation loc = {g_strdup(path), line, column};
    g_array_append_val(list->items, loc);
    while (list->items->len > list->max) {
        g_array_remove_index(list->items, 0);
        if (list->pos > 0) list->pos--;
    }
}

void jump_list_push(JumpList* list, const gchar* path, gint line, gint column) {
    if (list == NULL || path == NULL) return;
    if (list->pos < list->items->len) g_array_set_size(list->items, list->pos);
    append(list, path, line, column);
    list->pos = list->items->len;
}

const JumpLocation* jump_list_back(JumpList* list, const gchar* path, gint line, gint column) {
    if (list == NULL || list->items->len == 0) return NULL;
    if (list->pos >= list->items->len) {
        /* Leaving the live position: remember it for forward */
        if (path != NULL && !same_place(at(list, list->items->len - 1), path, line)) {
            append(list, path, line, column);
        }
        list->pos = list->items->len - 1;
    }
    if (list->pos == 0) return NULL;
    list->pos--;
    return at(list, list->pos);
}

const JumpLocation* jump_list_forward(JumpList* list) {
    if (list == NULL || list->pos + 1 >= list->items->len) return NULL;
    list->pos++;
    return at(list, list->pos);
}

guint jump_list_length(JumpList* list) {
    return list != NULL ? list->items->len : 0;
}

guint jump_list_position(JumpList* list) {
    return list != NULL ? list->pos : 0;
}

void jump_list_forget_file(JumpList* list, const gchar* path) {
    if (list == NULL || path == NULL) return;
    for (guint i = list->items->len; i-- > 0;) {
        if (g_strcmp0(at(list, i)->path, path) == 0) {
            g_array_remove_index(list->items, i);
            if (list->pos > i) list->pos--;
        }
    }
    if (list->pos > list->items->len) list->pos = list->items->len;
}

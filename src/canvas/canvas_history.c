/* Undo / redo history of a Canvas note (see canvas_history.h). */
#include "canvas/canvas_history.h"

struct CanvasHistory {
    GQueue undo; /* gchar*, the most recent first */
    GQueue redo;
    guint limit;
};

CanvasHistory* canvas_history_new(guint limit) {
    CanvasHistory* h = g_new0(CanvasHistory, 1);
    g_queue_init(&h->undo);
    g_queue_init(&h->redo);
    h->limit = MAX(limit, 1);
    return h;
}

void canvas_history_clear_redo(CanvasHistory* h) {
    g_queue_clear_full(&h->redo, g_free);
}

void canvas_history_clear(CanvasHistory* h) {
    g_queue_clear_full(&h->undo, g_free);
    canvas_history_clear_redo(h);
}

void canvas_history_free(CanvasHistory* h) {
    if (h == NULL) return;
    canvas_history_clear(h);
    g_free(h);
}

static void push_limited(CanvasHistory* h, GQueue* q, gchar* state) {
    g_queue_push_head(q, state);
    while (q->length > h->limit) g_free(g_queue_pop_tail(q));
}

void canvas_history_push(CanvasHistory* h, const gchar* state) {
    if (state == NULL) return;
    canvas_history_clear_redo(h);
    if (g_strcmp0(g_queue_peek_head(&h->undo), state) == 0) return;
    push_limited(h, &h->undo, g_strdup(state));
}

gboolean canvas_history_can_undo(const CanvasHistory* h) {
    return h->undo.length > 0;
}

gboolean canvas_history_can_redo(const CanvasHistory* h) {
    return h->redo.length > 0;
}

static gchar* step(CanvasHistory* h, GQueue* from, GQueue* to, const gchar* current) {
    if (g_queue_is_empty(from) || current == NULL) return NULL;
    push_limited(h, to, g_strdup(current));
    return g_queue_pop_head(from);
}

gchar* canvas_history_undo(CanvasHistory* h, const gchar* current) {
    return step(h, &h->undo, &h->redo, current);
}

gchar* canvas_history_redo(CanvasHistory* h, const gchar* current) {
    return step(h, &h->redo, &h->undo, current);
}

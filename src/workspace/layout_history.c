/* Per-workspace layout history (see layout_history.h) */
#include "workspace/layout_history.h"
#include "core/app_events.h"
#include "editor/editor.h"
#include "editor/editor_tagbar.h"
#include "terminal/terminal.h"
#include "widgets/panels.h"
#include "workspace/editor_layout.h"
#include "workspace/layout_history_stack.h"
#include "workspace/layouts.h"

#define LAYOUT_HISTORY_MAX 50
#define SETTLE_DELAY_MS 150

typedef struct {
    SavedLayout* panels;
    EditorSnapshot* editor;
    EditorTagbarStates* tagbars;
#ifdef HAVE_VTE
    TermSnapshot* terms;
#endif
} LayoutEntry;

struct LayoutHistory {
    LayoutHistoryStack* stack;
};

struct LayoutHistoryState {
    gboolean enabled;
    gboolean busy;    /* capturing, restoring or updating chrome */
    guint settle_id;  /* pending settle timeout, 0 = none */
};

static void entry_free(gpointer data) {
    LayoutEntry* e = data;
    if (e == NULL) {
        return;
    }
    saved_layout_free(e->panels);
    editor_snapshot_free(e->editor);
    editor_tagbar_states_free(e->tagbars);
#ifdef HAVE_VTE
    term_snapshot_free(e->terms);
#endif
    g_free(e);
}

static struct LayoutHistoryState* state_get(AppData* app) {
    if (app == NULL) {
        return NULL;
    }
    if (app->layout_history_state == NULL) {
        app->layout_history_state = g_new0(struct LayoutHistoryState, 1);
    }
    return app->layout_history_state;
}

static LayoutHistoryStack* current_stack(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return NULL;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    if (ws->layout_history == NULL) {
        struct LayoutHistory* h = g_new0(struct LayoutHistory, 1);
        h->stack = layout_history_stack_new(LAYOUT_HISTORY_MAX, entry_free);
        ws->layout_history = h;
    }
    return ws->layout_history->stack;
}

static void cancel_settle(AppData* app) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st != NULL && st->settle_id != 0) {
        g_source_remove(st->settle_id);
        st->settle_id = 0;
    }
}

static void notify_chrome(AppData* app) {
    struct LayoutHistoryState* st = state_get(app);
    if (st == NULL) {
        return;
    }
    gboolean prev = st->busy;
    st->busy = TRUE;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    st->busy = prev;
}

static LayoutEntry* capture_entry(AppData* app) {
    struct LayoutHistoryState* st = state_get(app);
    gboolean prev = st->busy;
    st->busy = TRUE; /* layouts_capture() emits APP_EVENT_LAYOUT_CHANGED */
    LayoutEntry* e = g_new0(LayoutEntry, 1);
    e->panels = layouts_capture(app, "");
    e->editor = editor_layout_snapshot(app);
    e->tagbars = editor_tagbar_states_take(app);
#ifdef HAVE_VTE
    e->terms = term_snapshot_take(app);
#endif
    st->busy = prev;
    return e;
}

static gboolean entries_same(const LayoutEntry* a, const LayoutEntry* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (!layout_history_same_panels(a->panels, b->panels)) {
        return FALSE;
    }
    if (!editor_snapshot_same_panes(a->editor, b->editor)) {
        return FALSE;
    }
    if (!editor_tagbar_states_same(a->tagbars, b->tagbars)) {
        return FALSE;
    }
#ifdef HAVE_VTE
    if (a->panels->term_vis && !term_snapshot_same_panes(a->terms, b->terms)) {
        return FALSE;
    }
#endif
    return TRUE;
}

static gboolean record_now(AppData* app, gboolean notify) {
    LayoutHistoryStack* stack = current_stack(app);
    if (stack == NULL) {
        return FALSE;
    }
    LayoutEntry* next = capture_entry(app);
    const LayoutEntry* cur = layout_history_stack_current(stack);
    if (cur != NULL && entries_same(cur, next)) {
        entry_free(next);
        return FALSE;
    }
    layout_history_stack_push(stack, next);
    if (notify) {
        notify_chrome(app);
    }
    return TRUE;
}

static gboolean on_settle_timeout(gpointer data) {
    AppData* app = data;
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL) {
        return G_SOURCE_REMOVE;
    }
    st->settle_id = 0;
    if (!st->enabled || st->busy || app->switching_ws ||
        panels_is_resizing(app) || layouts_preview_active()) {
        return G_SOURCE_REMOVE;
    }
    if (app_layout_held(app)) {
        st->settle_id = g_timeout_add(SETTLE_DELAY_MS, on_settle_timeout, app);
        return G_SOURCE_REMOVE;
    }
    record_now(app, TRUE);
    return G_SOURCE_REMOVE;
}

void layout_history_init(AppData* app) {
    struct LayoutHistoryState* st = state_get(app);
    if (st == NULL) {
        return;
    }
    cancel_settle(app);
    st->enabled = TRUE;
    LayoutHistoryStack* stack = current_stack(app);
    if (stack != NULL && layout_history_stack_length(stack) == 0) {
        record_now(app, FALSE);
    }
    notify_chrome(app);
}

void layout_history_free(AppData* app) {
    if (app == NULL) {
        return;
    }
    cancel_settle(app);
    for (gint i = 0; i < app->num_workspaces; i++) {
        layout_history_workspace_removed(&app->workspaces[i]);
    }
    g_clear_pointer(&app->layout_history_state, g_free);
}

void layout_history_on_change(AppData* app) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL || !st->enabled || st->busy || app->switching_ws ||
        panels_is_resizing(app) || layouts_preview_active()) {
        return;
    }
    if (st->settle_id != 0) {
        g_source_remove(st->settle_id);
    }
    st->settle_id = g_timeout_add(SETTLE_DELAY_MS, on_settle_timeout, app);
}

void layout_history_flush(AppData* app) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL || !st->enabled || st->busy || app->switching_ws ||
        panels_is_resizing(app) || layouts_preview_active()) {
        cancel_settle(app);
        return;
    }
    if (st->settle_id != 0) {
        g_source_remove(st->settle_id);
        st->settle_id = 0;
        record_now(app, FALSE);
    }
}

void layout_history_workspace_changed(AppData* app) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL || !st->enabled) {
        return;
    }
    cancel_settle(app);
    LayoutHistoryStack* stack = current_stack(app);
    if (stack != NULL && layout_history_stack_length(stack) == 0) {
        record_now(app, FALSE);
    }
    notify_chrome(app);
}

void layout_history_reset_current(AppData* app) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL || !st->enabled) {
        return;
    }
    cancel_settle(app);
    LayoutHistoryStack* stack = current_stack(app);
    if (stack != NULL) {
        layout_history_stack_clear(stack);
        record_now(app, FALSE);
    }
    notify_chrome(app);
}

void layout_history_workspace_removed(VirtualLayout* ws) {
    if (ws != NULL && ws->layout_history != NULL) {
        layout_history_stack_free(ws->layout_history->stack);
        g_free(ws->layout_history);
        ws->layout_history = NULL;
    }
}

static void step(AppData* app, gboolean back) {
    struct LayoutHistoryState* st = (app != NULL) ? app->layout_history_state : NULL;
    if (st == NULL || !st->enabled || st->busy || app->switching_ws || layouts_preview_active()) {
        return;
    }
    layout_history_flush(app);
    LayoutHistoryStack* stack = current_stack(app);
    if (stack == NULL) {
        return;
    }
    const LayoutEntry* target = back ? layout_history_stack_back(stack)
                                     : layout_history_stack_forward(stack);
    if (target == NULL) {
        return;
    }
    st->busy = TRUE;
    editor_layout_restore(app, target->editor, NULL);
#ifdef HAVE_VTE
    term_snapshot_restore(app, target->terms, NULL);
#endif
    editor_remove_empty_notebooks(app);
    layouts_apply_panels(app, target->panels);
    editor_tagbar_states_restore(app, target->tagbars);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    st->busy = FALSE;
}

void layout_history_back(AppData* app) {
    step(app, TRUE);
}

void layout_history_forward(AppData* app) {
    step(app, FALSE);
}

gboolean layout_history_can_go(AppData* app, gboolean back) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return FALSE;
    }
    struct LayoutHistory* h = app->workspaces[app->current_ws].layout_history;
    if (h == NULL || h->stack == NULL) {
        return FALSE;
    }
    return back ? layout_history_stack_can_back(h->stack)
                : layout_history_stack_can_forward(h->stack);
}

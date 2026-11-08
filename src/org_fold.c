/* Folding state of the Org preview (see org_fold.h). */
#include "org_fold.h"
#include "app_events.h"
#include "file_types.h"

/* Private to this file */
struct OrgFold {
    GHashTable* lines; /* headings folded by hand: GINT_TO_POINTER(file_line) */
    gint global;       /* 0 = all expanded, 1 = overview, 2 = contents */
};

static struct OrgFold* fold_get(AppData* app) {
    if (app->org_fold == NULL) {
        app->org_fold = g_new0(struct OrgFold, 1);
        app->org_fold->lines = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    return app->org_fold;
}

static gboolean refresh_org_preview(AppData* app) {
    if (app->viewed_path != NULL && is_org_file(app->viewed_path)) {
        app_emit(app, APP_EVENT_PREVIEW_STALE);
        return TRUE;
    }
    return FALSE;
}

gboolean orgmode_toggle_heading_fold(AppData* app, gint file_line) {
    if (app == NULL) return FALSE;
    GHashTable* lines = fold_get(app)->lines;
    gpointer key = GINT_TO_POINTER(file_line);
    if (!g_hash_table_remove(lines, key)) {
        g_hash_table_add(lines, key);
    }
    return refresh_org_preview(app);
}

void orgmode_cycle_global_fold(AppData* app) {
    if (app == NULL) return;
    struct OrgFold* f = fold_get(app);
    f->global = (f->global + 1) % 3;
    g_hash_table_remove_all(f->lines);
    refresh_org_preview(app);
}

GHashTable* org_fold_state(AppData* app, gint* global) {
    struct OrgFold* f = (app != NULL) ? app->org_fold : NULL;
    if (global != NULL) {
        *global = f != NULL ? f->global : 0;
    }
    return f != NULL ? f->lines : NULL;
}

void org_fold_free(AppData* app) {
    if (app == NULL || app->org_fold == NULL) {
        return;
    }
    g_hash_table_destroy(app->org_fold->lines);
    g_clear_pointer(&app->org_fold, g_free);
}

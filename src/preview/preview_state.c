/* What the preview shows (see preview_state.h). */
#include "preview/preview_state.h"

/* Private to this file */
struct PreviewState {
    gchar* path;
    gchar* name;
    FileKind kind;
    gchar* content_type;
    const gchar* encoding; /* static string */
    gboolean raw;
    gdouble zoom;
    guint page;
    guint pages;
};

static struct PreviewState* state_get(AppData* app) {
    if (app->preview_state == NULL) {
        app->preview_state = g_new0(struct PreviewState, 1);
        app->preview_state->zoom = 1.0;
    }
    return app->preview_state;
}

const gchar* preview_state_path(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->path : NULL;
}

const gchar* preview_state_name(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->name : NULL;
}

void preview_state_set_file(AppData* app, const gchar* path, const gchar* name) {
    struct PreviewState* s = state_get(app);
    /* Copied first: path or name may be the current value */
    gchar* new_path = g_strdup(path);
    gchar* new_name = (path != NULL) ? g_strdup(name) : NULL;
    g_free(s->path);
    g_free(s->name);
    s->path = new_path;
    s->name = new_name;
}

FileKind preview_state_kind(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->kind : FILE_KIND_TEXT;
}

const gchar* preview_state_content_type(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->content_type : NULL;
}

const gchar* preview_state_encoding(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->encoding : NULL;
}

gboolean preview_state_set_detected(AppData* app, FileKind kind, const gchar* encoding,
                                    const gchar* content_type) {
    struct PreviewState* s = state_get(app);
    if (s->kind == kind && s->encoding == encoding &&
        g_strcmp0(s->content_type, content_type) == 0) {
        return FALSE;
    }
    gchar* type = g_strdup(content_type);
    g_free(s->content_type);
    s->content_type = type;
    s->kind = kind;
    s->encoding = encoding;
    return TRUE;
}

gboolean preview_state_raw(const AppData* app) {
    return app != NULL && app->preview_state != NULL && app->preview_state->raw;
}

void preview_state_set_raw(AppData* app, gboolean raw) {
    state_get(app)->raw = raw;
}

gdouble preview_state_zoom(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->zoom : 1.0;
}

void preview_state_set_zoom(AppData* app, gdouble zoom) {
    state_get(app)->zoom = zoom;
}

guint preview_state_page(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->page : 0;
}

guint preview_state_pages(const AppData* app) {
    return (app != NULL && app->preview_state != NULL) ? app->preview_state->pages : 0;
}

void preview_state_set_page(AppData* app, guint page, guint pages) {
    struct PreviewState* s = state_get(app);
    s->pages = pages;
    s->page = MIN(page, pages);
}

void preview_state_free(AppData* app) {
    struct PreviewState* s = (app != NULL) ? app->preview_state : NULL;
    if (s == NULL) {
        return;
    }
    g_free(s->path);
    g_free(s->name);
    g_free(s->content_type);
    g_free(s);
    app->preview_state = NULL;
}

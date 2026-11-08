/* Application event bus (see app_events.h). */
#include "app_events.h"

#define APP_EVENT_MAX_HANDLERS 4

typedef struct AppEvents {
    AppEventHandler handlers[APP_EVENT_COUNT][APP_EVENT_MAX_HANDLERS];
    gint count[APP_EVENT_COUNT];
    gchar* notice;       /* app_notice() message, NULL if none */
    gboolean notice_undo; /* the message offers Undo */
    guint notice_serial; /* bumped by each app_notice() (restarts its timeout) */
    gboolean layout_held; /* app_layout_begin() until app_layout_release() */
} AppEvents;

static AppEvents* events_get(AppData* app) {
    if (app->events == NULL) {
        app->events = g_new0(AppEvents, 1);
    }
    return app->events;
}

void app_events_free(AppData* app) {
    if (app == NULL || app->events == NULL) {
        return;
    }
    g_free(app->events->notice);
    g_clear_pointer(&app->events, g_free);
}

void app_events_subscribe(AppData* app, AppEvent event, AppEventHandler handler) {
    g_return_if_fail(app != NULL && handler != NULL);
    g_return_if_fail(event >= 0 && event < APP_EVENT_COUNT);
    AppEvents* ev = events_get(app);
    g_return_if_fail(ev->count[event] < APP_EVENT_MAX_HANDLERS);
    ev->handlers[event][ev->count[event]++] = handler;
}

void app_emit(AppData* app, AppEvent event) {
    if (app == NULL || app->events == NULL || event < 0 || event >= APP_EVENT_COUNT) {
        return;
    }
    AppEvents* ev = app->events;
    for (gint i = 0; i < ev->count[event]; i++) {
        ev->handlers[event][i](app);
    }
}

void app_notice(AppData* app, const gchar* text, gboolean undo) {
    if (app == NULL) {
        return;
    }
    AppEvents* ev = events_get(app);
    g_free(ev->notice);
    ev->notice = g_strdup(text);
    ev->notice_undo = (text != NULL && undo);
    ev->notice_serial++;
    app_emit(app, APP_EVENT_STATUS_CHANGED);
}

const gchar* app_notice_get(AppData* app, gboolean* undo, guint* serial) {
    const AppEvents* ev = (app != NULL) ? (const AppEvents*)app->events : NULL;
    if (undo != NULL) {
        *undo = ev != NULL && ev->notice_undo;
    }
    if (serial != NULL) {
        *serial = ev != NULL ? ev->notice_serial : 0;
    }
    return ev != NULL ? ev->notice : NULL;
}

void app_layout_begin(AppData* app) {
    if (app == NULL) {
        return;
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app_layout_hold(app);
}

void app_layout_hold(AppData* app) {
    if (app != NULL) {
        events_get(app)->layout_held = TRUE;
    }
}

void app_layout_release(AppData* app) {
    if (app != NULL && app->events != NULL) {
        app->events->layout_held = FALSE;
    }
}

gboolean app_layout_held(AppData* app) {
    return app != NULL && app->events != NULL && app->events->layout_held;
}

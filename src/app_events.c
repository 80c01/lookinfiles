/* Application event bus (see app_events.h). */
#include "app_events.h"

#define APP_EVENT_MAX_HANDLERS 4

typedef struct {
    AppEventHandler handlers[APP_EVENT_COUNT][APP_EVENT_MAX_HANDLERS];
    gint count[APP_EVENT_COUNT];
} AppEvents;

void app_events_subscribe(AppData* app, AppEvent event, AppEventHandler handler) {
    g_return_if_fail(app != NULL && handler != NULL);
    g_return_if_fail(event >= 0 && event < APP_EVENT_COUNT);
    if (app->events == NULL) {
        app->events = g_new0(AppEvents, 1);
    }
    AppEvents* ev = (AppEvents*)app->events;
    g_return_if_fail(ev->count[event] < APP_EVENT_MAX_HANDLERS);
    ev->handlers[event][ev->count[event]++] = handler;
}

void app_emit(AppData* app, AppEvent event) {
    if (app == NULL || app->events == NULL || event < 0 || event >= APP_EVENT_COUNT) {
        return;
    }
    AppEvents* ev = (AppEvents*)app->events;
    for (gint i = 0; i < ev->count[event]; i++) {
        ev->handlers[event][i](app);
    }
}

void app_notice(AppData* app, const gchar* text, gboolean undo) {
    if (app == NULL) {
        return;
    }
    g_free(app->status_notice);
    app->status_notice = g_strdup(text);
    app->status_notice_undo = (text != NULL && undo);
    app->status_notice_serial++;
    app_emit(app, APP_EVENT_STATUS_CHANGED);
}

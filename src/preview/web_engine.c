/* The web view of a browser tab on WebKitGTK 6.0 (see web_engine.h); the
 * WebView2 engine is in web_engine_win.c */
#include "preview/web_engine.h"
#include "core/i18n.h"
#include "preview/web_address.h"

#if defined(HAVE_WEBKIT)
#include <webkit/webkit.h>

#define ENGINE_KEY "lookin-web-engine"

typedef struct {
    const WebEngineCallbacks* cb;
    gpointer data;
} Engine;

static Engine* engine_of(GtkWidget* view) {
    return view != NULL ? g_object_get_data(G_OBJECT(view), ENGINE_KEY) : NULL;
}

static void emit_changed(Engine* e, WebEngineChange what) {
    if (e->cb->changed != NULL) e->cb->changed(e->data, what);
}

static void on_title(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    emit_changed(data, WEB_ENGINE_TITLE);
}

static void on_uri(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    emit_changed(data, WEB_ENGINE_URI | WEB_ENGINE_TITLE);
}

static void on_progress(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    emit_changed(data, WEB_ENGINE_PROGRESS);
}

static void on_loading(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    emit_changed(data, WEB_ENGINE_LOADING);
}

static void on_history_changed(WebKitBackForwardList* list, WebKitBackForwardListItem* added,
                               gpointer removed, gpointer data) {
    (void)list;
    (void)added;
    (void)removed;
    emit_changed(data, WEB_ENGINE_LOADING);
}

static GtkWidget* on_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer data) {
    Engine* e = data;
    if (e->cb->create == NULL) return NULL;
    const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    return e->cb->create(e->data, GTK_WIDGET(view), uri);
}

static void on_ready_to_show(WebKitWebView* view, gpointer data) {
    (void)view;
    Engine* e = data;
    if (e->cb->ready_to_show != NULL) e->cb->ready_to_show(e->data);
}

static void on_view_close(WebKitWebView* view, gpointer data) {
    (void)view;
    Engine* e = data;
    if (e->cb->close != NULL) e->cb->close(e->data);
}

static void external(Engine* e, const gchar* uri, gboolean not_shown) {
    if (e->cb->external != NULL) e->cb->external(e->data, uri, not_shown);
}

static void notice(Engine* e, const gchar* message) {
    if (e->cb->notice != NULL) e->cb->notice(e->data, message);
}

static gboolean on_decide_policy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                 WebKitPolicyDecisionType type, gpointer data) {
    (void)view;
    Engine* e = data;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationAction* nav =
            webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(nav));
        if (web_address_shown(uri)) return FALSE;
        gchar* copy = g_strdup(uri);
        webkit_policy_decision_ignore(decision);
        external(e, copy, FALSE);
        g_free(copy);
        return TRUE;
    }
    if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return FALSE;
    /* A file the page cannot show (archive, package...): the default application */
    WebKitResponsePolicyDecision* rd = WEBKIT_RESPONSE_POLICY_DECISION(decision);
    if (webkit_response_policy_decision_is_mime_type_supported(rd) ||
        !webkit_response_policy_decision_is_main_frame_main_resource(rd)) {
        return FALSE;
    }
    gchar* uri = g_strdup(webkit_uri_request_get_uri(webkit_response_policy_decision_get_request(rd)));
    webkit_policy_decision_ignore(decision);
    external(e, uri, TRUE);
    g_free(uri);
    return TRUE;
}

/* Camera, microphone, location, notifications...: denied */
static gboolean on_permission(WebKitWebView* view, WebKitPermissionRequest* request, gpointer data) {
    (void)view;
    (void)data;
    webkit_permission_request_deny(request);
    return TRUE;
}

/* Upload a file: would open a file chooser window */
static gboolean on_file_chooser(WebKitWebView* view, WebKitFileChooserRequest* request, gpointer data) {
    (void)view;
    webkit_file_chooser_request_cancel(request);
    notice(data, _("Choosing a file to upload is not supported in browser tabs"));
    return TRUE;
}

/* Printing would open a print dialog window */
static gboolean on_print(WebKitWebView* view, WebKitPrintOperation* op, gpointer data) {
    (void)view;
    (void)op;
    notice(data, _("Printing is not supported in browser tabs"));
    return TRUE;
}

/* Fullscreen video and the like: the window stays as it is */
static gboolean on_fullscreen(WebKitWebView* view, gpointer data) {
    (void)view;
    (void)data;
    return TRUE;
}

gboolean web_engine_available(void) {
    return TRUE;
}

GtkWidget* web_engine_new(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data) {
    GtkWidget* view = related != NULL ? g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related, NULL)
                                      : webkit_web_view_new();
    Engine* e = g_new0(Engine, 1);
    e->cb = callbacks;
    e->data = data;
    g_object_set_data_full(G_OBJECT(view), ENGINE_KEY, e, g_free);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_title), e);
    g_signal_connect(view, "notify::uri", G_CALLBACK(on_uri), e);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_progress), e);
    g_signal_connect(view, "notify::is-loading", G_CALLBACK(on_loading), e);
    g_signal_connect(webkit_web_view_get_back_forward_list(WEBKIT_WEB_VIEW(view)), "changed",
                     G_CALLBACK(on_history_changed), e);
    g_signal_connect(view, "create", G_CALLBACK(on_create), e);
    g_signal_connect(view, "ready-to-show", G_CALLBACK(on_ready_to_show), e);
    g_signal_connect(view, "close", G_CALLBACK(on_view_close), e);
    g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), e);
    g_signal_connect(view, "permission-request", G_CALLBACK(on_permission), e);
    g_signal_connect(view, "run-file-chooser", G_CALLBACK(on_file_chooser), e);
    g_signal_connect(view, "print", G_CALLBACK(on_print), e);
    g_signal_connect(view, "enter-fullscreen", G_CALLBACK(on_fullscreen), e);
    return view;
}

void web_engine_load(GtkWidget* view, const gchar* uri) {
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), uri);
}

void web_engine_reload(GtkWidget* view) {
    webkit_web_view_reload(WEBKIT_WEB_VIEW(view));
}

void web_engine_stop(GtkWidget* view) {
    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(view));
}

void web_engine_go_back(GtkWidget* view) {
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(view));
}

void web_engine_go_forward(GtkWidget* view) {
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(view));
}

gboolean web_engine_can_go_back(GtkWidget* view) {
    return webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(view));
}

gboolean web_engine_can_go_forward(GtkWidget* view) {
    return webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(view));
}

gboolean web_engine_is_loading(GtkWidget* view) {
    return webkit_web_view_is_loading(WEBKIT_WEB_VIEW(view));
}

gdouble web_engine_progress(GtkWidget* view) {
    return webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(view));
}

const gchar* web_engine_uri(GtkWidget* view) {
    return engine_of(view) != NULL ? webkit_web_view_get_uri(WEBKIT_WEB_VIEW(view)) : NULL;
}

const gchar* web_engine_title(GtkWidget* view) {
    return engine_of(view) != NULL ? webkit_web_view_get_title(WEBKIT_WEB_VIEW(view)) : NULL;
}

#elif !defined(HAVE_WEBVIEW2)

/* No engine: browser.c checks web_engine_available() before anything else */

gboolean web_engine_available(void) {
    return FALSE;
}

GtkWidget* web_engine_new(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data) {
    (void)related;
    (void)callbacks;
    (void)data;
    return gtk_drawing_area_new();
}

void web_engine_load(GtkWidget* view, const gchar* uri) {
    (void)view;
    (void)uri;
}

void web_engine_reload(GtkWidget* view) {
    (void)view;
}

void web_engine_stop(GtkWidget* view) {
    (void)view;
}

void web_engine_go_back(GtkWidget* view) {
    (void)view;
}

void web_engine_go_forward(GtkWidget* view) {
    (void)view;
}

gboolean web_engine_can_go_back(GtkWidget* view) {
    (void)view;
    return FALSE;
}

gboolean web_engine_can_go_forward(GtkWidget* view) {
    (void)view;
    return FALSE;
}

gboolean web_engine_is_loading(GtkWidget* view) {
    (void)view;
    return FALSE;
}

gdouble web_engine_progress(GtkWidget* view) {
    (void)view;
    return 0.0;
}

const gchar* web_engine_uri(GtkWidget* view) {
    (void)view;
    return NULL;
}

const gchar* web_engine_title(GtkWidget* view) {
    (void)view;
    return NULL;
}

#endif

#ifndef WEB_ENGINE_H
#define WEB_ENGINE_H

/* The web view of a browser tab (browser.c), whatever engine shows it:
 * WebKitGTK 6.0 (HAVE_WEBKIT, web_engine.c) or Microsoft WebView2 on
 * Windows (HAVE_WEBVIEW2, web_engine_win.c). The view is a widget of the
 * tab; the engine tells the tab what changes through WebEngineCallbacks.
 * Built with neither, web_engine_available() is FALSE and nothing else is
 * called. */
#include <gtk/gtk.h>

/* What changed (the flags of WebEngineCallbacks.changed) */
typedef enum {
    WEB_ENGINE_TITLE = 1 << 0,
    WEB_ENGINE_URI = 1 << 1,
    WEB_ENGINE_LOADING = 1 << 2,  /* is_loading, and the back / forward history */
    WEB_ENGINE_PROGRESS = 1 << 3
} WebEngineChange;

typedef struct {
    void (*changed)(gpointer data, WebEngineChange what);
    /* A link or a script opens a new window (uri: its address, may be NULL):
     * the tab returns the view of a new tab made by web_engine_new(view, ...),
     * or NULL to drop it */
    GtkWidget* (*create)(gpointer data, GtkWidget* view, const gchar* uri);
    /* The new view returned by create can be shown */
    void (*ready_to_show)(gpointer data);
    /* The page asks to close its window (window.close()) */
    void (*close)(gpointer data);
    /* uri goes to the default application: a scheme the view does not show
     * (mailto:...), or (not_shown) a file it cannot show */
    void (*external)(gpointer data, const gchar* uri, gboolean not_shown);
    /* A message for the status line (something not supported in tabs) */
    void (*notice)(gpointer data, const gchar* message);
} WebEngineCallbacks;

/* TRUE when web views can be made (the engine is built in and, for
 * WebView2, its runtime is installed) */
gboolean web_engine_available(void);

/* A new view; related: the view whose page opened it (NULL for none).
 * callbacks stays valid as long as the view, data until the view is
 * destroyed. */
GtkWidget* web_engine_new(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data);

void web_engine_load(GtkWidget* view, const gchar* uri);
void web_engine_reload(GtkWidget* view);
void web_engine_stop(GtkWidget* view);
void web_engine_go_back(GtkWidget* view);
void web_engine_go_forward(GtkWidget* view);

gboolean web_engine_can_go_back(GtkWidget* view);
gboolean web_engine_can_go_forward(GtkWidget* view);
gboolean web_engine_is_loading(GtkWidget* view);
/* Load progress, 0.0 to 1.0 */
gdouble web_engine_progress(GtkWidget* view);
/* Address and title of the page (owned by the view, NULL when none) */
const gchar* web_engine_uri(GtkWidget* view);
const gchar* web_engine_title(GtkWidget* view);

#endif /* WEB_ENGINE_H */

#ifndef WEB_ENGINE_MODULE_H
#define WEB_ENGINE_MODULE_H

/* The WebKit engine as a module (docs/performance.md): web_engine_webkit.c
 * is built as WEB_ENGINE_MODULE_FILE, linked against WebKitGTK; the program
 * is not, and web_engine.c opens the module at the first web view. The
 * module exports one table of functions, WEB_ENGINE_MODULE_SYMBOL, the calls
 * of web_engine.h. A module of another version is refused (abi). */
#include "preview/web_engine.h"

/* Raise it whenever WebEngineOps or WebEngineCallbacks change */
#define WEB_ENGINE_MODULE_ABI 1
#define WEB_ENGINE_MODULE_FILE "lookinfiles-webkit.so"
#define WEB_ENGINE_MODULE_SYMBOL "web_engine_module_ops"

typedef struct {
    guint abi; /* WEB_ENGINE_MODULE_ABI */
    GtkWidget* (*new_view)(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data);
    void (*load)(GtkWidget* view, const gchar* uri);
    void (*reload)(GtkWidget* view);
    void (*stop)(GtkWidget* view);
    void (*go_back)(GtkWidget* view);
    void (*go_forward)(GtkWidget* view);
    gboolean (*can_go_back)(GtkWidget* view);
    gboolean (*can_go_forward)(GtkWidget* view);
    gboolean (*is_loading)(GtkWidget* view);
    gdouble (*progress)(GtkWidget* view);
    const gchar* (*uri)(GtkWidget* view);
    const gchar* (*title)(GtkWidget* view);
} WebEngineOps;

#endif /* WEB_ENGINE_MODULE_H */

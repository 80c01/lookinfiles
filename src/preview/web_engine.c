/* The web view of a browser tab (see web_engine.h). With WebKitGTK the
 * engine is a module opened at the first web view (web_engine_module.h,
 * web_engine_webkit.c): the program does not load WebKit before, about 30 MB
 * and 30 ms of start-up (docs/performance.md). The WebView2 engine is in
 * web_engine_win.c. */
#include "preview/web_engine.h"
#include "core/i18n.h"

#if defined(HAVE_WEBKIT)
#include "core/app_paths.h"
#include "core/module_path.h"
#include "core/profile.h"
#include "preview/web_engine_module.h"

#include <gmodule.h>

static const WebEngineOps* ops = NULL; /* the module's table, once opened */
static gboolean open_failed = FALSE;
static gchar* module_file = NULL;      /* found by web_engine_available() */
static gboolean module_searched = FALSE;

/* Where the module is, without opening it (menus ask often) */
static const gchar* find_module(void) {
    if (!module_searched) {
        module_searched = TRUE;
        gchar* exe_dir = module_path_exe_dir();
        module_file = module_path_find(exe_dir, MODULEDIR, WEB_ENGINE_MODULE_FILE);
        g_free(exe_dir);
    }
    return module_file;
}

/* The module's table, the module opened on the first call; NULL when it
 * cannot be (WebKitGTK not installed, another version of the module) */
static const WebEngineOps* engine_ops(void) {
    if (ops != NULL || open_failed) {
        return ops;
    }
    const gchar* path = find_module();
    gint64 start = profile_start();
    GModule* module = path != NULL ? g_module_open(path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL) : NULL;
    gpointer symbol = NULL;
    if (module == NULL || !g_module_symbol(module, WEB_ENGINE_MODULE_SYMBOL, &symbol) ||
        ((const WebEngineOps*)symbol)->abi != WEB_ENGINE_MODULE_ABI) {
        g_printerr("%s: browser tabs: cannot use %s: %s\n", APP_DIR_NAME,
                   path != NULL ? path : WEB_ENGINE_MODULE_FILE,
                   path == NULL     ? "not found (make builds it next to the program)"
                   : module == NULL ? g_module_error()
                                    : "another version of the module");
        if (module != NULL) {
            g_module_close(module);
        }
        open_failed = TRUE;
        return NULL;
    }
    g_module_make_resident(module); /* WebKit cannot be unloaded */
    ops = symbol;
    profile_done(start, "open %s", path);
    return ops;
}

gboolean web_engine_available(void) {
    return !open_failed && (ops != NULL || find_module() != NULL);
}

GtkWidget* web_engine_new(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data) {
    const WebEngineOps* o = engine_ops();
    if (o == NULL) {
        GtkWidget* label = gtk_label_new(_("Browser tabs could not load WebKitGTK 6.0 (see the terminal output)"));
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        return label;
    }
    return o->new_view(related, callbacks, data);
}

/* The other calls get views made by web_engine_new(): web views when the
 * module is open, a label when it could not be (then nothing to do) */
void web_engine_load(GtkWidget* view, const gchar* uri) {
    if (ops != NULL) ops->load(view, uri);
}

void web_engine_reload(GtkWidget* view) {
    if (ops != NULL) ops->reload(view);
}

void web_engine_stop(GtkWidget* view) {
    if (ops != NULL) ops->stop(view);
}

void web_engine_go_back(GtkWidget* view) {
    if (ops != NULL) ops->go_back(view);
}

void web_engine_go_forward(GtkWidget* view) {
    if (ops != NULL) ops->go_forward(view);
}

gboolean web_engine_can_go_back(GtkWidget* view) {
    return ops != NULL && ops->can_go_back(view);
}

gboolean web_engine_can_go_forward(GtkWidget* view) {
    return ops != NULL && ops->can_go_forward(view);
}

gboolean web_engine_is_loading(GtkWidget* view) {
    return ops != NULL && ops->is_loading(view);
}

gdouble web_engine_progress(GtkWidget* view) {
    return ops != NULL ? ops->progress(view) : 0.0;
}

const gchar* web_engine_uri(GtkWidget* view) {
    return ops != NULL ? ops->uri(view) : NULL;
}

const gchar* web_engine_title(GtkWidget* view) {
    return ops != NULL ? ops->title(view) : NULL;
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

/* The web view of a browser tab on Microsoft WebView2 (see web_engine_win.h) */
#ifdef HAVE_WEBVIEW2
#define COBJMACROS /* the C helpers of WebView2.h (ICoreWebView2_Navigate()...) */
#endif
#include "preview/web_engine_win.h"

#ifdef HAVE_WEBVIEW2
#include "preview/web_keys.h"
#include "preview/web_address.h"
#include "widgets/dialog.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include <math.h>
#include <string.h>
#include <windows.h>
#include <gdk/win32/gdkwin32.h>
#include <WebView2.h>

#define ENGINE_KEY "lookin-web-engine"

typedef struct {
    gint refs;
    GtkWidget* host; /* the view widget; NULL once it is destroyed */
    const WebEngineCallbacks* cb;
    gpointer data;
    ICoreWebView2Controller* controller;
    ICoreWebView2* web;
    HWND parent;      /* the GTK window */
    gboolean creating; /* the controller is being made */
    gboolean failed;   /* WebView2 could not start (said once) */
    gchar* pending;    /* address to load once the controller is made */
    gchar* uri;
    gchar* title;
    gboolean loading;
    gdouble progress;
    gboolean can_back;
    gboolean can_forward;
    gboolean shown;       /* the page's window is visible */
    RECT bounds;          /* its bounds, in the GTK window */
    RECT parent_rect;     /* where the GTK window was (popups of the page follow it) */
    gboolean native_focus; /* the page has the keyboard */
    guint tick;
    GtkWindow* window;
    gulong focus_handler;
} Engine;

static Engine* engine_ref(Engine* e) {
    e->refs++;
    return e;
}

static void engine_unref(gpointer data) {
    Engine* e = data;
    if (--e->refs > 0) return;
    g_free(e->pending);
    g_free(e->uri);
    g_free(e->title);
    g_free(e);
}

static Engine* engine_of(GtkWidget* view) {
    return view != NULL ? g_object_get_data(G_OBJECT(view), ENGINE_KEY) : NULL;
}

static void emit_changed(Engine* e, WebEngineChange what) {
    if (e->host != NULL && e->cb->changed != NULL) e->cb->changed(e->data, what);
}

static void notice(Engine* e, const gchar* message) {
    if (e->host != NULL && e->cb->notice != NULL) e->cb->notice(e->data, message);
}

static void engine_failed(Engine* e) {
    e->creating = FALSE;
    if (e->failed) return;
    e->failed = TRUE;
    notice(e, _("The web view could not start (Microsoft Edge WebView2 Runtime)"));
}

/* ---- COM handlers -------------------------------------------------------
 * Every WebView2 handler interface is IUnknown plus Invoke(This, a, b):
 * events get (sender, args), completions (HRESULT, result). One object
 * type serves them all, answering QueryInterface for its own interface. */

typedef struct Handler Handler;
typedef HRESULT (*EventFn)(Engine* e, void* sender, void* args);
typedef HRESULT (*DoneFn)(Engine* e, HRESULT result, void* object);

typedef struct {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(Handler* self, REFIID riid, void** object);
    ULONG(STDMETHODCALLTYPE* AddRef)(Handler* self);
    ULONG(STDMETHODCALLTYPE* Release)(Handler* self);
    HRESULT(STDMETHODCALLTYPE* Invoke)(Handler* self, void* sender, void* args);
} EventVtbl;

typedef struct {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(Handler* self, REFIID riid, void** object);
    ULONG(STDMETHODCALLTYPE* AddRef)(Handler* self);
    ULONG(STDMETHODCALLTYPE* Release)(Handler* self);
    HRESULT(STDMETHODCALLTYPE* Invoke)(Handler* self, HRESULT result, void* object);
} DoneVtbl;

struct Handler {
    const void* vtbl;
    LONG refs;
    const IID* iid;
    Engine* engine; /* referenced; NULL for the environment */
    EventFn event;
    DoneFn done;
};

/* The interfaces' IIDs (from WebView2.h, which only declares them) */
static const IID IID_Unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_EnvironmentCompleted = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
static const IID IID_ControllerCompleted = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
static const IID IID_NavigationStarting = {0x9adbe429, 0xf36d, 0x432b, {0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3}};
static const IID IID_ContentLoading = {0x364471e7, 0xf2be, 0x4910, {0xbd, 0xba, 0xd7, 0x20, 0x77, 0xd5, 0x1c, 0x4b}};
static const IID IID_SourceChanged = {0x3c067f9f, 0x5388, 0x4772, {0x8b, 0x48, 0x79, 0xf7, 0xef, 0x1a, 0xb3, 0x7c}};
static const IID IID_HistoryChanged = {0xc79a420c, 0xefd9, 0x4058, {0x92, 0x95, 0x3e, 0x8b, 0x4b, 0xca, 0xb6, 0x45}};
static const IID IID_NavigationCompleted = {0xd33a35bf, 0x1c49, 0x4f98, {0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c}};
static const IID IID_PermissionRequested = {0x15e1c6a3, 0xc72a, 0x4df3, {0x91, 0xd7, 0xd0, 0x97, 0xfb, 0xec, 0x6b, 0xfd}};
static const IID IID_NewWindowRequested = {0xd4c185fe, 0xc81c, 0x4989, {0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51}};
static const IID IID_DocumentTitleChanged = {0xf5f2b923, 0x953e, 0x4042, {0x9f, 0x95, 0xf3, 0xa1, 0x18, 0xe1, 0xaf, 0xd4}};
static const IID IID_WindowCloseRequested = {0x5c19e9e0, 0x092f, 0x486b, {0xaf, 0xfa, 0xca, 0x82, 0x31, 0x91, 0x30, 0x39}};
static const IID IID_AcceleratorKeyPressed = {0xb29c7e28, 0xfa79, 0x41a8, {0x8e, 0x44, 0x65, 0x81, 0x1c, 0x76, 0xdc, 0xb2}};
static const IID IID_FocusChanged = {0x05ea24bd, 0x6452, 0x4926, {0x90, 0x14, 0x4b, 0x82, 0xb4, 0x98, 0x13, 0x5d}};
static const IID IID_DownloadStarting = {0xefedc989, 0xc396, 0x41ca, {0x83, 0xf7, 0x07, 0xf8, 0x45, 0xa5, 0x57, 0x24}};
static const IID IID_CoreWebView2_4 = {0x20d02d59, 0x6df2, 0x42dc, {0xbd, 0x06, 0xf9, 0x8a, 0x69, 0x4b, 0x13, 0x02}};

static ULONG STDMETHODCALLTYPE handler_add_ref(Handler* self) {
    return (ULONG)InterlockedIncrement(&self->refs);
}

static ULONG STDMETHODCALLTYPE handler_release(Handler* self) {
    LONG refs = InterlockedDecrement(&self->refs);
    if (refs == 0) {
        if (self->engine != NULL) engine_unref(self->engine);
        g_free(self);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE handler_query(Handler* self, REFIID riid, void** object) {
    if (memcmp(riid, &IID_Unknown, sizeof(IID)) == 0 || memcmp(riid, self->iid, sizeof(IID)) == 0) {
        *object = self;
        handler_add_ref(self);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

/* Events after the view is destroyed are dropped */
static HRESULT STDMETHODCALLTYPE event_invoke(Handler* self, void* sender, void* args) {
    if (self->engine == NULL || self->engine->host == NULL) return S_OK;
    return self->event(self->engine, sender, args);
}

static HRESULT STDMETHODCALLTYPE done_invoke(Handler* self, HRESULT result, void* object) {
    return self->done(self->engine, result, object);
}

static const EventVtbl event_vtbl = {handler_query, handler_add_ref, handler_release, event_invoke};
static const DoneVtbl done_vtbl = {handler_query, handler_add_ref, handler_release, done_invoke};

static Handler* handler_new(const IID* iid, Engine* e) {
    Handler* h = g_new0(Handler, 1);
    h->refs = 1;
    h->iid = iid;
    h->engine = e != NULL ? engine_ref(e) : NULL;
    return h;
}

static Handler* event_handler(const IID* iid, Engine* e, EventFn fn) {
    Handler* h = handler_new(iid, e);
    h->vtbl = &event_vtbl;
    h->event = fn;
    return h;
}

static Handler* done_handler(const IID* iid, Engine* e, DoneFn fn) {
    Handler* h = handler_new(iid, e);
    h->vtbl = &done_vtbl;
    h->done = fn;
    return h;
}

/* ---- Loader and environment --------------------------------------------- */

typedef HRESULT(STDAPICALLTYPE* CreateEnvironmentFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
typedef HRESULT(STDAPICALLTYPE* VersionFn)(PCWSTR, LPWSTR*);

/* A function of WebView2Loader.dll, loaded from the executable's folder */
static void (*loader_function(const char* name))(void) {
    static HMODULE module = NULL;
    static gboolean tried = FALSE;
    if (!tried) {
        tried = TRUE;
        module = LoadLibraryExW(L"WebView2Loader.dll", NULL,
                                LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return module != NULL ? (void (*)(void))GetProcAddress(module, name) : NULL;
}

static ICoreWebView2Environment* environment = NULL;
static gboolean environment_failed = FALSE;
static gboolean environment_creating = FALSE;
static GPtrArray* environment_waiting = NULL; /* engines (referenced) */

static void controller_create(Engine* e);

static void environment_flush(void) {
    GPtrArray* waiting = environment_waiting;
    environment_waiting = NULL;
    if (waiting == NULL) return;
    for (guint i = 0; i < waiting->len; i++) {
        Engine* e = g_ptr_array_index(waiting, i);
        if (environment != NULL) {
            controller_create(e);
        } else {
            engine_failed(e);
        }
    }
    g_ptr_array_unref(waiting);
}

static HRESULT environment_done(Engine* unused, HRESULT result, void* object) {
    (void)unused;
    if (!environment_creating) return S_OK;
    environment_creating = FALSE;
    if (SUCCEEDED(result) && object != NULL) {
        environment = object;
        ICoreWebView2Environment_AddRef(environment);
    } else {
        environment_failed = TRUE;
    }
    environment_flush();
    return S_OK;
}

/* e gets its controller once the environment (one for every view) is made */
static void environment_request(Engine* e) {
    if (environment != NULL) {
        controller_create(e);
        return;
    }
    if (environment_failed) {
        engine_failed(e);
        return;
    }
    if (environment_waiting == NULL) environment_waiting = g_ptr_array_new_with_free_func(engine_unref);
    g_ptr_array_add(environment_waiting, engine_ref(e));
    if (environment_creating) return;
    environment_creating = TRUE;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); /* already done by GTK: S_FALSE */
    CreateEnvironmentFn create = (CreateEnvironmentFn)loader_function("CreateCoreWebView2EnvironmentWithOptions");
    gchar* cache = app_cache_dir();
    gchar* dir = g_build_filename(cache, "webview2", NULL);
    gunichar2* wdir = g_utf8_to_utf16(dir, -1, NULL, NULL, NULL);
    Handler* h = done_handler(&IID_EnvironmentCompleted, NULL, environment_done);
    HRESULT hr = create != NULL ? create(NULL, (PCWSTR)wdir, NULL,
                                         (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*)h)
                                : E_FAIL;
    handler_release(h);
    g_free(wdir);
    g_free(dir);
    g_free(cache);
    if (FAILED(hr) && environment_creating) {
        environment_creating = FALSE;
        environment_failed = TRUE;
        environment_flush();
    }
}

gboolean web_engine_available(void) {
    static gint state = -1;
    if (state < 0) {
        VersionFn version_of = (VersionFn)loader_function("GetAvailableCoreWebView2BrowserVersionString");
        LPWSTR version = NULL;
        state = version_of != NULL && SUCCEEDED(version_of(NULL, &version)) && version != NULL;
        if (version != NULL) CoTaskMemFree(version);
    }
    return state == 1;
}

/* ---- Page state ----------------------------------------------------------- */

/* A string of WebView2 (freed) as UTF-8, or NULL */
static gchar* take_string(LPWSTR w) {
    if (w == NULL) return NULL;
    gchar* s = g_utf16_to_utf8((const gunichar2*)w, -1, NULL, NULL, NULL);
    CoTaskMemFree(w);
    return s;
}

static void navigate(Engine* e, const gchar* uri) {
    gunichar2* w = g_utf8_to_utf16(uri, -1, NULL, NULL, NULL);
    if (w == NULL) return;
    ICoreWebView2_Navigate(e->web, (LPCWSTR)w);
    g_free(w);
}

static void refresh_uri(Engine* e) {
    LPWSTR w = NULL;
    if (SUCCEEDED(ICoreWebView2_get_Source(e->web, &w))) {
        g_free(e->uri);
        e->uri = take_string(w);
    }
}

static void refresh_title(Engine* e) {
    LPWSTR w = NULL;
    if (SUCCEEDED(ICoreWebView2_get_DocumentTitle(e->web, &w))) {
        g_free(e->title);
        e->title = take_string(w);
    }
}

static void refresh_history(Engine* e) {
    BOOL back = FALSE;
    BOOL forward = FALSE;
    ICoreWebView2_get_CanGoBack(e->web, &back);
    ICoreWebView2_get_CanGoForward(e->web, &forward);
    e->can_back = back;
    e->can_forward = forward;
}

/* ---- Events of the page ------------------------------------------------- */

static HRESULT on_navigation_starting(Engine* e, void* sender, void* data) {
    (void)sender;
    ICoreWebView2NavigationStartingEventArgs* args = data;
    LPWSTR w = NULL;
    ICoreWebView2NavigationStartingEventArgs_get_Uri(args, &w);
    gchar* uri = take_string(w);
    if (uri != NULL && !web_address_shown(uri)) {
        ICoreWebView2NavigationStartingEventArgs_put_Cancel(args, TRUE);
        if (e->cb->external != NULL) e->cb->external(e->data, uri, FALSE);
    } else {
        e->loading = TRUE;
        e->progress = 0.1;
        emit_changed(e, WEB_ENGINE_LOADING | WEB_ENGINE_PROGRESS);
    }
    g_free(uri);
    return S_OK;
}

static HRESULT on_content_loading(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    e->progress = 0.6;
    emit_changed(e, WEB_ENGINE_PROGRESS);
    return S_OK;
}

static HRESULT on_source_changed(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    refresh_uri(e);
    refresh_title(e);
    emit_changed(e, WEB_ENGINE_URI | WEB_ENGINE_TITLE);
    return S_OK;
}

static HRESULT on_history_changed(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    refresh_history(e);
    emit_changed(e, WEB_ENGINE_LOADING);
    return S_OK;
}

static HRESULT on_navigation_completed(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    e->loading = FALSE;
    e->progress = 1.0;
    refresh_history(e);
    refresh_title(e);
    emit_changed(e, WEB_ENGINE_LOADING | WEB_ENGINE_PROGRESS | WEB_ENGINE_TITLE);
    return S_OK;
}

static HRESULT on_title_changed(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    refresh_title(e);
    emit_changed(e, WEB_ENGINE_TITLE);
    return S_OK;
}

/* Camera, microphone, location, notifications...: denied */
static HRESULT on_permission(Engine* e, void* sender, void* args) {
    (void)e;
    (void)sender;
    ICoreWebView2PermissionRequestedEventArgs_put_State((ICoreWebView2PermissionRequestedEventArgs*)args,
                                                        COREWEBVIEW2_PERMISSION_STATE_DENY);
    return S_OK;
}

/* A link or script opens a new window: a new tab loading its address */
static HRESULT on_new_window(Engine* e, void* sender, void* data) {
    (void)sender;
    ICoreWebView2NewWindowRequestedEventArgs* args = data;
    LPWSTR w = NULL;
    ICoreWebView2NewWindowRequestedEventArgs_get_Uri(args, &w);
    gchar* uri = take_string(w);
    ICoreWebView2NewWindowRequestedEventArgs_put_Handled(args, TRUE);
    GtkWidget* view = uri != NULL && e->cb->create != NULL ? e->cb->create(e->data, e->host, uri) : NULL;
    Engine* ne = engine_of(view);
    if (ne != NULL) {
        web_engine_load(view, uri);
        if (ne->cb->ready_to_show != NULL) ne->cb->ready_to_show(ne->data);
    }
    g_free(uri);
    return S_OK;
}

static HRESULT on_close_requested(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    if (e->cb->close != NULL) e->cb->close(e->data);
    return S_OK;
}

/* A file the page cannot show (archive, package...): the default application */
static HRESULT on_download(Engine* e, void* sender, void* data) {
    (void)sender;
    ICoreWebView2DownloadStartingEventArgs* args = data;
    ICoreWebView2DownloadOperation* op = NULL;
    gchar* uri = NULL;
    if (SUCCEEDED(ICoreWebView2DownloadStartingEventArgs_get_DownloadOperation(args, &op)) && op != NULL) {
        LPWSTR w = NULL;
        ICoreWebView2DownloadOperation_get_Uri(op, &w);
        uri = take_string(w);
        ICoreWebView2DownloadOperation_Release(op);
    }
    ICoreWebView2DownloadStartingEventArgs_put_Cancel(args, TRUE);
    if (uri != NULL && e->cb->external != NULL) e->cb->external(e->data, uri, TRUE);
    g_free(uri);
    return S_OK;
}

/* ---- Keyboard and focus ------------------------------------------------- */

/* The GDK key of a Windows virtual key, 0 for the keys the page always keeps */
static guint vk_keyval(UINT vk, gboolean shift) {
    if (vk >= 'A' && vk <= 'Z') return (shift ? GDK_KEY_A : GDK_KEY_a) + (vk - 'A');
    if (vk >= '0' && vk <= '9') return GDK_KEY_0 + (vk - '0');
    if (vk >= VK_F1 && vk <= VK_F24) return GDK_KEY_F1 + (vk - VK_F1);
    switch (vk) {
    case VK_TAB: return shift ? GDK_KEY_ISO_Left_Tab : GDK_KEY_Tab;
    case VK_PRIOR: return GDK_KEY_Page_Up;
    case VK_NEXT: return GDK_KEY_Page_Down;
    case VK_LEFT: return GDK_KEY_Left;
    case VK_RIGHT: return GDK_KEY_Right;
    case VK_UP: return GDK_KEY_Up;
    case VK_DOWN: return GDK_KEY_Down;
    case VK_HOME: return GDK_KEY_Home;
    case VK_END: return GDK_KEY_End;
    case VK_INSERT: return GDK_KEY_Insert;
    case VK_DELETE: return GDK_KEY_Delete;
    case VK_BACK: return GDK_KEY_BackSpace;
    case VK_RETURN: return GDK_KEY_Return;
    case VK_ESCAPE: return GDK_KEY_Escape;
    case VK_OEM_PLUS: return GDK_KEY_equal;
    case VK_OEM_MINUS: return GDK_KEY_minus;
    case VK_OEM_COMMA: return GDK_KEY_comma;
    case VK_ADD: return GDK_KEY_KP_Add;
    case VK_SUBTRACT: return GDK_KEY_KP_Subtract;
    default: return 0;
    }
}

/* After a key given to GTK: the page gets the keyboard back if the view
 * still has the GTK focus (F5; not Ctrl+L, which went to the address bar) */
static gboolean refocus_idle(gpointer data) {
    Engine* e = data;
    if (e->host != NULL && e->controller != NULL && !e->native_focus && gtk_widget_is_focus(e->host)) {
        ICoreWebView2Controller_MoveFocus(e->controller, COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
    engine_unref(e);
    return G_SOURCE_REMOVE;
}

static HRESULT on_accelerator(Engine* e, void* sender, void* data) {
    (void)sender;
    ICoreWebView2AcceleratorKeyPressedEventArgs* args = data;
    COREWEBVIEW2_KEY_EVENT_KIND kind;
    UINT vk = 0;
    INT lparam = 0;
    if (FAILED(ICoreWebView2AcceleratorKeyPressedEventArgs_get_KeyEventKind(args, &kind)) ||
        FAILED(ICoreWebView2AcceleratorKeyPressedEventArgs_get_VirtualKey(args, &vk)) ||
        FAILED(ICoreWebView2AcceleratorKeyPressedEventArgs_get_KeyEventLParam(args, &lparam))) {
        return S_OK;
    }
    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
        return S_OK;
    }
    if (GetKeyState(VK_RMENU) < 0) return S_OK; /* AltGr: a character typed in the page */
    gboolean shift = GetKeyState(VK_SHIFT) < 0;
    GdkModifierType mods = 0;
    if (GetKeyState(VK_CONTROL) < 0) mods |= GDK_CONTROL_MASK;
    if (GetKeyState(VK_MENU) < 0) mods |= GDK_ALT_MASK;
    if (shift) mods |= GDK_SHIFT_MASK;
    if (!web_keys_for_app(vk_keyval(vk, shift), mods)) return S_OK;
    /* The key goes to the GTK window, as if the page had not been there */
    ICoreWebView2AcceleratorKeyPressedEventArgs_put_Handled(args, TRUE);
    SetFocus(e->parent);
    PostMessageW(e->parent, kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN ? WM_SYSKEYDOWN : WM_KEYDOWN,
                 (WPARAM)vk, (LPARAM)lparam);
    g_idle_add(refocus_idle, engine_ref(e));
    return S_OK;
}

/* The page took the keyboard (a click in it): the view takes the GTK focus */
static HRESULT on_got_focus(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    e->native_focus = TRUE;
    if (!gtk_widget_is_focus(e->host)) gtk_widget_grab_focus(e->host);
    return S_OK;
}

static HRESULT on_lost_focus(Engine* e, void* sender, void* args) {
    (void)sender;
    (void)args;
    e->native_focus = FALSE;
    return S_OK;
}

/* The view gets the GTK focus: the page gets the keyboard */
static void on_focus_enter(GtkEventControllerFocus* controller, gpointer data) {
    (void)controller;
    Engine* e = data;
    if (e->controller != NULL && !e->native_focus) {
        ICoreWebView2Controller_MoveFocus(e->controller, COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

/* The GTK focus went elsewhere (the address bar, another panel): the GTK
 * window takes the keyboard back from the page */
static void on_window_focus(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    Engine* e = data;
    if (e->native_focus && gtk_window_get_focus(GTK_WINDOW(obj)) != e->host && e->parent != NULL) {
        SetFocus(e->parent);
    }
}

/* ---- Placement ---------------------------------------------------------- */

/* The page's window on the view's bounds, shown while the view is mapped
 * and no dialog covers the window */
static void sync_bounds(Engine* e) {
    if (e->controller == NULL || e->host == NULL) return;
    GtkNative* native = gtk_widget_get_native(e->host);
    GtkRoot* root = gtk_widget_get_root(e->host);
    gboolean show = native != NULL && gtk_widget_get_mapped(e->host) &&
                    !(GTK_IS_WINDOW(root) && app_dialog_is_open(GTK_WINDOW(root)));
    graphene_rect_t r;
    if (show && !gtk_widget_compute_bounds(e->host, GTK_WIDGET(native), &r)) show = FALSE;
    if (show) {
        GdkSurface* surface = gtk_native_get_surface(native);
        double sx = 0.0;
        double sy = 0.0;
        gtk_native_get_surface_transform(native, &sx, &sy);
        double scale = surface != NULL ? gdk_surface_get_scale(surface) : 1.0;
        RECT b;
        b.left = (LONG)lround((r.origin.x + sx) * scale);
        b.top = (LONG)lround((r.origin.y + sy) * scale);
        b.right = (LONG)lround((r.origin.x + r.size.width + sx) * scale);
        b.bottom = (LONG)lround((r.origin.y + r.size.height + sy) * scale);
        if (b.right <= b.left || b.bottom <= b.top) {
            show = FALSE;
        } else if (memcmp(&b, &e->bounds, sizeof b) != 0) {
            e->bounds = b;
            ICoreWebView2Controller_put_Bounds(e->controller, b);
        }
    }
    if (show != e->shown) {
        e->shown = show;
        ICoreWebView2Controller_put_IsVisible(e->controller, show);
    }
    RECT pr;
    if (e->parent != NULL && GetWindowRect(e->parent, &pr) && memcmp(&pr, &e->parent_rect, sizeof pr) != 0) {
        e->parent_rect = pr;
        ICoreWebView2Controller_NotifyParentWindowPositionChanged(e->controller);
    }
}

/* Each frame while mapped: GTK 4 tells no widget that an ancestor moved */
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)widget;
    (void)clock;
    sync_bounds(data);
    return G_SOURCE_CONTINUE;
}

/* ---- The controller ----------------------------------------------------- */

static void add_event(Engine* e, const IID* iid, EventFn fn,
                      HRESULT (*add)(Engine* e, Handler* h, EventRegistrationToken* token)) {
    EventRegistrationToken token;
    Handler* h = event_handler(iid, e, fn);
    add(e, h, &token);
    handler_release(h);
}

#define ADDER(name, iface, target, method)                                              \
    static HRESULT name(Engine* e, Handler* h, EventRegistrationToken* token) {          \
        return method(target, (iface*)h, token);                                        \
    }
ADDER(add_navigation_starting, ICoreWebView2NavigationStartingEventHandler, e->web, ICoreWebView2_add_NavigationStarting)
ADDER(add_content_loading, ICoreWebView2ContentLoadingEventHandler, e->web, ICoreWebView2_add_ContentLoading)
ADDER(add_source_changed, ICoreWebView2SourceChangedEventHandler, e->web, ICoreWebView2_add_SourceChanged)
ADDER(add_history_changed, ICoreWebView2HistoryChangedEventHandler, e->web, ICoreWebView2_add_HistoryChanged)
ADDER(add_navigation_completed, ICoreWebView2NavigationCompletedEventHandler, e->web, ICoreWebView2_add_NavigationCompleted)
ADDER(add_permission, ICoreWebView2PermissionRequestedEventHandler, e->web, ICoreWebView2_add_PermissionRequested)
ADDER(add_new_window, ICoreWebView2NewWindowRequestedEventHandler, e->web, ICoreWebView2_add_NewWindowRequested)
ADDER(add_title_changed, ICoreWebView2DocumentTitleChangedEventHandler, e->web, ICoreWebView2_add_DocumentTitleChanged)
ADDER(add_close_requested, ICoreWebView2WindowCloseRequestedEventHandler, e->web, ICoreWebView2_add_WindowCloseRequested)
ADDER(add_accelerator, ICoreWebView2AcceleratorKeyPressedEventHandler, e->controller, ICoreWebView2Controller_add_AcceleratorKeyPressed)
ADDER(add_got_focus, ICoreWebView2FocusChangedEventHandler, e->controller, ICoreWebView2Controller_add_GotFocus)
ADDER(add_lost_focus, ICoreWebView2FocusChangedEventHandler, e->controller, ICoreWebView2Controller_add_LostFocus)
#undef ADDER

/* Downloads (ICoreWebView2_4, runtimes from 2021 on) */
static void add_download(Engine* e) {
    ICoreWebView2_4* web4 = NULL;
    if (FAILED(ICoreWebView2_QueryInterface(e->web, &IID_CoreWebView2_4, (void**)&web4)) || web4 == NULL) return;
    EventRegistrationToken token;
    Handler* h = event_handler(&IID_DownloadStarting, e, on_download);
    ICoreWebView2_4_add_DownloadStarting(web4, (ICoreWebView2DownloadStartingEventHandler*)h, &token);
    handler_release(h);
    ICoreWebView2_4_Release(web4);
}

static void controller_close(Engine* e) {
    if (e->web != NULL) {
        ICoreWebView2_Release(e->web);
        e->web = NULL;
    }
    if (e->controller != NULL) {
        ICoreWebView2Controller_Close(e->controller);
        ICoreWebView2Controller_Release(e->controller);
        e->controller = NULL;
    }
}

static HRESULT controller_done(Engine* e, HRESULT result, void* object) {
    e->creating = FALSE;
    if (FAILED(result) || object == NULL) {
        engine_failed(e);
        return S_OK;
    }
    ICoreWebView2Controller* controller = object;
    if (e->host == NULL) {
        ICoreWebView2Controller_Close(controller);
        return S_OK;
    }
    ICoreWebView2Controller_AddRef(controller);
    e->controller = controller;
    if (FAILED(ICoreWebView2Controller_get_CoreWebView2(controller, &e->web)) || e->web == NULL) {
        e->web = NULL;
        controller_close(e);
        engine_failed(e);
        return S_OK;
    }
    ICoreWebView2Settings* settings = NULL;
    if (SUCCEEDED(ICoreWebView2_get_Settings(e->web, &settings)) && settings != NULL) {
        ICoreWebView2Settings_put_AreDevToolsEnabled(settings, FALSE); /* a separate window */
        ICoreWebView2Settings_Release(settings);
    }
    add_event(e, &IID_NavigationStarting, on_navigation_starting, add_navigation_starting);
    add_event(e, &IID_ContentLoading, on_content_loading, add_content_loading);
    add_event(e, &IID_SourceChanged, on_source_changed, add_source_changed);
    add_event(e, &IID_HistoryChanged, on_history_changed, add_history_changed);
    add_event(e, &IID_NavigationCompleted, on_navigation_completed, add_navigation_completed);
    add_event(e, &IID_PermissionRequested, on_permission, add_permission);
    add_event(e, &IID_NewWindowRequested, on_new_window, add_new_window);
    add_event(e, &IID_DocumentTitleChanged, on_title_changed, add_title_changed);
    add_event(e, &IID_WindowCloseRequested, on_close_requested, add_close_requested);
    add_event(e, &IID_AcceleratorKeyPressed, on_accelerator, add_accelerator);
    add_event(e, &IID_FocusChanged, on_got_focus, add_got_focus);
    add_event(e, &IID_FocusChanged, on_lost_focus, add_lost_focus);
    add_download(e);
    e->shown = TRUE; /* visible when made: sync_bounds() hides it if needed */
    memset(&e->bounds, 0, sizeof e->bounds);
    sync_bounds(e);
    if (gtk_widget_is_focus(e->host)) {
        ICoreWebView2Controller_MoveFocus(controller, COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
    if (e->pending != NULL) {
        gchar* uri = e->pending;
        e->pending = NULL;
        navigate(e, uri);
        g_free(uri);
    }
    return S_OK;
}

static void controller_create(Engine* e) {
    if (e->host == NULL || e->parent == NULL || environment == NULL) {
        e->creating = FALSE;
        return;
    }
    Handler* h = done_handler(&IID_ControllerCompleted, e, controller_done);
    HRESULT hr = ICoreWebView2Environment_CreateCoreWebView2Controller(
        environment, e->parent, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*)h);
    handler_release(h);
    if (FAILED(hr)) engine_failed(e);
}

/* ---- The view widget ---------------------------------------------------- */

static void on_realize(GtkWidget* widget, gpointer data) {
    Engine* e = data;
    GtkNative* native = gtk_widget_get_native(widget);
    GdkSurface* surface = native != NULL ? gtk_native_get_surface(native) : NULL;
    HWND hwnd = surface != NULL ? gdk_win32_surface_get_handle(surface) : NULL;
    if (hwnd == NULL) return;
    if (e->controller != NULL && hwnd != e->parent) {
        ICoreWebView2Controller_put_ParentWindow(e->controller, hwnd);
    }
    e->parent = hwnd;
    GtkRoot* root = gtk_widget_get_root(widget);
    if (GTK_IS_WINDOW(root) && e->window == NULL) {
        e->window = GTK_WINDOW(root);
        e->focus_handler = g_signal_connect(root, "notify::focus-widget", G_CALLBACK(on_window_focus), e);
    }
    if (e->controller == NULL && !e->creating && !e->failed) {
        e->creating = TRUE;
        environment_request(e);
    }
}

static void forget_window(Engine* e) {
    if (e->window != NULL) {
        g_signal_handler_disconnect(e->window, e->focus_handler);
        e->window = NULL;
        e->focus_handler = 0;
    }
}

/* Out of the window (another workspace, being moved): hidden, the page kept */
static void on_unrealize(GtkWidget* widget, gpointer data) {
    (void)widget;
    Engine* e = data;
    forget_window(e);
    if (e->controller != NULL && e->shown) {
        e->shown = FALSE;
        ICoreWebView2Controller_put_IsVisible(e->controller, FALSE);
    }
    if (e->native_focus && e->parent != NULL) SetFocus(e->parent);
}

static void on_map(GtkWidget* widget, gpointer data) {
    Engine* e = data;
    if (e->tick == 0) e->tick = gtk_widget_add_tick_callback(widget, on_tick, e, NULL);
    sync_bounds(e);
}

static void on_unmap(GtkWidget* widget, gpointer data) {
    Engine* e = data;
    if (e->tick != 0) {
        gtk_widget_remove_tick_callback(widget, e->tick);
        e->tick = 0;
    }
    if (e->controller != NULL && e->shown) {
        e->shown = FALSE;
        ICoreWebView2Controller_put_IsVisible(e->controller, FALSE);
    }
    if (e->native_focus && e->parent != NULL) SetFocus(e->parent);
}

static void on_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    Engine* e = data;
    forget_window(e);
    if (e->native_focus && e->parent != NULL) SetFocus(e->parent);
    e->host = NULL;
    controller_close(e);
}

GtkWidget* web_engine_new(GtkWidget* related, const WebEngineCallbacks* callbacks, gpointer data) {
    (void)related; /* a new window gets its address, not its opener */
    GtkWidget* host = gtk_drawing_area_new();
    gtk_widget_set_focusable(host, TRUE);
    Engine* e = g_new0(Engine, 1);
    e->refs = 1;
    e->host = host;
    e->cb = callbacks;
    e->data = data;
    g_object_set_data_full(G_OBJECT(host), ENGINE_KEY, e, engine_unref);
    g_signal_connect(host, "realize", G_CALLBACK(on_realize), e);
    g_signal_connect(host, "unrealize", G_CALLBACK(on_unrealize), e);
    g_signal_connect(host, "map", G_CALLBACK(on_map), e);
    g_signal_connect(host, "unmap", G_CALLBACK(on_unmap), e);
    g_signal_connect(host, "destroy", G_CALLBACK(on_destroy), e);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), e);
    gtk_widget_add_controller(host, focus);
    return host;
}

void web_engine_load(GtkWidget* view, const gchar* uri) {
    Engine* e = engine_of(view);
    if (e == NULL || uri == NULL) return;
    if (e->web != NULL) {
        navigate(e, uri);
        return;
    }
    /* Not made yet (a tab of a hidden workspace is made when shown): the
     * address is kept, and saved with the session meanwhile */
    g_free(e->pending);
    e->pending = g_strdup(uri);
    g_free(e->uri);
    e->uri = g_strdup(uri);
    emit_changed(e, WEB_ENGINE_URI | WEB_ENGINE_TITLE);
}

void web_engine_reload(GtkWidget* view) {
    Engine* e = engine_of(view);
    if (e != NULL && e->web != NULL) ICoreWebView2_Reload(e->web);
}

void web_engine_stop(GtkWidget* view) {
    Engine* e = engine_of(view);
    if (e != NULL && e->web != NULL) ICoreWebView2_Stop(e->web);
}

void web_engine_go_back(GtkWidget* view) {
    Engine* e = engine_of(view);
    if (e != NULL && e->web != NULL) ICoreWebView2_GoBack(e->web);
}

void web_engine_go_forward(GtkWidget* view) {
    Engine* e = engine_of(view);
    if (e != NULL && e->web != NULL) ICoreWebView2_GoForward(e->web);
}

gboolean web_engine_can_go_back(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL && e->can_back;
}

gboolean web_engine_can_go_forward(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL && e->can_forward;
}

gboolean web_engine_is_loading(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL && e->loading;
}

gdouble web_engine_progress(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL ? e->progress : 0.0;
}

const gchar* web_engine_uri(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL ? e->uri : NULL;
}

const gchar* web_engine_title(GtkWidget* view) {
    Engine* e = engine_of(view);
    return e != NULL ? e->title : NULL;
}

#endif /* HAVE_WEBVIEW2 */

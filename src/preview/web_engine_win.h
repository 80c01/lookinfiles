#ifndef WEB_ENGINE_WIN_H
#define WEB_ENGINE_WIN_H

/* web_engine.h on Windows, with Microsoft WebView2 (HAVE_WEBVIEW2: the SDK's
 * WebView2.h at build time, packaging/windows/fetch_webview2.sh; at run
 * time WebView2Loader.dll next to the executable and the WebView2 Runtime,
 * part of Windows 10 and 11).
 *
 * The page is a native window over the view widget (a GtkDrawingArea that
 * draws nothing), kept on its bounds while the widget is mapped, hidden
 * while an AppDialog is open (it would cover it). Keys go to the page,
 * except those web_keys_for_app() gives to the application: they are posted
 * to the GTK window, so the tab and window shortcuts (Ctrl+Q included) work
 * as with WebKitGTK. The profile is kept in app_cache_dir()/webview2. */
#include "preview/web_engine.h"

#endif /* WEB_ENGINE_WIN_H */

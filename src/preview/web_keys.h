#ifndef WEB_KEYS_H
#define WEB_KEYS_H

/* Which keys a native web view (WebView2 on Windows, web_engine_win.c) hands
 * back to the application instead of the page. WebKitGTK gets its keys
 * through GTK, so the tab and window shortcuts reach the application as for
 * any widget; a native view takes them all, so it asks here for each key
 * pressed with Ctrl or Alt held, or a function key. Pure, tested in
 * tests/test_web_keys.c. */
#include <gdk/gdk.h>

/* TRUE when keyval with mods (GDK_CONTROL_MASK, GDK_ALT_MASK and
 * GDK_SHIFT_MASK; AltGr is not Ctrl+Alt, the caller leaves it to the page)
 * goes to the application: Ctrl and Alt shortcuts except the editing keys
 * of the page (copy, paste, select all, undo, find, word moves), and the
 * function keys except F3 (find next), F7 (caret browsing) and F12. */
gboolean web_keys_for_app(guint keyval, GdkModifierType mods);

#endif /* WEB_KEYS_H */

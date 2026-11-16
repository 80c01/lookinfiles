/* Keys a native web view hands back to the application (see web_keys.h) */
#include "preview/web_keys.h"

/* Keys the page keeps with Ctrl (and Ctrl+Shift): editing and moving in
 * its text fields, finding in the page */
static gboolean page_ctrl_key(guint keyval) {
    switch (gdk_keyval_to_lower(keyval)) {
    case GDK_KEY_a:
    case GDK_KEY_c:
    case GDK_KEY_v:
    case GDK_KEY_x:
    case GDK_KEY_y:
    case GDK_KEY_z:
    case GDK_KEY_f:
    case GDK_KEY_BackSpace:
    case GDK_KEY_Delete:
    case GDK_KEY_Insert:
    case GDK_KEY_Left:
    case GDK_KEY_Right:
    case GDK_KEY_Up:
    case GDK_KEY_Down:
    case GDK_KEY_Home:
    case GDK_KEY_End:
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        return TRUE;
    default:
        return FALSE;
    }
}

gboolean web_keys_for_app(guint keyval, GdkModifierType mods) {
    if (keyval == 0) return FALSE;
    if (mods & GDK_ALT_MASK) return TRUE; /* Alt+digit, Alt+Left, Ctrl+Alt+Arrow... */
    if (mods & GDK_CONTROL_MASK) return !page_ctrl_key(keyval);
    if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F11) {
        return keyval != GDK_KEY_F3 && keyval != GDK_KEY_F7;
    }
    return FALSE;
}

#include "nav_keys.h"

#define CTRL_ALT (GDK_CONTROL_MASK | GDK_ALT_MASK)

/* Shift, Control, Alt... pressed alone: keeps a pending prefix */
static gboolean is_modifier_key(guint keyval) {
    return (keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) ||
           keyval == GDK_KEY_ISO_Level3_Shift || keyval == GDK_KEY_ISO_Level5_Shift;
}

/* Second key of Vim Ctrl+W: w / Ctrl+W / l / j / Right / Down go to the next
 * panel, W / h / k / p / Left / Up to the previous one */
static NavAction vim_window_key(guint keyval) {
    switch (keyval) {
    case GDK_KEY_w: case GDK_KEY_l: case GDK_KEY_j: case GDK_KEY_Right: case GDK_KEY_Down:
        return NAV_PANEL_NEXT;
    case GDK_KEY_W: case GDK_KEY_h: case GDK_KEY_k: case GDK_KEY_p:
    case GDK_KEY_Left: case GDK_KEY_Up:
        return NAV_PANEL_PREV;
    default:
        return NAV_CANCEL;
    }
}

/* Arrows, Home / End, Page Up / Down: the same in every mode */
static NavAction universal_key(guint keyval) {
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: return NAV_DOWN;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: return NAV_UP;
    case GDK_KEY_Right: case GDK_KEY_KP_Right: return NAV_FORWARD;
    case GDK_KEY_Left: case GDK_KEY_KP_Left: return NAV_BACKWARD;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: return NAV_FIRST;
    case GDK_KEY_End: case GDK_KEY_KP_End: return NAV_LAST;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return NAV_PAGE_DOWN;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return NAV_PAGE_UP;
    default: return NAV_NONE;
    }
}

/* Vim letter keys without Ctrl / Alt (Vim mode only) */
static NavAction vim_plain_key(NavPrefix prefix, guint keyval, NavPrefix* next) {
    switch (keyval) {
    case GDK_KEY_j: return NAV_DOWN;
    case GDK_KEY_k: return NAV_UP;
    case GDK_KEY_l: return NAV_FORWARD;
    case GDK_KEY_h: return NAV_BACKWARD;
    case GDK_KEY_G: return NAV_LAST;
    case GDK_KEY_g:
        if (prefix == NAV_PREFIX_G) return NAV_FIRST;
        *next = NAV_PREFIX_G;
        return NAV_PENDING;
    case GDK_KEY_z:
        if (prefix == NAV_PREFIX_Z) return NAV_CENTER;
        *next = NAV_PREFIX_Z;
        return NAV_PENDING;
    case GDK_KEY_t:
        return prefix == NAV_PREFIX_Z ? NAV_TOP : NAV_NONE;
    case GDK_KEY_b:
        return prefix == NAV_PREFIX_Z ? NAV_BOTTOM : NAV_NONE;
    case GDK_KEY_slash: return NAV_SEARCH;
    case GDK_KEY_n: return NAV_SEARCH_NEXT;
    case GDK_KEY_N: return NAV_SEARCH_PREV;
    default: return NAV_NONE;
    }
}

static NavAction vim_key(guint keyval, GdkModifierType state, NavPrefix* next) {
    if ((state & CTRL_ALT) != GDK_CONTROL_MASK) return NAV_NONE;
    switch (gdk_keyval_to_lower(keyval)) {
    case GDK_KEY_f: return NAV_PAGE_DOWN;
    case GDK_KEY_b: return NAV_PAGE_UP;
    case GDK_KEY_w:
        *next = NAV_PREFIX_CTRL_W;
        return NAV_PENDING;
    default: return NAV_NONE;
    }
}

static NavAction emacs_key(guint keyval, GdkModifierType state, NavPrefix* next) {
    GdkModifierType mods = state & CTRL_ALT;
    if (mods == GDK_ALT_MASK) {
        switch (keyval) {
        case GDK_KEY_less: return NAV_FIRST;
        case GDK_KEY_greater: return NAV_LAST;
        case GDK_KEY_v: case GDK_KEY_V: return NAV_PAGE_UP;
        default: return NAV_NONE;
        }
    }
    if (mods != GDK_CONTROL_MASK || (state & GDK_SHIFT_MASK) != 0) return NAV_NONE;
    switch (gdk_keyval_to_lower(keyval)) {
    case GDK_KEY_n: return NAV_DOWN;
    case GDK_KEY_p: return NAV_UP;
    case GDK_KEY_f: return NAV_FORWARD;
    case GDK_KEY_b: return NAV_BACKWARD;
    case GDK_KEY_v: return NAV_PAGE_DOWN;
    case GDK_KEY_l: return NAV_RECENTER;
    case GDK_KEY_s: return NAV_SEARCH;
    case GDK_KEY_r: return NAV_SEARCH_BACKWARD;
    case GDK_KEY_g: return NAV_CANCEL;
    case GDK_KEY_x:
        *next = NAV_PREFIX_CTRL_X;
        return NAV_PENDING;
    default: return NAV_NONE;
    }
}

static NavAction notepad_key(guint keyval, GdkModifierType state) {
    GdkModifierType mods = state & CTRL_ALT;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    if (mods == 0 && keyval == GDK_KEY_F3) return shift ? NAV_SEARCH_PREV : NAV_SEARCH_NEXT;
    if (mods == 0 && keyval == GDK_KEY_F6) return shift ? NAV_PANEL_PREV : NAV_PANEL_NEXT;
    if (mods != GDK_CONTROL_MASK || shift) return NAV_NONE;
    switch (keyval) {
    case GDK_KEY_f: case GDK_KEY_F: return NAV_SEARCH;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: return NAV_FIRST;
    case GDK_KEY_End: case GDK_KEY_KP_End: return NAV_LAST;
    default: return NAV_NONE;
    }
}

NavAction nav_keys_lookup(NavStyle style, NavPrefix prefix, guint keyval,
                          GdkModifierType state, NavPrefix* next_prefix) {
    NavPrefix dummy;
    NavPrefix* next = next_prefix != NULL ? next_prefix : &dummy;
    *next = NAV_PREFIX_NONE;

    if (is_modifier_key(keyval)) {
        *next = prefix;
        return prefix != NAV_PREFIX_NONE ? NAV_PENDING : NAV_NONE;
    }

    /* Second key of a mode prefix: C-x o, Ctrl+W w */
    if (prefix == NAV_PREFIX_CTRL_X) {
        if ((state & CTRL_ALT) == 0 && keyval == GDK_KEY_o) return NAV_PANEL_NEXT;
        return NAV_CANCEL;
    }
    if (prefix == NAV_PREFIX_CTRL_W) {
        if ((state & GDK_ALT_MASK) != 0) return NAV_CANCEL;
        if ((state & GDK_CONTROL_MASK) != 0) {
            return gdk_keyval_to_lower(keyval) == GDK_KEY_w ? NAV_PANEL_NEXT : NAV_CANCEL;
        }
        return vim_window_key(keyval);
    }

    if ((state & CTRL_ALT) == 0) {
        NavAction a = universal_key(keyval);
        if (a != NAV_NONE) return a;
        if (style == NAV_STYLE_VIM) return vim_plain_key(prefix, keyval, next);
        if (style == NAV_STYLE_NOTEPAD) return notepad_key(keyval, state);
        return NAV_NONE;
    }

    switch (style) {
    case NAV_STYLE_VIM: return vim_key(keyval, state, next);
    case NAV_STYLE_EMACS: return emacs_key(keyval, state, next);
    default: return notepad_key(keyval, state);
    }
}

NavSearchKey nav_keys_search_lookup(NavStyle style, guint keyval, GdkModifierType state) {
    GdkModifierType mods = state & CTRL_ALT;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean enter = keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter ||
                     keyval == GDK_KEY_ISO_Enter;

    if (mods == 0 && keyval == GDK_KEY_Escape) return NAV_SEARCH_KEY_ACCEPT;

    switch (style) {
    case NAV_STYLE_EMACS:
        if (mods == GDK_CONTROL_MASK && !shift) {
            switch (gdk_keyval_to_lower(keyval)) {
            case GDK_KEY_s: return NAV_SEARCH_KEY_NEXT;
            case GDK_KEY_r: return NAV_SEARCH_KEY_PREV;
            case GDK_KEY_g: return NAV_SEARCH_KEY_CANCEL;
            default: break;
            }
        }
        break;
    case NAV_STYLE_NOTEPAD:
        /* Enter finds the next match, like a Find box; Escape closes it */
        if (mods == 0 && (enter || keyval == GDK_KEY_F3)) {
            return shift ? NAV_SEARCH_KEY_PREV : NAV_SEARCH_KEY_NEXT;
        }
        break;
    default:
        break;
    }
    return enter && mods == 0 ? NAV_SEARCH_KEY_ACCEPT : NAV_SEARCH_KEY_NONE;
}

const gchar* nav_style_name(NavStyle style) {
    switch (style) {
    case NAV_STYLE_VIM: return "Vim";
    case NAV_STYLE_EMACS: return "Emacs";
    default: return "Notepad";
    }
}

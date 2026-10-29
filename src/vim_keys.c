/* Vim key codes: GDK conversion, normalization, <notation> */
#include "vim_keys.h"
#include <string.h>

gunichar vim_key_from_gdk(guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & (GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK)) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    if (alt) return 0;
    switch (keyval) {
        case GDK_KEY_Escape: return VIM_KEY_ESC;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
            return ctrl ? 0 : VIM_KEY_CR;
        case GDK_KEY_BackSpace: return ctrl ? 0 : VIM_KEY_BS;
        case GDK_KEY_Tab: case GDK_KEY_KP_Tab: return ctrl || shift ? 0 : VIM_KEY_TAB;
        case GDK_KEY_ISO_Left_Tab: return 0;
        case GDK_KEY_Delete: case GDK_KEY_KP_Delete: return ctrl ? 0 : VIM_KEY_DEL;
        case GDK_KEY_Left: case GDK_KEY_KP_Left: return ctrl || shift ? 0 : VIM_KEY_LEFT;
        case GDK_KEY_Right: case GDK_KEY_KP_Right: return ctrl || shift ? 0 : VIM_KEY_RIGHT;
        case GDK_KEY_Up: case GDK_KEY_KP_Up: return ctrl || shift ? 0 : VIM_KEY_UP;
        case GDK_KEY_Down: case GDK_KEY_KP_Down: return ctrl || shift ? 0 : VIM_KEY_DOWN;
        case GDK_KEY_Home: case GDK_KEY_KP_Home: return ctrl || shift ? 0 : VIM_KEY_HOME;
        case GDK_KEY_End: case GDK_KEY_KP_End: return ctrl || shift ? 0 : VIM_KEY_END;
        case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return ctrl || shift ? 0 : VIM_KEY_PAGEUP;
        case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return ctrl || shift ? 0 : VIM_KEY_PAGEDOWN;
        case GDK_KEY_Insert: case GDK_KEY_KP_Insert: return ctrl || shift ? 0 : VIM_KEY_INSERT;
        default: break;
    }
    if (ctrl) {
        if (shift && keyval != GDK_KEY_asciicircum && keyval != GDK_KEY_underscore &&
            keyval != GDK_KEY_at) {
            return 0; /* Ctrl+Shift+V and friends stay with GTK */
        }
        guint lower = gdk_keyval_to_lower(keyval);
        if (lower >= GDK_KEY_a && lower <= GDK_KEY_z) {
            return vim_key_normalize(VIM_KEY_CTRL('a' + (lower - GDK_KEY_a)));
        }
        switch (keyval) {
            case GDK_KEY_bracketleft: return VIM_KEY_ESC;
            case GDK_KEY_bracketright: return VIM_KEY_CTRL(']');
            case GDK_KEY_backslash: return VIM_KEY_CTRL('\\');
            case GDK_KEY_asciicircum: case GDK_KEY_6: return VIM_KEY_CTRL('^');
            case GDK_KEY_underscore: case GDK_KEY_minus: return VIM_KEY_CTRL('_');
            case GDK_KEY_at: case GDK_KEY_2: case GDK_KEY_space: return VIM_KEY_CTRL('@');
            default: return 0;
        }
    }
    gunichar uc = gdk_keyval_to_unicode(keyval);
    if (uc == 0 || g_unichar_iscntrl(uc)) return 0;
    return uc;
}

gunichar vim_key_normalize(gunichar k) {
    if (k == 0x1b) return VIM_KEY_ESC;
    if (k == '\r') return VIM_KEY_CR;
    if (k == 0x08 || k == 0x7f) return VIM_KEY_BS;
    if (k == '\t') return VIM_KEY_TAB;
    if (k == '\n') return VIM_KEY_NL;
    if (k >= 0x01 && k <= 0x1a) return VIM_KEY_CTRL('a' + (k - 1));
    if (k == 0x00) return VIM_KEY_CTRL('@');
    if (k == 0x1c) return VIM_KEY_CTRL('\\');
    if (k == 0x1d) return VIM_KEY_CTRL(']');
    if (k == 0x1e) return VIM_KEY_CTRL('^');
    if (k == 0x1f) return VIM_KEY_CTRL('_');
    switch (k) {
        case VIM_KEY_CTRL('['): return VIM_KEY_ESC;
        case VIM_KEY_CTRL('m'): return VIM_KEY_CR;
        case VIM_KEY_CTRL('h'): return VIM_KEY_BS;
        case VIM_KEY_CTRL('i'): return VIM_KEY_TAB;
        case VIM_KEY_CTRL('j'): return VIM_KEY_NL;
        default: return k;
    }
}

void vim_keys_append(GString* s, gunichar k) {
    g_string_append_unichar(s, k);
}

static const struct {
    const gchar* name;
    gunichar key;
} NAMED_KEYS[] = {
    {"esc", VIM_KEY_ESC}, {"cr", VIM_KEY_CR}, {"enter", VIM_KEY_CR}, {"return", VIM_KEY_CR},
    {"bs", VIM_KEY_BS}, {"tab", VIM_KEY_TAB}, {"del", VIM_KEY_DEL}, {"left", VIM_KEY_LEFT},
    {"right", VIM_KEY_RIGHT}, {"up", VIM_KEY_UP}, {"down", VIM_KEY_DOWN}, {"home", VIM_KEY_HOME},
    {"end", VIM_KEY_END}, {"pageup", VIM_KEY_PAGEUP}, {"pagedown", VIM_KEY_PAGEDOWN},
    {"insert", VIM_KEY_INSERT}, {"nl", VIM_KEY_NL}, {"lt", '<'}, {"space", ' '}, {"bar", '|'},
};

gchar* vim_keys_from_notation(const gchar* notation) {
    GString* out = g_string_new(NULL);
    const gchar* p = notation != NULL ? notation : "";
    while (*p != '\0') {
        if (*p == '<') {
            const gchar* close = strchr(p, '>');
            if (close != NULL && close - p > 1) {
                gchar* name = g_ascii_strdown(p + 1, close - p - 1);
                gunichar key = 0;
                if ((name[0] == 'c') && name[1] == '-' && name[2] != '\0' && name[3] == '\0') {
                    key = vim_key_normalize(VIM_KEY_CTRL(g_ascii_tolower(name[2])));
                } else {
                    for (gsize i = 0; i < G_N_ELEMENTS(NAMED_KEYS); i++) {
                        if (strcmp(name, NAMED_KEYS[i].name) == 0) key = NAMED_KEYS[i].key;
                    }
                }
                g_free(name);
                if (key != 0) {
                    vim_keys_append(out, key);
                    p = close + 1;
                    continue;
                }
            }
        }
        gunichar c = g_utf8_get_char(p);
        vim_keys_append(out, vim_key_normalize(c));
        p = g_utf8_next_char(p);
    }
    return g_string_free(out, FALSE);
}

gchar* vim_keys_to_display(const gchar* keys) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = keys != NULL ? keys : ""; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar k = g_utf8_get_char(p);
        switch (k) {
            case VIM_KEY_ESC: g_string_append(out, "^["); break;
            case VIM_KEY_CR: g_string_append(out, "^M"); break;
            case VIM_KEY_BS: g_string_append(out, "^H"); break;
            case VIM_KEY_TAB: g_string_append(out, "^I"); break;
            case VIM_KEY_NL: g_string_append(out, "^J"); break;
            case VIM_KEY_DEL: g_string_append(out, "<Del>"); break;
            case VIM_KEY_LEFT: g_string_append(out, "<Left>"); break;
            case VIM_KEY_RIGHT: g_string_append(out, "<Right>"); break;
            case VIM_KEY_UP: g_string_append(out, "<Up>"); break;
            case VIM_KEY_DOWN: g_string_append(out, "<Down>"); break;
            case VIM_KEY_HOME: g_string_append(out, "<Home>"); break;
            case VIM_KEY_END: g_string_append(out, "<End>"); break;
            case VIM_KEY_PAGEUP: g_string_append(out, "<PageUp>"); break;
            case VIM_KEY_PAGEDOWN: g_string_append(out, "<PageDown>"); break;
            case VIM_KEY_INSERT: g_string_append(out, "<Insert>"); break;
            default:
                if (VIM_KEY_IS_CTRL(k)) {
                    g_string_append_c(out, '^');
                    g_string_append_c(out, g_ascii_toupper((gchar)(k - 0xE100)));
                } else {
                    g_string_append_unichar(out, k);
                }
        }
    }
    return g_string_free(out, FALSE);
}

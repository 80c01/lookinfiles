#ifndef NAV_KEYS_H
#define NAV_KEYS_H

/* Navigation keys of the explorer and viewer panels in each global mode
 * (Notepad, Vim, Emacs), strictly those of the mode. Arrows, Home / End and
 * Page Up / Down work in every mode; the rest follows the mode:
 *   Vim:     j k h l, gg G, zz zt zb, / n N, Ctrl+F / Ctrl+B, Ctrl+W w / W
 *   Emacs:   C-n C-p C-f C-b, C-v / M-v, M-< / M->, C-l, C-s / C-r, C-x o
 *   Notepad: Ctrl+F find, F3 / Shift+F3, Ctrl+Home / Ctrl+End, F6 / Shift+F6 */
#include <gtk/gtk.h>

typedef enum {
    NAV_STYLE_NOTEPAD = 0,
    NAV_STYLE_VIM,
    NAV_STYLE_EMACS
} NavStyle;

/* Key typed after a prefix key, waiting for the next one */
typedef enum {
    NAV_PREFIX_NONE = 0,
    NAV_PREFIX_G,      /* g (gg) */
    NAV_PREFIX_Z,      /* z (zz, zt, zb) */
    NAV_PREFIX_CTRL_W, /* Vim Ctrl+W (window command) */
    NAV_PREFIX_CTRL_X  /* Emacs C-x */
} NavPrefix;

typedef enum {
    NAV_NONE = 0,     /* not a navigation key: the panel handles it */
    NAV_PENDING,      /* prefix (or modifier) typed: wait for the next key */
    NAV_CANCEL,       /* unknown key after C-x / Ctrl+W, or C-g: drop it */
    NAV_DOWN,
    NAV_UP,
    NAV_FORWARD,      /* expand / step into (Right, l, C-f) */
    NAV_BACKWARD,     /* collapse / parent (Left, h, C-b) */
    NAV_FIRST,
    NAV_LAST,
    NAV_PAGE_DOWN,
    NAV_PAGE_UP,
    NAV_CENTER,       /* zz */
    NAV_TOP,          /* zt */
    NAV_BOTTOM,       /* zb */
    NAV_RECENTER,     /* C-l: center, top, bottom in turn */
    NAV_SEARCH,       /* open the search bar */
    NAV_SEARCH_BACKWARD,
    NAV_SEARCH_NEXT,
    NAV_SEARCH_PREV,
    NAV_PANEL_NEXT,   /* focus the next / previous panel */
    NAV_PANEL_PREV
} NavAction;

/* Action of keyval with state in style, after the pending prefix.
 * *next_prefix receives the prefix to keep for the next key. Pure: callers
 * may look a key up without consuming it. */
NavAction nav_keys_lookup(NavStyle style, NavPrefix prefix, guint keyval,
                          GdkModifierType state, NavPrefix* next_prefix);

/* Keys typed in the search bar of the explorer / viewer */
typedef enum {
    NAV_SEARCH_KEY_NONE = 0, /* the entry's own key */
    NAV_SEARCH_KEY_NEXT,     /* go to the next match, keep the bar */
    NAV_SEARCH_KEY_PREV,
    NAV_SEARCH_KEY_ACCEPT,   /* close the bar on the match */
    NAV_SEARCH_KEY_CANCEL    /* close the bar, back where the search started */
} NavSearchKey;

NavSearchKey nav_keys_search_lookup(NavStyle style, guint keyval, GdkModifierType state);

/* Display name of the style ("Notepad", "Vim", "Emacs") */
const gchar* nav_style_name(NavStyle style);

#endif /* NAV_KEYS_H */

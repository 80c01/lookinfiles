#ifndef VT_SCREEN_H
#define VT_SCREEN_H

/* Screen of a terminal emulator: the bytes a program writes to its
 * pseudo-terminal (UTF-8 text, C0 controls, ESC / CSI / OSC sequences of
 * xterm, as ConPTY, bash or vim send them) applied to a grid of cells, with a
 * scrollback, the alternate screen and the answers the program asks for
 * (cursor position, device attributes). Pure GLib: drawn by term_widget. */

#include <glib.h>

/* Cell colours: the terminal's default, one of the 256 indexed colours
 * (0-15 the theme's ANSI colours), or 24-bit RGB */
#define VT_COLOR_DEFAULT 0u
#define VT_COLOR_INDEXED(i) (0x01000000u | (guint32)(i))
#define VT_COLOR_RGB(r, g, b) (0x02000000u | ((guint32)(r) << 16) | ((guint32)(g) << 8) | (guint32)(b))
#define VT_COLOR_KIND(c) ((c) >> 24) /* 0 default, 1 indexed, 2 RGB */

enum {
    VT_ATTR_BOLD = 1 << 0,
    VT_ATTR_DIM = 1 << 1,
    VT_ATTR_ITALIC = 1 << 2,
    VT_ATTR_UNDERLINE = 1 << 3,
    VT_ATTR_BLINK = 1 << 4,
    VT_ATTR_INVERSE = 1 << 5,
    VT_ATTR_HIDDEN = 1 << 6,
    VT_ATTR_STRIKE = 1 << 7,
    VT_ATTR_WIDE_TAIL = 1 << 8 /* right half of a double-width character */
};

typedef struct {
    gunichar ch; /* 0: blank */
    guint32 fg;
    guint32 bg;
    guint16 attrs;
} VtCell;

/* Modes the program set (vt_screen_modes()) */
enum {
    VT_MODE_APP_CURSOR = 1 << 0,      /* cursor keys send ESC O x */
    VT_MODE_BRACKETED_PASTE = 1 << 1, /* pastes wrapped in ESC [200~ ... ESC [201~ */
    VT_MODE_ALT_SCREEN = 1 << 2,
    VT_MODE_CURSOR_HIDDEN = 1 << 3,
    VT_MODE_FOCUS_EVENTS = 1 << 4 /* focus in / out reported as ESC [I / ESC [O */
};

/* What vt_screen_feed() changed besides the cells */
enum {
    VT_CHANGE_TITLE = 1 << 0,
    VT_CHANGE_BELL = 1 << 1
};

typedef struct VtScreen VtScreen;

/* cols x rows cells (at least 1 x 1), keeping up to max_scrollback lines */
VtScreen* vt_screen_new(gint cols, gint rows, gint max_scrollback);
void vt_screen_free(VtScreen* s);

/* Apply output of the program (sequences may be split between calls);
 * returns the VT_CHANGE_ flags */
guint vt_screen_feed(VtScreen* s, const gchar* data, gsize len);
/* New size: lines are cut or padded (no reflow); when rows shrink, the top
 * lines go to the scrollback so the cursor line stays */
void vt_screen_resize(VtScreen* s, gint cols, gint rows);

gint vt_screen_cols(const VtScreen* s);
gint vt_screen_rows(const VtScreen* s);
/* Lines of scrollback above the screen (0 on the alternate screen) */
gint vt_screen_scrollback(const VtScreen* s);
/* Lines ever pushed into the scrollback: a row r stays the same line as
 * r + scrolled while the count grows (selections) */
gint64 vt_screen_scrolled(const VtScreen* s);
/* Line row: 0 is the top of the screen, -1 the newest scrollback line;
 * *len cells (scrollback lines keep their width); NULL out of range */
const VtCell* vt_screen_line(const VtScreen* s, gint row, gint* len);
/* The line row continues on the next one (automatic wrap) */
gboolean vt_screen_line_wrapped(const VtScreen* s, gint row);
void vt_screen_cursor(const VtScreen* s, gint* row, gint* col);
guint vt_screen_modes(const VtScreen* s);
/* Title set by the program (OSC 0 / 2), "" if none */
const gchar* vt_screen_title(const VtScreen* s);
/* Bytes to write back to the program, NULL if none; caller frees */
gchar* vt_screen_take_reply(VtScreen* s, gsize* len);
/* Text from (row0, col0) to (row1, col1) excluded, rows as for
 * vt_screen_line(): trailing blanks dropped, a newline between lines that
 * do not wrap. Caller frees. */
gchar* vt_screen_text(const VtScreen* s, gint row0, gint col0, gint row1, gint col1);

#endif /* VT_SCREEN_H */

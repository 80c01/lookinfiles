/* Terminal screen model: parser of xterm output and grid of cells. */
#include "terminal/vt_screen.h"

#include <stdlib.h>
#include <string.h>

#define VT_MAX_PARAMS 16
#define VT_MAX_STRING 4096 /* OSC text kept (titles) */

typedef struct {
    gint len;
    gboolean wrapped;
    VtCell cells[];
} VtLine;

typedef enum {
    ST_GROUND,
    ST_ESC,
    ST_ESC_CHARSET, /* ESC ( x: charset of G0 / G1 */
    ST_ESC_HASH,    /* ESC # x */
    ST_CSI,
    ST_OSC,
    ST_STRING,      /* DCS, SOS, PM, APC: ignored up to ST */
    ST_STRING_ESC,  /* ESC seen in a string: ST if '\' follows */
} ParserState;

typedef struct {
    gint x, y;
    VtCell pen;
    gboolean graphics[2];
    gint charset;
    gboolean origin;
} SavedCursor;

struct VtScreen {
    gint cols, rows;
    VtLine** lines;     /* the screen shown (main or alternate) */
    VtLine** other;     /* the other one (NULL until the alternate screen is used) */
    gboolean alt;

    VtLine** sb;        /* scrollback ring of the main screen */
    gint sb_cap, sb_head, sb_len;
    gint64 scrolled;

    gint x, y;
    gboolean wrap_pending;
    VtCell pen;         /* colours and attributes of written cells */
    gint top, bottom;   /* scroll region, inclusive */
    gboolean autowrap, insert, origin;
    gboolean graphics[2]; /* G0 / G1 hold the DEC line-drawing set */
    gint charset;         /* 0: G0, 1: G1 (SO / SI) */
    gboolean* tabs;
    guint modes;
    SavedCursor saved, saved_alt;
    gunichar last_char;

    ParserState state;
    gint params[VT_MAX_PARAMS];
    gint nparams;
    gboolean param_started;
    gchar priv;         /* '?', '>', '=', '<' or 0 */
    gchar inter;        /* intermediate byte (' ', '$', '!'...) or 0 */
    gint charset_slot;
    GString* osc;

    guint32 utf8_cp;
    gint utf8_need;

    gchar* title;
    GString* reply;
    guint changes;
};

/* ------------------------------------------------------------------------- */
/* Lines                                                                     */
/* ------------------------------------------------------------------------- */

static VtCell blank_cell(const VtScreen* s) {
    VtCell c = {0, VT_COLOR_DEFAULT, s->pen.bg, 0};
    return c;
}

static VtLine* line_new(gint cols, VtCell fill) {
    VtLine* l = g_malloc(sizeof(VtLine) + sizeof(VtCell) * (gsize)cols);
    l->len = cols;
    l->wrapped = FALSE;
    for (gint i = 0; i < cols; i++) {
        l->cells[i] = fill;
    }
    return l;
}

static VtLine* line_resize(VtLine* l, gint cols) {
    if (l->len == cols) {
        return l;
    }
    gint old = l->len;
    l = g_realloc(l, sizeof(VtLine) + sizeof(VtCell) * (gsize)cols);
    VtCell blank = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};
    for (gint i = old; i < cols; i++) {
        l->cells[i] = blank;
    }
    l->len = cols;
    if (cols > 0 && (l->cells[cols - 1].attrs & VT_ATTR_WIDE_TAIL) == 0 && cols < old &&
        l->cells[cols - 1].ch != 0 && g_unichar_iswide(l->cells[cols - 1].ch)) {
        l->cells[cols - 1] = blank; /* half of a wide character cut off */
    }
    return l;
}

static VtLine** screen_new_lines(const VtScreen* s) {
    VtLine** lines = g_new(VtLine*, s->rows);
    VtCell blank = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};
    for (gint i = 0; i < s->rows; i++) {
        lines[i] = line_new(s->cols, blank);
    }
    return lines;
}

static void free_lines(VtLine** lines, gint n) {
    if (lines == NULL) {
        return;
    }
    for (gint i = 0; i < n; i++) {
        g_free(lines[i]);
    }
    g_free(lines);
}

static void sb_push(VtScreen* s, VtLine* l) {
    s->scrolled++;
    if (s->sb_cap == 0) {
        g_free(l);
        return;
    }
    if (s->sb_len < s->sb_cap) {
        s->sb[(s->sb_head + s->sb_len) % s->sb_cap] = l;
        s->sb_len++;
    } else {
        g_free(s->sb[s->sb_head]);
        s->sb[s->sb_head] = l;
        s->sb_head = (s->sb_head + 1) % s->sb_cap;
    }
}

/* Newest scrollback line taken back, NULL if none */
static VtLine* sb_pop(VtScreen* s) {
    if (s->sb_len == 0) {
        return NULL;
    }
    gint idx = (s->sb_head + s->sb_len - 1) % s->sb_cap;
    s->sb_len--;
    s->scrolled--;
    return s->sb[idx];
}

static void sb_clear(VtScreen* s) {
    for (gint i = 0; i < s->sb_len; i++) {
        g_free(s->sb[(s->sb_head + i) % s->sb_cap]);
    }
    s->sb_len = 0;
    s->sb_head = 0;
}

static void reset_tabs(VtScreen* s) {
    g_free(s->tabs);
    s->tabs = g_new0(gboolean, s->cols);
    for (gint i = 8; i < s->cols; i += 8) {
        s->tabs[i] = TRUE;
    }
}

/* ------------------------------------------------------------------------- */
/* Screen operations                                                         */
/* ------------------------------------------------------------------------- */

static void clamp_cursor(VtScreen* s) {
    s->x = CLAMP(s->x, 0, s->cols - 1);
    s->y = CLAMP(s->y, 0, s->rows - 1);
}

static void move_to(VtScreen* s, gint x, gint y) {
    s->wrap_pending = FALSE;
    s->x = x;
    s->y = y;
    clamp_cursor(s);
}

/* Lines top..bottom moved up by n; blank lines enter at the bottom. The lines
 * leaving the top of the main screen go to the scrollback (keep). */
static void scroll_up(VtScreen* s, gint top, gint bottom, gint n, gboolean keep) {
    gint height = bottom - top + 1;
    n = MIN(n, height);
    for (gint i = 0; i < n; i++) {
        VtLine* gone = s->lines[top + i];
        if (keep && top == 0 && !s->alt) {
            sb_push(s, gone);
        } else {
            g_free(gone);
        }
    }
    memmove(&s->lines[top], &s->lines[top + n], sizeof(VtLine*) * (gsize)(height - n));
    for (gint i = bottom - n + 1; i <= bottom; i++) {
        s->lines[i] = line_new(s->cols, blank_cell(s));
    }
}

static void scroll_down(VtScreen* s, gint top, gint bottom, gint n) {
    gint height = bottom - top + 1;
    n = MIN(n, height);
    for (gint i = bottom - n + 1; i <= bottom; i++) {
        g_free(s->lines[i]);
    }
    memmove(&s->lines[top + n], &s->lines[top], sizeof(VtLine*) * (gsize)(height - n));
    for (gint i = top; i < top + n; i++) {
        s->lines[i] = line_new(s->cols, blank_cell(s));
    }
}

static void linefeed(VtScreen* s) {
    s->wrap_pending = FALSE;
    if (s->y == s->bottom) {
        scroll_up(s, s->top, s->bottom, 1, TRUE);
    } else if (s->y < s->rows - 1) {
        s->y++;
    }
}

static void reverse_index(VtScreen* s) {
    s->wrap_pending = FALSE;
    if (s->y == s->top) {
        scroll_down(s, s->top, s->bottom, 1);
    } else if (s->y > 0) {
        s->y--;
    }
}

/* Cells x0..x1 (excluded) of line y blanked with the current background */
static void erase_cells(VtScreen* s, gint y, gint x0, gint x1) {
    VtLine* l = s->lines[y];
    x0 = CLAMP(x0, 0, l->len);
    x1 = CLAMP(x1, 0, l->len);
    for (gint i = x0; i < x1; i++) {
        l->cells[i] = blank_cell(s);
    }
    /* A wide character cut in half loses both halves */
    if (x0 > 0 && x0 < l->len && (l->cells[x0 - 1].attrs & VT_ATTR_WIDE_TAIL) == 0 &&
        l->cells[x0 - 1].ch != 0 && g_unichar_iswide(l->cells[x0 - 1].ch)) {
        l->cells[x0 - 1] = blank_cell(s);
    }
    if (x1 < l->len && (l->cells[x1].attrs & VT_ATTR_WIDE_TAIL)) {
        l->cells[x1] = blank_cell(s);
    }
    if (x1 >= l->len) {
        l->wrapped = FALSE;
    }
}

static void erase_lines(VtScreen* s, gint y0, gint y1) {
    for (gint y = MAX(y0, 0); y < MIN(y1, s->rows); y++) {
        erase_cells(s, y, 0, s->cols);
    }
}

static gunichar dec_graphics(gunichar c) {
    /* DEC special graphics, 0x60-0x7e: diamond, checkerboard, ..., box drawing */
    static const gunichar map[31] = {
        0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
        0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
        0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
        0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
    };
    return (c >= 0x60 && c <= 0x7e) ? map[c - 0x60] : c;
}

static void put_char(VtScreen* s, gunichar c) {
    if (s->graphics[s->charset]) {
        c = dec_graphics(c);
    }
    if (g_unichar_iszerowidth(c)) {
        return; /* combining marks are not composed */
    }
    gint width = g_unichar_iswide(c) ? 2 : 1;
    if (width > s->cols) {
        return;
    }
    if (s->wrap_pending && s->autowrap) {
        s->lines[s->y]->wrapped = TRUE;
        s->x = 0;
        linefeed(s);
    }
    s->wrap_pending = FALSE;
    if (width == 2 && s->x == s->cols - 1) {
        if (!s->autowrap) {
            return;
        }
        erase_cells(s, s->y, s->x, s->cols);
        s->lines[s->y]->wrapped = TRUE;
        s->x = 0;
        linefeed(s);
    }
    VtLine* l = s->lines[s->y];
    if (s->insert) {
        memmove(&l->cells[s->x + width], &l->cells[s->x],
                sizeof(VtCell) * (gsize)MAX(0, l->len - s->x - width));
    }
    /* Overwriting half of a wide character blanks its other half */
    if (l->cells[s->x].attrs & VT_ATTR_WIDE_TAIL && s->x > 0) {
        l->cells[s->x - 1] = blank_cell(s);
    }
    gint end = s->x + width;
    if (end < l->len && (l->cells[end].attrs & VT_ATTR_WIDE_TAIL)) {
        l->cells[end] = blank_cell(s);
    }
    VtCell cell = s->pen;
    cell.ch = c;
    cell.attrs &= (guint16)~VT_ATTR_WIDE_TAIL;
    l->cells[s->x] = cell;
    if (width == 2) {
        cell.ch = 0;
        cell.attrs |= VT_ATTR_WIDE_TAIL;
        l->cells[s->x + 1] = cell;
    }
    s->last_char = c;
    s->x += width;
    if (s->x >= s->cols) {
        s->x = s->cols - 1;
        s->wrap_pending = s->autowrap;
    }
}

static void save_cursor(VtScreen* s, SavedCursor* to) {
    to->x = s->x;
    to->y = s->y;
    to->pen = s->pen;
    to->graphics[0] = s->graphics[0];
    to->graphics[1] = s->graphics[1];
    to->charset = s->charset;
    to->origin = s->origin;
}

static void restore_cursor(VtScreen* s, const SavedCursor* from) {
    s->pen = from->pen;
    s->graphics[0] = from->graphics[0];
    s->graphics[1] = from->graphics[1];
    s->charset = from->charset;
    s->origin = from->origin;
    move_to(s, from->x, from->y);
}

static void set_alt_screen(VtScreen* s, gboolean on) {
    if (on == s->alt) {
        return;
    }
    if (s->other == NULL) {
        s->other = screen_new_lines(s);
    }
    VtLine** t = s->lines;
    s->lines = s->other;
    s->other = t;
    s->alt = on;
    if (on) {
        s->modes |= VT_MODE_ALT_SCREEN;
        erase_lines(s, 0, s->rows);
    } else {
        s->modes &= ~(guint)VT_MODE_ALT_SCREEN;
    }
    s->top = 0;
    s->bottom = s->rows - 1;
}

static void full_reset(VtScreen* s) {
    set_alt_screen(s, FALSE);
    VtCell pen = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};
    s->pen = pen;
    s->autowrap = TRUE;
    s->insert = FALSE;
    s->origin = FALSE;
    s->graphics[0] = s->graphics[1] = FALSE;
    s->charset = 0;
    s->modes = 0;
    s->top = 0;
    s->bottom = s->rows - 1;
    erase_lines(s, 0, s->rows);
    sb_clear(s);
    reset_tabs(s);
    move_to(s, 0, 0);
    save_cursor(s, &s->saved);
    save_cursor(s, &s->saved_alt);
}

/* ------------------------------------------------------------------------- */
/* Sequences                                                                 */
/* ------------------------------------------------------------------------- */

/* Parameter i, def when missing or 0 */
static gint param(const VtScreen* s, gint i, gint def) {
    return (i < s->nparams && s->params[i] > 0) ? s->params[i] : def;
}

static guint32 sgr_color(const VtScreen* s, gint* i) {
    /* 38;5;n or 38;2;r;g;b (the index i points at 38 / 48) */
    gint k = *i + 1;
    if (k < s->nparams && s->params[k] == 5 && k + 1 < s->nparams) {
        *i = k + 1;
        return VT_COLOR_INDEXED(CLAMP(s->params[k + 1], 0, 255));
    }
    if (k < s->nparams && s->params[k] == 2 && k + 3 < s->nparams) {
        *i = k + 3;
        return VT_COLOR_RGB(CLAMP(s->params[k + 1], 0, 255), CLAMP(s->params[k + 2], 0, 255),
                            CLAMP(s->params[k + 3], 0, 255));
    }
    *i = s->nparams;
    return VT_COLOR_DEFAULT;
}

static void do_sgr(VtScreen* s) {
    if (s->nparams == 0) {
        s->nparams = 1;
        s->params[0] = 0;
    }
    for (gint i = 0; i < s->nparams; i++) {
        gint p = s->params[i];
        guint16* a = &s->pen.attrs;
        switch (p) {
        case 0:
            s->pen.attrs = 0;
            s->pen.fg = s->pen.bg = VT_COLOR_DEFAULT;
            break;
        case 1: *a |= VT_ATTR_BOLD; break;
        case 2: *a |= VT_ATTR_DIM; break;
        case 3: *a |= VT_ATTR_ITALIC; break;
        case 4: case 21: *a |= VT_ATTR_UNDERLINE; break;
        case 5: case 6: *a |= VT_ATTR_BLINK; break;
        case 7: *a |= VT_ATTR_INVERSE; break;
        case 8: *a |= VT_ATTR_HIDDEN; break;
        case 9: *a |= VT_ATTR_STRIKE; break;
        case 22: *a &= (guint16)~(VT_ATTR_BOLD | VT_ATTR_DIM); break;
        case 23: *a &= (guint16)~VT_ATTR_ITALIC; break;
        case 24: *a &= (guint16)~VT_ATTR_UNDERLINE; break;
        case 25: *a &= (guint16)~VT_ATTR_BLINK; break;
        case 27: *a &= (guint16)~VT_ATTR_INVERSE; break;
        case 28: *a &= (guint16)~VT_ATTR_HIDDEN; break;
        case 29: *a &= (guint16)~VT_ATTR_STRIKE; break;
        case 38: s->pen.fg = sgr_color(s, &i); break;
        case 39: s->pen.fg = VT_COLOR_DEFAULT; break;
        case 48: s->pen.bg = sgr_color(s, &i); break;
        case 49: s->pen.bg = VT_COLOR_DEFAULT; break;
        default:
            if (p >= 30 && p <= 37) {
                s->pen.fg = VT_COLOR_INDEXED(p - 30);
            } else if (p >= 40 && p <= 47) {
                s->pen.bg = VT_COLOR_INDEXED(p - 40);
            } else if (p >= 90 && p <= 97) {
                s->pen.fg = VT_COLOR_INDEXED(p - 90 + 8);
            } else if (p >= 100 && p <= 107) {
                s->pen.bg = VT_COLOR_INDEXED(p - 100 + 8);
            }
            break;
        }
    }
}

static void set_mode(VtScreen* s, gboolean on) {
    for (gint i = 0; i < MAX(s->nparams, 1); i++) {
        gint p = i < s->nparams ? s->params[i] : 0;
        if (s->priv == 0) {
            if (p == 4) {
                s->insert = on;
            }
            continue;
        }
        if (s->priv != '?') {
            continue;
        }
        switch (p) {
        case 1:
            s->modes = on ? (s->modes | VT_MODE_APP_CURSOR) : (s->modes & ~(guint)VT_MODE_APP_CURSOR);
            break;
        case 6:
            s->origin = on;
            move_to(s, 0, on ? s->top : 0);
            break;
        case 7:
            s->autowrap = on;
            break;
        case 25:
            s->modes = on ? (s->modes & ~(guint)VT_MODE_CURSOR_HIDDEN) : (s->modes | VT_MODE_CURSOR_HIDDEN);
            break;
        case 47:
        case 1047:
            set_alt_screen(s, on);
            break;
        case 1048:
            if (on) {
                save_cursor(s, &s->saved);
            } else {
                restore_cursor(s, &s->saved);
            }
            break;
        case 1049:
            if (on) {
                save_cursor(s, &s->saved_alt);
                set_alt_screen(s, TRUE);
            } else {
                set_alt_screen(s, FALSE);
                restore_cursor(s, &s->saved_alt);
            }
            break;
        case 1004:
            s->modes = on ? (s->modes | VT_MODE_FOCUS_EVENTS) : (s->modes & ~(guint)VT_MODE_FOCUS_EVENTS);
            break;
        case 2004:
            s->modes = on ? (s->modes | VT_MODE_BRACKETED_PASTE) : (s->modes & ~(guint)VT_MODE_BRACKETED_PASTE);
            break;
        default:
            break; /* mouse reporting, win32-input-mode...: not supported */
        }
    }
}

static void do_csi(VtScreen* s, gchar final) {
    gint n = param(s, 0, 1);
    if (s->inter != 0) {
        return; /* DECSCUSR (cursor shape), DECSTR...: ignored */
    }
    if (s->priv != 0 && final != 'h' && final != 'l' && final != 'c' && final != 'n') {
        return;
    }
    switch (final) {
    case 'A': move_to(s, s->x, MAX(s->y - n, s->y >= s->top ? s->top : 0)); break;
    case 'B': case 'e': move_to(s, s->x, MIN(s->y + n, s->y <= s->bottom ? s->bottom : s->rows - 1)); break;
    case 'C': case 'a': move_to(s, s->x + n, s->y); break;
    case 'D': move_to(s, s->x - n, s->y); break;
    case 'E': move_to(s, 0, MIN(s->y + n, s->bottom)); break;
    case 'F': move_to(s, 0, MAX(s->y - n, s->top)); break;
    case 'G': case '`': move_to(s, n - 1, s->y); break;
    case 'd': move_to(s, s->x, (s->origin ? s->top : 0) + n - 1); break;
    case 'H': case 'f':
        move_to(s, param(s, 1, 1) - 1, (s->origin ? s->top : 0) + n - 1);
        break;
    case 'J': {
        gint mode = param(s, 0, 0);
        if (mode == 0) {
            erase_cells(s, s->y, s->x, s->cols);
            erase_lines(s, s->y + 1, s->rows);
        } else if (mode == 1) {
            erase_lines(s, 0, s->y);
            erase_cells(s, s->y, 0, s->x + 1);
        } else if (mode == 2) {
            erase_lines(s, 0, s->rows);
        } else if (mode == 3) {
            sb_clear(s);
        }
        break;
    }
    case 'K': {
        gint mode = param(s, 0, 0);
        if (mode == 0) {
            erase_cells(s, s->y, s->x, s->cols);
        } else if (mode == 1) {
            erase_cells(s, s->y, 0, s->x + 1);
        } else if (mode == 2) {
            erase_cells(s, s->y, 0, s->cols);
        }
        break;
    }
    case 'X': erase_cells(s, s->y, s->x, s->x + n); s->wrap_pending = FALSE; break;
    case 'L':
        if (s->y >= s->top && s->y <= s->bottom) {
            scroll_down(s, s->y, s->bottom, n);
            s->x = 0;
            s->wrap_pending = FALSE;
        }
        break;
    case 'M':
        if (s->y >= s->top && s->y <= s->bottom) {
            scroll_up(s, s->y, s->bottom, n, FALSE); /* deleted: not to the scrollback */
            s->x = 0;
            s->wrap_pending = FALSE;
        }
        break;
    case '@': {
        VtLine* l = s->lines[s->y];
        n = MIN(n, s->cols - s->x);
        memmove(&l->cells[s->x + n], &l->cells[s->x], sizeof(VtCell) * (gsize)(s->cols - s->x - n));
        erase_cells(s, s->y, s->x, s->x + n);
        s->wrap_pending = FALSE;
        break;
    }
    case 'P': {
        VtLine* l = s->lines[s->y];
        n = MIN(n, s->cols - s->x);
        memmove(&l->cells[s->x], &l->cells[s->x + n], sizeof(VtCell) * (gsize)(s->cols - s->x - n));
        erase_cells(s, s->y, s->cols - n, s->cols);
        s->wrap_pending = FALSE;
        break;
    }
    case 'S': scroll_up(s, s->top, s->bottom, n, TRUE); break;
    case 'T': scroll_down(s, s->top, s->bottom, n); break;
    case 'b':
        for (gint i = 0; i < MIN(n, 65535) && s->last_char != 0; i++) {
            put_char(s, s->last_char);
        }
        break;
    case 'm': do_sgr(s); break;
    case 'r': {
        gint top = param(s, 0, 1) - 1;
        gint bottom = param(s, 1, s->rows) - 1;
        if (top < bottom && bottom < s->rows) {
            s->top = top;
            s->bottom = bottom;
            move_to(s, 0, s->origin ? s->top : 0);
        }
        break;
    }
    case 's': save_cursor(s, &s->saved); break;
    case 'u': restore_cursor(s, &s->saved); break;
    case 'h': set_mode(s, TRUE); break;
    case 'l': set_mode(s, FALSE); break;
    case 'g':
        if (param(s, 0, 0) == 0) {
            s->tabs[s->x] = FALSE;
        } else if (param(s, 0, 0) == 3) {
            memset(s->tabs, 0, sizeof(gboolean) * (gsize)s->cols);
        }
        break;
    case 'I':
    case 'Z':
        for (gint i = 0; i < n; i++) {
            gint x = s->x;
            if (final == 'I') {
                do { x++; } while (x < s->cols - 1 && !s->tabs[x]);
            } else {
                do { x--; } while (x > 0 && !s->tabs[x]);
            }
            move_to(s, x, s->y);
        }
        break;
    case 'n':
        if (s->priv == 0 && param(s, 0, 0) == 5) {
            g_string_append(s->reply, "\033[0n");
        } else if (param(s, 0, 0) == 6) {
            gint row = s->y + 1 - (s->origin ? s->top : 0);
            g_string_append_printf(s->reply, s->priv == '?' ? "\033[?%d;%dR" : "\033[%d;%dR", row, s->x + 1);
        }
        break;
    case 'c':
        if (param(s, 0, 0) != 0) {
            break;
        }
        if (s->priv == 0) {
            g_string_append(s->reply, "\033[?62;22c"); /* VT220 with ANSI colour */
        } else if (s->priv == '>') {
            g_string_append(s->reply, "\033[>1;10;0c");
        }
        break;
    default:
        break;
    }
}

static void do_osc(VtScreen* s) {
    const gchar* text = s->osc->str;
    const gchar* semi = strchr(text, ';');
    if (semi == NULL) {
        return;
    }
    gint code = atoi(text);
    if (code == 0 || code == 2) {
        if (g_strcmp0(s->title, semi + 1) != 0) {
            g_free(s->title);
            s->title = g_utf8_make_valid(semi + 1, -1);
            s->changes |= VT_CHANGE_TITLE;
        }
    }
}

static void do_esc(VtScreen* s, gunichar c) {
    s->state = ST_GROUND;
    switch (c) {
    case '[':
        s->state = ST_CSI;
        s->nparams = 0;
        s->param_started = FALSE;
        s->priv = 0;
        s->inter = 0;
        memset(s->params, 0, sizeof(s->params));
        break;
    case ']':
        s->state = ST_OSC;
        g_string_truncate(s->osc, 0);
        break;
    case 'P': case 'X': case '^': case '_':
        s->state = ST_STRING;
        break;
    case '(': case ')':
        s->charset_slot = c == '(' ? 0 : 1;
        s->state = ST_ESC_CHARSET;
        break;
    case '*': case '+': case '-': case '.': case '/':
        s->charset_slot = -1; /* G2 / G3: not used */
        s->state = ST_ESC_CHARSET;
        break;
    case '#': s->state = ST_ESC_HASH; break;
    case '7': save_cursor(s, &s->saved); break;
    case '8': restore_cursor(s, &s->saved); break;
    case 'D': linefeed(s); break;
    case 'E': s->x = 0; linefeed(s); break;
    case 'M': reverse_index(s); break;
    case 'H': s->tabs[s->x] = TRUE; break;
    case 'c': full_reset(s); break;
    default: break; /* ESC = / ESC > (keypad modes), ESC \ ... */
    }
}

static void csi_byte(VtScreen* s, gunichar c) {
    if (c >= '0' && c <= '9') {
        if (s->nparams == 0) {
            s->nparams = 1;
        }
        gint* p = &s->params[s->nparams - 1];
        if (*p < 100000) {
            *p = *p * 10 + (gint)(c - '0');
        }
        s->param_started = TRUE;
    } else if (c == ';' || c == ':') {
        if (s->nparams == 0) {
            s->nparams = 1;
        }
        if (s->nparams < VT_MAX_PARAMS) {
            s->nparams++;
        }
    } else if (c >= '<' && c <= '?') {
        s->priv = (gchar)c;
    } else if (c >= 0x20 && c <= 0x2f) {
        s->inter = (gchar)c;
    } else if (c >= 0x40 && c <= 0x7e) {
        do_csi(s, (gchar)c);
        s->state = ST_GROUND;
    } else {
        s->state = ST_GROUND; /* malformed */
    }
}

static void control(VtScreen* s, gunichar c) {
    switch (c) {
    case 0x07: s->changes |= VT_CHANGE_BELL; break;
    case 0x08:
        if (s->x > 0) {
            s->x--;
        }
        s->wrap_pending = FALSE;
        break;
    case 0x09: {
        gint x = s->x;
        do { x++; } while (x < s->cols - 1 && !s->tabs[x]);
        move_to(s, x, s->y);
        break;
    }
    case 0x0a: case 0x0b: case 0x0c: linefeed(s); break;
    case 0x0d: s->x = 0; s->wrap_pending = FALSE; break;
    case 0x0e: s->charset = 1; break;
    case 0x0f: s->charset = 0; break;
    default: break;
    }
}

static void process(VtScreen* s, gunichar c) {
    /* Strings run up to BEL (OSC) or ST */
    if (s->state == ST_OSC) {
        if (c == 0x07) {
            do_osc(s);
            s->state = ST_GROUND;
        } else if (c == 0x1b) {
            s->state = ST_STRING_ESC;
            g_string_append_c(s->osc, '\0'); /* marks an OSC ending */
        } else if (s->osc->len < VT_MAX_STRING) {
            g_string_append_unichar(s->osc, c);
        }
        return;
    }
    if (s->state == ST_STRING) {
        if (c == 0x1b) {
            s->state = ST_STRING_ESC;
            g_string_truncate(s->osc, 0);
        } else if (c == 0x07) {
            s->state = ST_GROUND;
        }
        return;
    }
    if (s->state == ST_STRING_ESC) {
        gboolean was_osc = s->osc->len > 0 && s->osc->str[s->osc->len - 1] == '\0';
        if (was_osc) {
            g_string_truncate(s->osc, s->osc->len - 1);
            do_osc(s);
        }
        s->state = ST_GROUND;
        if (c != '\\') {
            if (c == 0x1b) {
                s->state = ST_ESC;
            } else {
                process(s, c);
            }
        }
        return;
    }

    if (c == 0x18 || c == 0x1a) {
        s->state = ST_GROUND; /* CAN / SUB cancel a sequence */
        return;
    }
    if (c == 0x1b) {
        s->state = ST_ESC;
        return;
    }
    if (c < 0x20 || c == 0x7f) {
        if (c != 0x7f) {
            control(s, c); /* also inside sequences, as xterm does */
        }
        return;
    }
    switch (s->state) {
    case ST_GROUND:
        if (c >= 0x80 && c < 0xa0) {
            return; /* C1 controls written as UTF-8 */
        }
        put_char(s, c);
        break;
    case ST_ESC:
        do_esc(s, c);
        break;
    case ST_ESC_CHARSET:
        if (s->charset_slot >= 0) {
            s->graphics[s->charset_slot] = (c == '0');
        }
        s->state = ST_GROUND;
        break;
    case ST_ESC_HASH:
        s->state = ST_GROUND;
        break;
    case ST_CSI:
        csi_byte(s, c);
        break;
    default:
        s->state = ST_GROUND;
        break;
    }
}

guint vt_screen_feed(VtScreen* s, const gchar* data, gsize len) {
    s->changes = 0;
    const guchar* p = (const guchar*)data;
    for (gsize i = 0; i < len; i++) {
        guchar b = p[i];
        if (s->utf8_need > 0) {
            if ((b & 0xc0) == 0x80) {
                s->utf8_cp = (s->utf8_cp << 6) | (b & 0x3f);
                if (--s->utf8_need == 0) {
                    gunichar c = s->utf8_cp;
                    process(s, g_unichar_validate(c) ? c : 0xfffd);
                }
                continue;
            }
            s->utf8_need = 0;
            process(s, 0xfffd); /* truncated sequence; b is read again below */
        }
        if (b < 0x80) {
            process(s, b);
        } else if ((b & 0xe0) == 0xc0) {
            s->utf8_cp = b & 0x1f;
            s->utf8_need = 1;
        } else if ((b & 0xf0) == 0xe0) {
            s->utf8_cp = b & 0x0f;
            s->utf8_need = 2;
        } else if ((b & 0xf8) == 0xf0) {
            s->utf8_cp = b & 0x07;
            s->utf8_need = 3;
        } else {
            process(s, 0xfffd);
        }
    }
    return s->changes;
}

/* ------------------------------------------------------------------------- */
/* Life cycle, size, queries                                                 */
/* ------------------------------------------------------------------------- */

VtScreen* vt_screen_new(gint cols, gint rows, gint max_scrollback) {
    VtScreen* s = g_new0(VtScreen, 1);
    s->cols = MAX(cols, 1);
    s->rows = MAX(rows, 1);
    s->lines = screen_new_lines(s);
    s->sb_cap = MAX(max_scrollback, 0);
    s->sb = s->sb_cap > 0 ? g_new0(VtLine*, s->sb_cap) : NULL;
    s->osc = g_string_new(NULL);
    s->reply = g_string_new(NULL);
    s->title = g_strdup("");
    full_reset(s);
    return s;
}

void vt_screen_free(VtScreen* s) {
    if (s == NULL) {
        return;
    }
    free_lines(s->lines, s->rows);
    free_lines(s->other, s->rows);
    sb_clear(s);
    g_free(s->sb);
    g_free(s->tabs);
    g_string_free(s->osc, TRUE);
    g_string_free(s->reply, TRUE);
    g_free(s->title);
    g_free(s);
}

/* Lines of one screen to rows x cols; main: lines move to and from the
 * scrollback, keeping *cy on its line */
static VtLine** resize_lines(VtScreen* s, VtLine** lines, gint cols, gint rows, gboolean main, gint* cy) {
    if (lines == NULL) {
        return NULL;
    }
    GPtrArray* out = g_ptr_array_new();
    gint drop_top = MAX(0, *cy + 1 - rows);
    for (gint i = 0; i < s->rows; i++) {
        if (i < drop_top) {
            if (main) {
                sb_push(s, lines[i]);
            } else {
                g_free(lines[i]);
            }
        } else if ((gint)out->len < rows) {
            g_ptr_array_add(out, line_resize(lines[i], cols));
        } else {
            g_free(lines[i]);
        }
    }
    g_free(lines);
    *cy -= drop_top;
    /* Growing: the main screen takes its lines back from the scrollback */
    while (main && (gint)out->len < rows && s->sb_len > 0 && drop_top == 0) {
        g_ptr_array_insert(out, 0, line_resize(sb_pop(s), cols));
        (*cy)++;
    }
    VtCell blank = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};
    while ((gint)out->len < rows) {
        g_ptr_array_add(out, line_new(cols, blank));
    }
    return (VtLine**)g_ptr_array_free(out, FALSE);
}

void vt_screen_resize(VtScreen* s, gint cols, gint rows) {
    cols = MAX(cols, 1);
    rows = MAX(rows, 1);
    if (cols == s->cols && rows == s->rows) {
        return;
    }
    VtLine** main_lines = s->alt ? s->other : s->lines;
    VtLine** alt_lines = s->alt ? s->lines : s->other;
    gint main_y = s->alt ? s->saved_alt.y : s->y;
    gint alt_y = s->alt ? s->y : 0;
    main_lines = resize_lines(s, main_lines, cols, rows, TRUE, &main_y);
    alt_lines = resize_lines(s, alt_lines, cols, rows, FALSE, &alt_y);
    s->cols = cols;
    s->rows = rows;
    if (s->alt) {
        s->lines = alt_lines;
        s->other = main_lines;
        s->y = alt_y;
        s->saved_alt.y = CLAMP(main_y, 0, rows - 1);
    } else {
        s->lines = main_lines;
        s->other = alt_lines;
        s->y = main_y;
    }
    s->top = 0;
    s->bottom = rows - 1;
    s->wrap_pending = FALSE;
    reset_tabs(s);
    clamp_cursor(s);
    s->saved.x = CLAMP(s->saved.x, 0, cols - 1);
    s->saved.y = CLAMP(s->saved.y, 0, rows - 1);
    s->saved_alt.x = CLAMP(s->saved_alt.x, 0, cols - 1);
}

gint vt_screen_cols(const VtScreen* s) {
    return s->cols;
}

gint vt_screen_rows(const VtScreen* s) {
    return s->rows;
}

gint vt_screen_scrollback(const VtScreen* s) {
    return s->alt ? 0 : s->sb_len;
}

gint64 vt_screen_scrolled(const VtScreen* s) {
    return s->scrolled;
}

static const VtLine* get_line(const VtScreen* s, gint row) {
    if (row >= 0) {
        return row < s->rows ? s->lines[row] : NULL;
    }
    if (s->alt || -row > s->sb_len) {
        return NULL;
    }
    return s->sb[(s->sb_head + s->sb_len + row) % s->sb_cap];
}

const VtCell* vt_screen_line(const VtScreen* s, gint row, gint* len) {
    const VtLine* l = get_line(s, row);
    if (len != NULL) {
        *len = l != NULL ? l->len : 0;
    }
    return l != NULL ? l->cells : NULL;
}

gboolean vt_screen_line_wrapped(const VtScreen* s, gint row) {
    const VtLine* l = get_line(s, row);
    return l != NULL && l->wrapped;
}

void vt_screen_cursor(const VtScreen* s, gint* row, gint* col) {
    if (row != NULL) {
        *row = s->y;
    }
    if (col != NULL) {
        *col = s->x;
    }
}

guint vt_screen_modes(const VtScreen* s) {
    return s->modes;
}

const gchar* vt_screen_title(const VtScreen* s) {
    return s->title;
}

gchar* vt_screen_take_reply(VtScreen* s, gsize* len) {
    if (s->reply->len == 0) {
        if (len != NULL) {
            *len = 0;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = s->reply->len;
    }
    gchar* out = g_strndup(s->reply->str, s->reply->len);
    g_string_truncate(s->reply, 0);
    return out;
}

gchar* vt_screen_text(const VtScreen* s, gint row0, gint col0, gint row1, gint col1) {
    GString* out = g_string_new(NULL);
    for (gint row = row0; row <= row1; row++) {
        gint len = 0;
        const VtCell* cells = vt_screen_line(s, row, &len);
        gint from = row == row0 ? MAX(col0, 0) : 0;
        gint to = row == row1 ? MIN(col1, len) : len;
        gsize line_start = out->len;
        for (gint i = from; cells != NULL && i < to; i++) {
            if (cells[i].attrs & VT_ATTR_WIDE_TAIL) {
                continue;
            }
            g_string_append_unichar(out, cells[i].ch != 0 ? cells[i].ch : ' ');
        }
        gboolean wrapped = vt_screen_line_wrapped(s, row) && to >= len;
        if (!wrapped) {
            while (out->len > line_start && out->str[out->len - 1] == ' ') {
                g_string_truncate(out, out->len - 1);
            }
            if (row < row1) {
                g_string_append_c(out, '\n');
            }
        }
    }
    return g_string_free(out, FALSE);
}

/* Chess game with Minimax AI for LookinFiles */
#include "game_chess.h"
#include "palette.h"
#include "fonts.h"
#include "icons.h"
#include "app_events.h"
#include <stdlib.h>
#include <math.h>

#define PIECE_EMPTY  0
#define PIECE_PAWN   1
#define PIECE_KNIGHT 2
#define PIECE_BISHOP 3
#define PIECE_ROOK   4
#define PIECE_QUEEN  5
#define PIECE_KING   6

typedef struct {
    gint from_r, from_c;
    gint to_r, to_c;
} ChessMove;

typedef struct {
    AppData* app;
    GtkWidget* drawing_area;
    GtkWidget* status_label;
    GtkWidget* mode_btn;

    /* Board: positive = White, negative = Black */
    gint board[8][8];
    gint turn; /* +1 White, -1 Black */
    gboolean vs_ai;
    gboolean game_over;

    /* Castling rights */
    gboolean w_castle_k, w_castle_q;
    gboolean b_castle_k, b_castle_q;
    gint ep_c; /* en passant target column (-1 if none) */

    /* Selection & cursor */
    gint sel_r, sel_c;
    gint cur_r, cur_c;
    gboolean in_check;
    const gchar* message;
} ChessGame;

static const gint INITIAL_BOARD[8][8] = {
    {-4, -2, -3, -5, -6, -3, -2, -4},
    {-1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 1,  1,  1,  1,  1,  1,  1,  1},
    { 4,  2,  3,  5,  6,  3,  2,  4}
};

static void chess_init_board(ChessGame* g) {
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) {
            g->board[r][c] = INITIAL_BOARD[r][c];
        }
    }
    g->turn = 1; /* White starts */
    g->w_castle_k = TRUE;
    g->w_castle_q = TRUE;
    g->b_castle_k = TRUE;
    g->b_castle_q = TRUE;
    g->ep_c = -1;
    g->sel_r = -1;
    g->sel_c = -1;
    g->cur_r = 6;
    g->cur_c = 4;
    g->in_check = FALSE;
    g->game_over = FALSE;
    g->message = "White to move";
}

/* Returns TRUE if side's king is attacked at square (kr, kc) */
static gboolean is_square_attacked(const gint b[8][8], gint target_r, gint target_c, gint attacker_side) {
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) {
            gint p = b[r][c];
            if (p * attacker_side <= 0) continue;
            gint type = abs(p);
            gint dr = target_r - r;
            gint dc = target_c - c;

            if (type == PIECE_PAWN) {
                gint forward = (attacker_side > 0) ? -1 : 1;
                if (dr == forward && abs(dc) == 1) return TRUE;
            } else if (type == PIECE_KNIGHT) {
                if ((abs(dr) == 1 && abs(dc) == 2) || (abs(dr) == 2 && abs(dc) == 1)) return TRUE;
            } else if (type == PIECE_KING) {
                if (abs(dr) <= 1 && abs(dc) <= 1) return TRUE;
            } else if (type == PIECE_BISHOP || type == PIECE_ROOK || type == PIECE_QUEEN) {
                gboolean is_diag = (abs(dr) == abs(dc) && dr != 0);
                gboolean is_orth = (dr == 0 || dc == 0) && (dr != 0 || dc != 0);
                if ((type == PIECE_BISHOP && !is_diag) ||
                    (type == PIECE_ROOK && !is_orth) ||
                    (type == PIECE_QUEEN && !is_diag && !is_orth)) continue;

                gint step_r = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
                gint step_c = (dc == 0) ? 0 : (dc > 0 ? 1 : -1);
                gint curr_r = r + step_r;
                gint curr_c = c + step_c;
                gboolean clear = TRUE;
                while (curr_r != target_r || curr_c != target_c) {
                    if (b[curr_r][curr_c] != 0) { clear = FALSE; break; }
                    curr_r += step_r;
                    curr_c += step_c;
                }
                if (clear) return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean is_in_check(const gint b[8][8], gint side) {
    gint kr = -1, kc = -1;
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) {
            if (b[r][c] == side * PIECE_KING) {
                kr = r; kc = c; break;
            }
        }
    }
    if (kr < 0) return FALSE;
    return is_square_attacked(b, kr, kc, -side);
}

static gboolean is_pseudo_legal_move(ChessGame* g, gint fr, gint fc, gint tr, gint tc) {
    if (fr == tr && fc == tc) return FALSE;
    if (fr < 0 || fr >= 8 || fc < 0 || fc >= 8) return FALSE;
    if (tr < 0 || tr >= 8 || tc < 0 || tc >= 8) return FALSE;

    gint p = g->board[fr][fc];
    if (p * g->turn <= 0) return FALSE; /* Not moving own piece */

    gint dest = g->board[tr][tc];
    if (dest * g->turn > 0) return FALSE; /* Cannot capture own piece */

    gint type = abs(p);
    gint dr = tr - fr;
    gint dc = tc - fc;

    if (type == PIECE_PAWN) {
        gint dir = (g->turn > 0) ? -1 : 1;
        gint start_r = (g->turn > 0) ? 6 : 1;

        /* Forward 1 */
        if (dc == 0 && dr == dir && dest == 0) return TRUE;
        /* Forward 2 from start */
        if (dc == 0 && fr == start_r && dr == 2 * dir && dest == 0 && g->board[fr + dir][fc] == 0) return TRUE;
        /* Diagonal Capture */
        if (abs(dc) == 1 && dr == dir) {
            if (dest * g->turn < 0) return TRUE;
            /* En passant */
            if (dest == 0 && tc == g->ep_c && fr == (g->turn > 0 ? 3 : 4)) return TRUE;
        }
        return FALSE;
    }

    if (type == PIECE_KNIGHT) {
        return (abs(dr) == 1 && abs(dc) == 2) || (abs(dr) == 2 && abs(dc) == 1);
    }

    if (type == PIECE_BISHOP || type == PIECE_ROOK || type == PIECE_QUEEN) {
        gboolean is_diag = (abs(dr) == abs(dc) && dr != 0);
        gboolean is_orth = (dr == 0 || dc == 0) && (dr != 0 || dc != 0);
        if ((type == PIECE_BISHOP && !is_diag) ||
            (type == PIECE_ROOK && !is_orth) ||
            (type == PIECE_QUEEN && !is_diag && !is_orth)) return FALSE;

        gint step_r = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
        gint step_c = (dc == 0) ? 0 : (dc > 0 ? 1 : -1);
        gint curr_r = fr + step_r;
        gint curr_c = fc + step_c;
        while (curr_r != tr || curr_c != tc) {
            if (g->board[curr_r][curr_c] != 0) return FALSE;
            curr_r += step_r;
            curr_c += step_c;
        }
        return TRUE;
    }

    if (type == PIECE_KING) {
        if (abs(dr) <= 1 && abs(dc) <= 1) return TRUE;

        /* Castling */
        if (dr == 0 && abs(dc) == 2 && !g->in_check) {
            if (g->turn > 0 && fr == 7 && fc == 4) {
                if (dc == 2 && g->w_castle_k && g->board[7][5] == 0 && g->board[7][6] == 0 &&
                    !is_square_attacked(g->board, 7, 5, -1)) return TRUE;
                if (dc == -2 && g->w_castle_q && g->board[7][3] == 0 && g->board[7][2] == 0 && g->board[7][1] == 0 &&
                    !is_square_attacked(g->board, 7, 3, -1)) return TRUE;
            } else if (g->turn < 0 && fr == 0 && fc == 4) {
                if (dc == 2 && g->b_castle_k && g->board[0][5] == 0 && g->board[0][6] == 0 &&
                    !is_square_attacked(g->board, 0, 5, 1)) return TRUE;
                if (dc == -2 && g->b_castle_q && g->board[0][3] == 0 && g->board[0][2] == 0 && g->board[0][1] == 0 &&
                    !is_square_attacked(g->board, 0, 3, 1)) return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean is_legal_move(ChessGame* g, gint fr, gint fc, gint tr, gint tc) {
    if (!is_pseudo_legal_move(g, fr, fc, tr, tc)) return FALSE;

    /* Verify own King does not end in check */
    gint temp[8][8];
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) temp[r][c] = g->board[r][c];
    }
    temp[tr][tc] = temp[fr][fc];
    temp[fr][fc] = 0;

    /* En passant capture removal */
    if (abs(g->board[fr][fc]) == PIECE_PAWN && tc == g->ep_c && abs(tr - fr) == 1 && abs(tc - fc) == 1) {
        temp[fr][tc] = 0;
    }

    return !is_in_check(temp, g->turn);
}

static GArray* get_all_legal_moves(ChessGame* g) {
    GArray* moves = g_array_new(FALSE, FALSE, sizeof(ChessMove));
    for (gint fr = 0; fr < 8; fr++) {
        for (gint fc = 0; fc < 8; fc++) {
            if (g->board[fr][fc] * g->turn > 0) {
                for (gint tr = 0; tr < 8; tr++) {
                    for (gint tc = 0; tc < 8; tc++) {
                        if (is_legal_move(g, fr, fc, tr, tc)) {
                            ChessMove m = {fr, fc, tr, tc};
                            g_array_append_val(moves, m);
                        }
                    }
                }
            }
        }
    }
    return moves;
}

static void execute_move(ChessGame* g, gint fr, gint fc, gint tr, gint tc) {
    gint p = g->board[fr][fc];
    gint type = abs(p);

    /* En passant capture */
    if (type == PIECE_PAWN && tc == g->ep_c && abs(tr - fr) == 1 && abs(tc - fc) == 1) {
        g->board[fr][tc] = 0;
    }

    /* Double pawn push sets ep_c */
    if (type == PIECE_PAWN && abs(tr - fr) == 2) {
        g->ep_c = fc;
    } else {
        g->ep_c = -1;
    }

    /* Castling rook move */
    if (type == PIECE_KING && abs(tc - fc) == 2) {
        if (tc == 6) { /* Kingside */
            g->board[tr][5] = g->board[tr][7];
            g->board[tr][7] = 0;
        } else if (tc == 2) { /* Queenside */
            g->board[tr][3] = g->board[tr][0];
            g->board[tr][0] = 0;
        }
    }

    /* Update castling rights */
    if (fr == 7 && fc == 4) { g->w_castle_k = FALSE; g->w_castle_q = FALSE; }
    if (fr == 0 && fc == 4) { g->b_castle_k = FALSE; g->b_castle_q = FALSE; }
    if (fr == 7 && fc == 7) g->w_castle_k = FALSE;
    if (fr == 7 && fc == 0) g->w_castle_q = FALSE;
    if (fr == 0 && fc == 7) g->b_castle_k = FALSE;
    if (fr == 0 && fc == 0) g->b_castle_q = FALSE;

    /* Move piece */
    g->board[tr][tc] = p;
    g->board[fr][fc] = 0;

    /* Promotion to Queen */
    if (type == PIECE_PAWN && (tr == 0 || tr == 7)) {
        g->board[tr][tc] = (g->turn > 0) ? PIECE_QUEEN : -PIECE_QUEEN;
    }

    /* Switch turn */
    g->turn = -g->turn;
    g->sel_r = -1;
    g->sel_c = -1;
    g->in_check = is_in_check(g->board, g->turn);

    GArray* next_moves = get_all_legal_moves(g);
    if (next_moves->len == 0) {
        g->game_over = TRUE;
        if (g->in_check) {
            g->message = (g->turn < 0) ? "Checkmate! White wins!" : "Checkmate! Black wins!";
        } else {
            g->message = "Stalemate! Draw!";
        }
    } else {
        g->message = (g->turn > 0) ? (g->in_check ? "White in Check!" : "White to move")
                                   : (g->in_check ? "Black in Check!" : "Black to move");
    }
    g_array_unref(next_moves);
}

/* AI Minimax evaluation */
static gint evaluate_board(const gint b[8][8]) {
    gint val = 0;
    gint piece_vals[7] = {0, 100, 320, 330, 500, 900, 20000};
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) {
            gint p = b[r][c];
            if (p != 0) {
                gint sign = (p > 0) ? 1 : -1;
                gint type = abs(p);
                val += sign * piece_vals[type];
                /* Slight center control bonus */
                if (r >= 2 && r <= 5 && c >= 2 && c <= 5) {
                    val += sign * 15;
                }
            }
        }
    }
    return val;
}

static void chess_ai_make_move(ChessGame* g) {
    if (g->game_over || g->turn > 0) return;

    GArray* moves = get_all_legal_moves(g);
    if (moves->len == 0) {
        g_array_unref(moves);
        return;
    }

    gint best_score = 999999;
    ChessMove best_move = g_array_index(moves, ChessMove, 0);

    for (guint i = 0; i < moves->len; i++) {
        ChessMove m = g_array_index(moves, ChessMove, i);
        gint temp[8][8];
        for (gint r = 0; r < 8; r++) {
            for (gint c = 0; c < 8; c++) temp[r][c] = g->board[r][c];
        }
        temp[m.to_r][m.to_c] = temp[m.from_r][m.from_c];
        temp[m.from_r][m.from_c] = 0;

        gint score = evaluate_board(temp);
        /* Black wants to minimize score */
        if (score < best_score) {
            best_score = score;
            best_move = m;
        }
    }
    g_array_unref(moves);

    execute_move(g, best_move.from_r, best_move.from_c, best_move.to_r, best_move.to_c);
}

static const gchar* chess_piece_unicode(gint p) {
    switch (p) {
        case 1:  return "♙";
        case 2:  return "♘";
        case 3:  return "♗";
        case 4:  return "♖";
        case 5:  return "♕";
        case 6:  return "♔";
        case -1: return "♟";
        case -2: return "♞";
        case -3: return "♝";
        case -4: return "♜";
        case -5: return "♛";
        case -6: return "♚";
        default: return "";
    }
}

static void chess_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    ChessGame* g = (ChessGame*)user_data;
    if (g == NULL) return;

    double bg_r, bg_g, bg_b;
    palette_rgb(PAL_BG_VIEW, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    gfloat size = (width < height) ? (width * 0.9f) : (height * 0.85f);
    gfloat sq = size / 8.0f;
    gfloat ox = (width - size) * 0.5f;
    gfloat oy = (height - size) * 0.5f;

    double light_r, light_g, light_b;
    palette_rgb(PAL_BG_RAISED, &light_r, &light_g, &light_b);

    double dark_r, dark_g, dark_b;
    palette_rgb(PAL_BG_BLOCK, &dark_r, &dark_g, &dark_b);

    double sel_r, sel_g, sel_b;
    palette_rgb(PAL_BG_SELECTED, &sel_r, &sel_g, &sel_b);

    double check_r, check_g, check_b;
    palette_rgb(PAL_ERROR, &check_r, &check_g, &check_b);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font_desc = pango_font_description_from_string(fonts_family(FONT_UI));
    pango_font_description_set_size(font_desc, (gint)(sq * 0.65f * PANGO_SCALE));
    pango_layout_set_font_description(layout, font_desc);

    /* Draw Board */
    for (gint r = 0; r < 8; r++) {
        for (gint c = 0; c < 8; c++) {
            gfloat x = ox + c * sq;
            gfloat y = oy + r * sq;
            gboolean is_light = ((r + c) % 2 == 0);

            if (r == g->sel_r && c == g->sel_c) {
                cairo_set_source_rgb(cr, sel_r, sel_g, sel_b);
            } else if (g->in_check && g->board[r][c] == g->turn * PIECE_KING) {
                cairo_set_source_rgba(cr, check_r, check_g, check_b, 0.7);
            } else if (is_light) {
                cairo_set_source_rgb(cr, light_r, light_g, light_b);
            } else {
                cairo_set_source_rgb(cr, dark_r, dark_g, dark_b);
            }
            cairo_rectangle(cr, x, y, sq, sq);
            cairo_fill(cr);

            /* Keyboard cursor highlight */
            if (r == g->cur_r && c == g->cur_c) {
                cairo_set_source_rgb(cr, sel_r, sel_g, sel_b);
                cairo_set_line_width(cr, 3.0);
                cairo_rectangle(cr, x + 1.5f, y + 1.5f, sq - 3.0f, sq - 3.0f);
                cairo_stroke(cr);
            }

            /* Legal destination move dot */
            if (g->sel_r >= 0 && is_legal_move(g, g->sel_r, g->sel_c, r, c)) {
                cairo_set_source_rgba(cr, sel_r, sel_g, sel_b, 0.6);
                cairo_arc(cr, x + sq * 0.5f, y + sq * 0.5f, sq * 0.16f, 0, 2 * M_PI);
                cairo_fill(cr);
            }

            /* Draw piece glyph */
            gint p = g->board[r][c];
            if (p != 0) {
                const gchar* glyph = chess_piece_unicode(p);
                pango_layout_set_text(layout, glyph, -1);
                int pw, ph;
                pango_layout_get_pixel_size(layout, &pw, &ph);

                if (p > 0) {
                    cairo_set_source_rgb(cr, 0.98, 0.98, 0.98); /* White piece */
                } else {
                    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12); /* Black piece */
                }
                cairo_move_to(cr, x + (sq - pw) * 0.5f, y + (sq - ph) * 0.5f);
                pango_cairo_show_layout(cr, layout);
            }
        }
    }

    pango_font_description_free(font_desc);
    g_object_unref(layout);
}

static void on_chess_click(GtkGestureClick* gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    ChessGame* g = (ChessGame*)data;
    if (g == NULL || g->game_over) return;
    if (g->vs_ai && g->turn < 0) return;

    GtkWidget* w = g->drawing_area;
    int width = gtk_widget_get_width(w);
    int height = gtk_widget_get_height(w);

    gfloat size = (width < height) ? (width * 0.9f) : (height * 0.85f);
    gfloat sq = size / 8.0f;
    gfloat ox = (width - size) * 0.5f;
    gfloat oy = (height - size) * 0.5f;

    gint c = (gint)((x - ox) / sq);
    gint r = (gint)((y - oy) / sq);

    if (r < 0 || r >= 8 || c < 0 || c >= 8) return;

    g->cur_r = r;
    g->cur_c = c;

    if (g->sel_r < 0) {
        if (g->board[r][c] * g->turn > 0) {
            g->sel_r = r;
            g->sel_c = c;
        }
    } else {
        if (is_legal_move(g, g->sel_r, g->sel_c, r, c)) {
            execute_move(g, g->sel_r, g->sel_c, r, c);
            if (g->vs_ai && !g->game_over && g->turn < 0) {
                chess_ai_make_move(g);
            }
        } else if (g->board[r][c] * g->turn > 0) {
            g->sel_r = r;
            g->sel_c = c;
        } else {
            g->sel_r = -1;
            g->sel_c = -1;
        }
    }

    gtk_label_set_text(GTK_LABEL(g->status_label), g->message);
    gtk_widget_queue_draw(g->drawing_area);
}

static gboolean on_chess_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    ChessGame* g = (ChessGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE;
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_h) {
        if (g->cur_c > 0) g->cur_c--;
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_l) {
        if (g->cur_c < 7) g->cur_c++;
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_k) {
        if (g->cur_r > 0) g->cur_r--;
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j) {
        if (g->cur_r < 7) g->cur_r++;
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_Return || keyval == GDK_KEY_space) {
        if (g->sel_r < 0) {
            if (g->board[g->cur_r][g->cur_c] * g->turn > 0) {
                g->sel_r = g->cur_r;
                g->sel_c = g->cur_c;
            }
        } else {
            if (is_legal_move(g, g->sel_r, g->sel_c, g->cur_r, g->cur_c)) {
                execute_move(g, g->sel_r, g->sel_c, g->cur_r, g->cur_c);
                if (g->vs_ai && !g->game_over && g->turn < 0) {
                    chess_ai_make_move(g);
                }
            } else {
                g->sel_r = -1;
                g->sel_c = -1;
            }
        }
        gtk_label_set_text(GTK_LABEL(g->status_label), g->message);
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        chess_init_board(g);
        gtk_label_set_text(GTK_LABEL(g->status_label), g->message);
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    }
    return FALSE;
}

static void on_chess_restart_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    ChessGame* g = (ChessGame*)data;
    if (g == NULL) return;
    chess_init_board(g);
    gtk_label_set_text(GTK_LABEL(g->status_label), g->message);
    gtk_widget_queue_draw(g->drawing_area);
    gtk_widget_grab_focus(g->drawing_area);
}

static void on_chess_mode_clicked(GtkButton* btn, gpointer data) {
    ChessGame* g = (ChessGame*)data;
    if (g == NULL) return;
    g->vs_ai = !g->vs_ai;
    gtk_button_set_label(btn, g->vs_ai ? "Mode: vs AI" : "Mode: 2 Player");
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_chess_create_tab(AppData* app) {
    ChessGame* g = g_new0(ChessGame, 1);
    g->app = app;
    g->vs_ai = TRUE;
    chess_init_board(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>CHESS</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->status_label = gtk_label_new("White to move");
    gtk_widget_set_hexpand(g->status_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->status_label);

    g->mode_btn = gtk_button_new_with_label("Mode: vs AI");
    gtk_widget_set_focusable(g->mode_btn, FALSE);
    g_signal_connect(g->mode_btn, "clicked", G_CALLBACK(on_chess_mode_clicked), g);
    gtk_box_append(GTK_BOX(header), g->mode_btn);

    GtkWidget* restart_btn = gtk_button_new_with_label("New Game (R)");
    gtk_widget_set_focusable(restart_btn, FALSE);
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_chess_restart_clicked), g);
    gtk_box_append(GTK_BOX(header), restart_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), chess_draw_func, g, NULL);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_chess_click), g);
    gtk_widget_add_controller(g->drawing_area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_chess_key_press), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Footer */
    GtkWidget* footer = gtk_label_new("Click or Arrows to select piece and target square  |  Space/Enter: Select/Move  |  R: New Game");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g_object_set_data_full(G_OBJECT(box), "chess-data", g, g_free);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

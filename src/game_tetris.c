/* Tetris game for LookinFiles */
#include "game_tetris.h"
#include "theme/palette.h"
#include "theme/fonts.h"
#include "theme/icons.h"
#include "core/app_events.h"
#include <stdlib.h>

#define TET_COLS 10
#define TET_ROWS 20

typedef struct {
    AppData* app;
    GtkWidget* drawing_area;
    GtkWidget* score_label;
    GtkWidget* status_label;
    guint timer_id;
    gboolean paused;
    gboolean game_over;
    gint score;
    gint high_score;
    gint lines;
    gint level;

    /* Board: 0 = empty, 1..7 = piece color index */
    gint board[TET_ROWS][TET_COLS];

    /* Active piece */
    gint piece_type; /* 1..7 */
    gint piece_rot;  /* 0..3 */
    gint piece_x;
    gint piece_y;

    /* Next & Hold */
    gint next_piece;
    gint hold_piece;
    gboolean can_hold;
} TetrisGame;

/* 7 standard Tetrominoes (4x4 rotation matrices, 1..7) */
static const gint TET_SHAPES[7][4][4][4] = {
    /* 1: I */
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    /* 2: J */
    {
        {{2,0,0,0},{2,2,2,0},{0,0,0,0},{0,0,0,0}},
        {{0,2,2,0},{0,2,0,0},{0,2,0,0},{0,0,0,0}},
        {{0,0,0,0},{2,2,2,0},{0,0,2,0},{0,0,0,0}},
        {{0,2,0,0},{0,2,0,0},{2,2,0,0},{0,0,0,0}}
    },
    /* 3: L */
    {
        {{0,0,3,0},{3,3,3,0},{0,0,0,0},{0,0,0,0}},
        {{0,3,0,0},{0,3,0,0},{0,3,3,0},{0,0,0,0}},
        {{0,0,0,0},{3,3,3,0},{3,0,0,0},{0,0,0,0}},
        {{3,3,0,0},{0,3,0,0},{0,3,0,0},{0,0,0,0}}
    },
    /* 4: O */
    {
        {{0,4,4,0},{0,4,4,0},{0,0,0,0},{0,0,0,0}},
        {{0,4,4,0},{0,4,4,0},{0,0,0,0},{0,0,0,0}},
        {{0,4,4,0},{0,4,4,0},{0,0,0,0},{0,0,0,0}},
        {{0,4,4,0},{0,4,4,0},{0,0,0,0},{0,0,0,0}}
    },
    /* 5: S */
    {
        {{0,5,5,0},{5,5,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,5,0,0},{0,5,5,0},{0,0,5,0},{0,0,0,0}},
        {{0,0,0,0},{0,5,5,0},{5,5,0,0},{0,0,0,0}},
        {{5,0,0,0},{5,5,0,0},{0,5,0,0},{0,0,0,0}}
    },
    /* 6: T */
    {
        {{0,6,0,0},{6,6,6,0},{0,0,0,0},{0,0,0,0}},
        {{0,6,0,0},{0,6,6,0},{0,6,0,0},{0,0,0,0}},
        {{0,0,0,0},{6,6,6,0},{0,6,0,0},{0,0,0,0}},
        {{0,6,0,0},{6,6,0,0},{0,6,0,0},{0,0,0,0}}
    },
    /* 7: Z */
    {
        {{7,7,0,0},{0,7,7,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,7,0},{0,7,7,0},{0,7,0,0},{0,0,0,0}},
        {{0,0,0,0},{7,7,0,0},{0,7,7,0},{0,0,0,0}},
        {{0,7,0,0},{7,7,0,0},{7,0,0,0},{0,0,0,0}}
    }
};

static PaletteRole tet_color_role(gint type) {
    switch (type) {
        case 1: return PAL_LINK;         /* I: Cyan */
        case 2: return PAL_ACCENT;       /* J: Blue */
        case 3: return PAL_ICON_ORANGE;  /* L: Orange */
        case 4: return PAL_ICON_YELLOW;  /* O: Yellow */
        case 5: return PAL_ICON_GREEN;   /* S: Green */
        case 6: return PAL_ICON_PURPLE;  /* T: Purple */
        case 7: return PAL_ICON_RED;     /* Z: Red */
        default: return PAL_FG_MUTED;
    }
}

static gboolean tet_fits(TetrisGame* g, gint type, gint rot, gint px, gint py) {
    if (type < 1 || type > 7) return FALSE;
    for (gint r = 0; r < 4; r++) {
        for (gint c = 0; c < 4; c++) {
            if (TET_SHAPES[type - 1][rot][r][c] != 0) {
                gint bx = px + c;
                gint by = py + r;
                if (bx < 0 || bx >= TET_COLS || by >= TET_ROWS) return FALSE;
                if (by >= 0 && g->board[by][bx] != 0) return FALSE;
            }
        }
    }
    return TRUE;
}

static void tet_spawn_piece(TetrisGame* g) {
    g->piece_type = g->next_piece;
    g->next_piece = 1 + (rand() % 7);
    g->piece_rot = 0;
    g->piece_x = (TET_COLS - 4) / 2;
    g->piece_y = 0;
    g->can_hold = TRUE;

    if (!tet_fits(g, g->piece_type, g->piece_rot, g->piece_x, g->piece_y)) {
        g->game_over = TRUE;
    }
}

static void tet_init_game(TetrisGame* g) {
    for (gint r = 0; r < TET_ROWS; r++) {
        for (gint c = 0; c < TET_COLS; c++) {
            g->board[r][c] = 0;
        }
    }
    g->score = 0;
    g->lines = 0;
    g->level = 1;
    g->game_over = FALSE;
    g->paused = FALSE;
    g->hold_piece = 0;
    g->can_hold = TRUE;
    g->next_piece = 1 + (rand() % 7);
    tet_spawn_piece(g);
}

static void tet_lock_piece(TetrisGame* g) {
    for (gint r = 0; r < 4; r++) {
        for (gint c = 0; c < 4; c++) {
            if (TET_SHAPES[g->piece_type - 1][g->piece_rot][r][c] != 0) {
                gint bx = g->piece_x + c;
                gint by = g->piece_y + r;
                if (bx >= 0 && bx < TET_COLS && by >= 0 && by < TET_ROWS) {
                    g->board[by][bx] = g->piece_type;
                }
            }
        }
    }

    /* Check cleared lines */
    gint lines_cleared = 0;
    for (gint r = TET_ROWS - 1; r >= 0; r--) {
        gboolean full = TRUE;
        for (gint c = 0; c < TET_COLS; c++) {
            if (g->board[r][c] == 0) {
                full = FALSE;
                break;
            }
        }
        if (full) {
            lines_cleared++;
            for (gint kr = r; kr > 0; kr--) {
                for (gint kc = 0; kc < TET_COLS; kc++) {
                    g->board[kr][kc] = g->board[kr - 1][kc];
                }
            }
            for (gint kc = 0; kc < TET_COLS; kc++) {
                g->board[0][kc] = 0;
            }
            r++; /* check same row again */
        }
    }

    if (lines_cleared > 0) {
        gint base_pts[5] = {0, 100, 300, 500, 800};
        g->score += base_pts[lines_cleared] * g->level;
        g->lines += lines_cleared;
        g->level = 1 + (g->lines / 10);
    }

    if (g->score > g->high_score) g->high_score = g->score;
    tet_spawn_piece(g);
}

static gint tet_get_ghost_y(TetrisGame* g) {
    gint gy = g->piece_y;
    while (tet_fits(g, g->piece_type, g->piece_rot, g->piece_x, gy + 1)) {
        gy++;
    }
    return gy;
}

static gboolean tet_tick(gpointer user_data) {
    TetrisGame* g = (TetrisGame*)user_data;
    if (g == NULL || g->paused || g->game_over) return G_SOURCE_CONTINUE;
    /* Wait while the tab is not shown (another tab or workspace) */
    if (!gtk_widget_get_mapped(g->drawing_area)) return G_SOURCE_CONTINUE;

    if (tet_fits(g, g->piece_type, g->piece_rot, g->piece_x, g->piece_y + 1)) {
        g->piece_y++;
    } else {
        tet_lock_piece(g);
    }

    /* Update labels */
    gchar* s = g_strdup_printf("Score: %d   High: %d   Lines: %d   Level: %d",
                               g->score, g->high_score, g->lines, g->level);
    gtk_label_set_text(GTK_LABEL(g->score_label), s);
    g_free(s);

    if (g->game_over) {
        gtk_label_set_text(GTK_LABEL(g->status_label), "GAME OVER - Press R to Restart");
    } else if (g->paused) {
        gtk_label_set_text(GTK_LABEL(g->status_label), "PAUSED");
    } else {
        gtk_label_set_text(GTK_LABEL(g->status_label), "");
    }

    gtk_widget_queue_draw(g->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void tet_draw_block(cairo_t* cr, gfloat x, gfloat y, gfloat size, PaletteRole role, gboolean ghost) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    if (ghost) {
        cairo_set_source_rgba(cr, r, g, b, 0.35);
        cairo_rectangle(cr, x + 2, y + 2, size - 4, size - 4);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    } else {
        /* Beveled block */
        cairo_set_source_rgb(cr, r, g, b);
        cairo_rectangle(cr, x + 1, y + 1, size - 2, size - 2);
        cairo_fill(cr);

        /* Highlight */
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
        cairo_move_to(cr, x + 1, y + size - 1);
        cairo_line_to(cr, x + 1, y + 1);
        cairo_line_to(cr, x + size - 1, y + 1);
        cairo_line_to(cr, x + size - 3, y + 3);
        cairo_line_to(cr, x + 3, y + 3);
        cairo_line_to(cr, x + 3, y + size - 3);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
}

static void tet_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    TetrisGame* g = (TetrisGame*)user_data;
    if (g == NULL) return;

    double bg_r, bg_g, bg_b;
    palette_rgb(PAL_BG_VIEW, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    gfloat block = (gfloat)height / (TET_ROWS + 2);
    if (block * (TET_COLS + 10) > width) {
        block = (gfloat)width / (TET_COLS + 10);
    }

    gfloat board_w = block * TET_COLS;
    gfloat board_h = block * TET_ROWS;
    gfloat ox = (width - board_w) * 0.5f;
    gfloat oy = (height - board_h) * 0.5f;

    /* Draw Board Background & Grid */
    double block_r, block_g, block_b;
    palette_rgb(PAL_BG_BLOCK, &block_r, &block_g, &block_b);
    cairo_set_source_rgb(cr, block_r, block_g, block_b);
    cairo_rectangle(cr, ox, oy, board_w, board_h);
    cairo_fill(cr);

    double border_r, border_g, border_b;
    palette_rgb(PAL_BORDER_STRONG, &border_r, &border_g, &border_b);
    cairo_set_source_rgb(cr, border_r, border_g, border_b);
    cairo_rectangle(cr, ox, oy, board_w, board_h);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* Grid lines */
    double rule_r, rule_g, rule_b;
    palette_rgb(PAL_RULE, &rule_r, &rule_g, &rule_b);
    cairo_set_source_rgba(cr, rule_r, rule_g, rule_b, 0.4);
    cairo_set_line_width(cr, 0.5);
    for (gint c = 1; c < TET_COLS; c++) {
        cairo_move_to(cr, ox + c * block, oy);
        cairo_line_to(cr, ox + c * block, oy + board_h);
    }
    for (gint r = 1; r < TET_ROWS; r++) {
        cairo_move_to(cr, ox, oy + r * block);
        cairo_line_to(cr, ox + board_w, oy + r * block);
    }
    cairo_stroke(cr);

    /* Locked blocks */
    for (gint r = 0; r < TET_ROWS; r++) {
        for (gint c = 0; c < TET_COLS; c++) {
            gint type = g->board[r][c];
            if (type > 0) {
                tet_draw_block(cr, ox + c * block, oy + r * block, block, tet_color_role(type), FALSE);
            }
        }
    }

    if (!g->game_over && g->piece_type > 0) {
        /* Ghost piece */
        gint gy = tet_get_ghost_y(g);
        for (gint r = 0; r < 4; r++) {
            for (gint c = 0; c < 4; c++) {
                if (TET_SHAPES[g->piece_type - 1][g->piece_rot][r][c] != 0) {
                    tet_draw_block(cr, ox + (g->piece_x + c) * block, oy + (gy + r) * block,
                                   block, tet_color_role(g->piece_type), TRUE);
                }
            }
        }

        /* Active falling piece */
        for (gint r = 0; r < 4; r++) {
            for (gint c = 0; c < 4; c++) {
                if (TET_SHAPES[g->piece_type - 1][g->piece_rot][r][c] != 0) {
                    tet_draw_block(cr, ox + (g->piece_x + c) * block, oy + (g->piece_y + r) * block,
                                   block, tet_color_role(g->piece_type), FALSE);
                }
            }
        }
    }

    /* Next piece box (Right side) */
    gfloat side_box_size = block * 4;
    gfloat next_x = ox + board_w + block;
    gfloat next_y = oy + block;
    cairo_set_source_rgb(cr, block_r, block_g, block_b);
    cairo_rectangle(cr, next_x, next_y, side_box_size, side_box_size);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, border_r, border_g, border_b);
    cairo_rectangle(cr, next_x, next_y, side_box_size, side_box_size);
    cairo_stroke(cr);

    if (g->next_piece > 0) {
        for (gint r = 0; r < 4; r++) {
            for (gint c = 0; c < 4; c++) {
                if (TET_SHAPES[g->next_piece - 1][0][r][c] != 0) {
                    tet_draw_block(cr, next_x + (c + 0.5f) * block * 0.75f, next_y + (r + 0.5f) * block * 0.75f,
                                   block * 0.75f, tet_color_role(g->next_piece), FALSE);
                }
            }
        }
    }

    /* Hold piece box (Left side) */
    gfloat hold_x = ox - side_box_size - block;
    gfloat hold_y = oy + block;
    if (hold_x >= 4) {
        cairo_set_source_rgb(cr, block_r, block_g, block_b);
        cairo_rectangle(cr, hold_x, hold_y, side_box_size, side_box_size);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, border_r, border_g, border_b);
        cairo_rectangle(cr, hold_x, hold_y, side_box_size, side_box_size);
        cairo_stroke(cr);

        if (g->hold_piece > 0) {
            for (gint r = 0; r < 4; r++) {
                for (gint c = 0; c < 4; c++) {
                    if (TET_SHAPES[g->hold_piece - 1][0][r][c] != 0) {
                        tet_draw_block(cr, hold_x + (c + 0.5f) * block * 0.75f, hold_y + (r + 0.5f) * block * 0.75f,
                                       block * 0.75f, tet_color_role(g->hold_piece), FALSE);
                    }
                }
            }
        }
    }
}

static gboolean on_tet_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    TetrisGame* g = (TetrisGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE; /* Never swallow Ctrl+Q */
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_h || keyval == GDK_KEY_a || keyval == GDK_KEY_A) {
        if (!g->paused && !g->game_over && tet_fits(g, g->piece_type, g->piece_rot, g->piece_x - 1, g->piece_y)) {
            g->piece_x--;
            gtk_widget_queue_draw(g->drawing_area);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_l || keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
        if (!g->paused && !g->game_over && tet_fits(g, g->piece_type, g->piece_rot, g->piece_x + 1, g->piece_y)) {
            g->piece_x++;
            gtk_widget_queue_draw(g->drawing_area);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_k || keyval == GDK_KEY_w || keyval == GDK_KEY_W) {
        if (!g->paused && !g->game_over) {
            gint new_rot = (g->piece_rot + 1) % 4;
            /* Try wall kicks: 0, -1, +1, -2, +2 */
            gint kicks[5] = {0, -1, 1, -2, 2};
            for (gint k = 0; k < 5; k++) {
                if (tet_fits(g, g->piece_type, new_rot, g->piece_x + kicks[k], g->piece_y)) {
                    g->piece_rot = new_rot;
                    g->piece_x += kicks[k];
                    gtk_widget_queue_draw(g->drawing_area);
                    break;
                }
            }
        }
        return TRUE;
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j || keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        if (!g->paused && !g->game_over) {
            if (tet_fits(g, g->piece_type, g->piece_rot, g->piece_x, g->piece_y + 1)) {
                g->piece_y++;
                g->score += 1;
                gtk_widget_queue_draw(g->drawing_area);
            }
        }
        return TRUE;
    } else if (keyval == GDK_KEY_space) {
        if (!g->paused && !g->game_over) {
            gint gy = tet_get_ghost_y(g);
            g->score += (gy - g->piece_y) * 2;
            g->piece_y = gy;
            tet_lock_piece(g);
            gtk_widget_queue_draw(g->drawing_area);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_c || keyval == GDK_KEY_C || keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R) {
        if (!g->paused && !g->game_over && g->can_hold) {
            if (g->hold_piece == 0) {
                g->hold_piece = g->piece_type;
                tet_spawn_piece(g);
            } else {
                gint tmp = g->piece_type;
                g->piece_type = g->hold_piece;
                g->hold_piece = tmp;
                g->piece_rot = 0;
                g->piece_x = (TET_COLS - 4) / 2;
                g->piece_y = 0;
            }
            g->can_hold = FALSE;
            gtk_widget_queue_draw(g->drawing_area);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g->paused = !g->paused;
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        tet_init_game(g);
        return TRUE;
    }
    return FALSE;
}

static void on_tet_destroy(gpointer data) {
    TetrisGame* g = (TetrisGame*)data;
    if (g == NULL) return;
    if (g->timer_id > 0) {
        g_source_remove(g->timer_id);
        g->timer_id = 0;
    }
    g_free(g);
}

static void on_tet_restart_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    TetrisGame* g = (TetrisGame*)data;
    if (g == NULL) return;
    tet_init_game(g);
    gtk_widget_grab_focus(g->drawing_area);
}

static void on_tet_pause_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    TetrisGame* g = (TetrisGame*)data;
    if (g == NULL) return;
    g->paused = !g->paused;
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_tetris_create_tab(AppData* app) {
    TetrisGame* g = g_new0(TetrisGame, 1);
    g->app = app;
    g->high_score = 5000;
    tet_init_game(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>TETRIS</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->score_label = gtk_label_new("Score: 0   High: 5000   Lines: 0   Level: 1");
    gtk_box_append(GTK_BOX(header), g->score_label);

    g->status_label = gtk_label_new("");
    gtk_widget_set_hexpand(g->status_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->status_label);

    GtkWidget* pause_btn = gtk_button_new_with_label("Pause (P)");
    gtk_widget_set_focusable(pause_btn, FALSE);
    g_signal_connect(pause_btn, "clicked", G_CALLBACK(on_tet_pause_clicked), g);
    gtk_box_append(GTK_BOX(header), pause_btn);

    GtkWidget* restart_btn = gtk_button_new_with_label("Restart (R)");
    gtk_widget_set_focusable(restart_btn, FALSE);
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_tet_restart_clicked), g);
    gtk_box_append(GTK_BOX(header), restart_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), tet_draw_func, g, NULL);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_tet_key_press), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Footer controls */
    GtkWidget* footer = gtk_label_new("← / →: Move  |  ↑: Rotate  |  ↓: Soft Drop  |  Space: Hard Drop  |  C/Shift: Hold  |  P: Pause  |  R: Restart");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g->timer_id = g_timeout_add(500, tet_tick, g);
    g_object_set_data_full(G_OBJECT(box), "tetris-data", g, on_tet_destroy);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

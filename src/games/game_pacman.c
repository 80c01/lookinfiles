/* Pac-Man arcade game for LookinFiles */
#include "games/game_pacman.h"
#include "theme/palette.h"
#include "theme/fonts.h"
#include "theme/icons.h"
#include "core/app_events.h"
#include <math.h>

#define PAC_COLS 19
#define PAC_ROWS 21

typedef enum {
    PAC_DIR_NONE = 0,
    PAC_DIR_UP,
    PAC_DIR_DOWN,
    PAC_DIR_LEFT,
    PAC_DIR_RIGHT
} PacDir;

typedef enum {
    GHOST_CHASE,
    GHOST_FRIGHTENED,
    GHOST_EATEN
} GhostState;

typedef struct {
    gfloat x, y;
    PacDir dir;
    GhostState state;
    PaletteRole color_role;
    gfloat home_x, home_y;
} PacGhost;

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
    gint lives;
    gint dots_remaining;

    /* Pacman state */
    gfloat pac_x, pac_y;
    PacDir pac_dir;
    PacDir pac_next_dir;
    gfloat mouth_angle;
    gboolean mouth_opening;

    /* Ghosts */
    PacGhost ghosts[4];
    gint frightened_timer;
    gint ghost_combo;

    /* Map: '#' wall, '.' pellet, '*' energizer, ' ' empty, '=' door */
    gchar map[PAC_ROWS][PAC_COLS + 1];
} PacGame;

static const gchar* DEFAULT_PAC_MAP[PAC_ROWS] = {
    "###################",
    "#*.......#.......*#",
    "#.##.###.#.###.##.#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.### # ###.####",
    "   #.#       #.#   ",
    "####.# ##=## #.####",
    "    .  #   #  .    ",
    "####.# ##### #.####",
    "   #.#       #.#   ",
    "####.# ##### #.####",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#*..#.... .....#.*#",
    "###.#.#.#####.#.###",
    "#.....#...#...#...#",
    "#.#######.#.#######",
    "#.................#",
    "###################"
};

static void pac_init_level(PacGame* g) {
    g->dots_remaining = 0;
    for (gint r = 0; r < PAC_ROWS; r++) {
        for (gint c = 0; c < PAC_COLS; c++) {
            g->map[r][c] = DEFAULT_PAC_MAP[r][c];
            if (g->map[r][c] == '.' || g->map[r][c] == '*') {
                g->dots_remaining++;
            }
        }
        g->map[r][PAC_COLS] = '\0';
    }

    g->pac_x = 9.0f;
    g->pac_y = 15.0f;
    g->pac_dir = PAC_DIR_NONE;
    g->pac_next_dir = PAC_DIR_NONE;
    g->mouth_angle = 0.2f;
    g->mouth_opening = TRUE;

    /* Blinky (Red), Pinky (Pink), Inky (Cyan), Clyde (Orange) */
    PaletteRole roles[4] = {PAL_ICON_RED, PAL_ICON_PINK, PAL_LINK, PAL_ICON_ORANGE};
    gfloat spawn_x[4] = {9.0f, 8.0f, 9.0f, 10.0f};
    gfloat spawn_y[4] = {7.0f, 9.0f, 9.0f, 9.0f};

    for (gint i = 0; i < 4; i++) {
        g->ghosts[i].x = spawn_x[i];
        g->ghosts[i].y = spawn_y[i];
        g->ghosts[i].home_x = spawn_x[i];
        g->ghosts[i].home_y = spawn_y[i];
        g->ghosts[i].dir = (i == 0) ? PAC_DIR_LEFT : PAC_DIR_UP;
        g->ghosts[i].state = GHOST_CHASE;
        g->ghosts[i].color_role = roles[i];
    }
    g->frightened_timer = 0;
    g->ghost_combo = 0;
}

static gboolean is_wall(PacGame* g, gint x, gint y) {
    if (x < 0 || x >= PAC_COLS || y < 0 || y >= PAC_ROWS) return FALSE;
    char c = g->map[y][x];
    return (c == '#' || c == '=');
}

static gboolean can_move(PacGame* g, gfloat x, gfloat y, PacDir dir) {
    gfloat nx = x;
    gfloat ny = y;
    gfloat step = 0.5f;
    switch (dir) {
        case PAC_DIR_UP: ny -= step; break;
        case PAC_DIR_DOWN: ny += step; break;
        case PAC_DIR_LEFT: nx -= step; break;
        case PAC_DIR_RIGHT: nx += step; break;
        default: return FALSE;
    }
    gint gx = (gint)floorf(nx + 0.5f);
    gint gy = (gint)floorf(ny + 0.5f);
    if (gx < 0 || gx >= PAC_COLS) return TRUE; /* tunnel */
    if (gy < 0 || gy >= PAC_ROWS) return FALSE;
    return !is_wall(g, gx, gy);
}

static void update_ghost_dir(PacGame* g, PacGhost* gh) {
    gint gx = (gint)floorf(gh->x + 0.5f);
    gint gy = (gint)floorf(gh->y + 0.5f);
    if (fabsf(gh->x - gx) > 0.1f || fabsf(gh->y - gy) > 0.1f) return;

    PacDir dirs[4] = {PAC_DIR_UP, PAC_DIR_LEFT, PAC_DIR_DOWN, PAC_DIR_RIGHT};
    PacDir opp = PAC_DIR_NONE;
    if (gh->dir == PAC_DIR_UP) opp = PAC_DIR_DOWN;
    else if (gh->dir == PAC_DIR_DOWN) opp = PAC_DIR_UP;
    else if (gh->dir == PAC_DIR_LEFT) opp = PAC_DIR_RIGHT;
    else if (gh->dir == PAC_DIR_RIGHT) opp = PAC_DIR_LEFT;

    PacDir best_dir = gh->dir;
    gfloat best_dist = (gh->state == GHOST_FRIGHTENED) ? -1.0f : 1e9f;

    gfloat tx = g->pac_x;
    gfloat ty = g->pac_y;
    if (gh->state == GHOST_EATEN) {
        tx = gh->home_x;
        ty = gh->home_y;
    }

    for (gint i = 0; i < 4; i++) {
        PacDir d = dirs[i];
        if (d == opp) continue;
        gint nx = gx, ny = gy;
        if (d == PAC_DIR_UP) ny--;
        else if (d == PAC_DIR_DOWN) ny++;
        else if (d == PAC_DIR_LEFT) nx--;
        else if (d == PAC_DIR_RIGHT) nx++;

        if (nx < 0 || nx >= PAC_COLS || ny < 0 || ny >= PAC_ROWS || is_wall(g, nx, ny)) continue;

        gfloat dist = (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty);
        if (gh->state == GHOST_FRIGHTENED) {
            if (dist > best_dist || best_dist < 0) {
                best_dist = dist;
                best_dir = d;
            }
        } else {
            if (dist < best_dist) {
                best_dist = dist;
                best_dir = d;
            }
        }
    }
    gh->dir = best_dir;
}

static gboolean pac_tick(gpointer user_data) {
    PacGame* g = (PacGame*)user_data;
    if (g == NULL || g->paused || g->game_over) return G_SOURCE_CONTINUE;
    /* Wait while the tab is not shown (another tab or workspace) */
    if (!gtk_widget_get_mapped(g->drawing_area)) return G_SOURCE_CONTINUE;

    /* Mouth animation */
    if (g->mouth_opening) {
        g->mouth_angle += 0.05f;
        if (g->mouth_angle >= 0.40f) g->mouth_opening = FALSE;
    } else {
        g->mouth_angle -= 0.05f;
        if (g->mouth_angle <= 0.05f) g->mouth_opening = TRUE;
    }

    /* Try turning */
    if (g->pac_next_dir != PAC_DIR_NONE && can_move(g, g->pac_x, g->pac_y, g->pac_next_dir)) {
        g->pac_dir = g->pac_next_dir;
        g->pac_next_dir = PAC_DIR_NONE;
    }

    /* Move Pacman */
    gfloat speed = 0.16f;
    if (can_move(g, g->pac_x, g->pac_y, g->pac_dir)) {
        switch (g->pac_dir) {
            case PAC_DIR_UP: g->pac_y -= speed; break;
            case PAC_DIR_DOWN: g->pac_y += speed; break;
            case PAC_DIR_LEFT: g->pac_x -= speed; break;
            case PAC_DIR_RIGHT: g->pac_x += speed; break;
            default: break;
        }
    }

    /* Tunnel wrap */
    if (g->pac_x < -0.5f) g->pac_x = (gfloat)(PAC_COLS - 0.5f);
    else if (g->pac_x > (gfloat)(PAC_COLS - 0.5f)) g->pac_x = -0.5f;

    /* Eat dots */
    gint gx = (gint)floorf(g->pac_x + 0.5f);
    gint gy = (gint)floorf(g->pac_y + 0.5f);
    if (gx >= 0 && gx < PAC_COLS && gy >= 0 && gy < PAC_ROWS) {
        if (g->map[gy][gx] == '.') {
            g->map[gy][gx] = ' ';
            g->score += 10;
            g->dots_remaining--;
        } else if (g->map[gy][gx] == '*') {
            g->map[gy][gx] = ' ';
            g->score += 50;
            g->dots_remaining--;
            g->frightened_timer = 200; /* ~6 seconds at 30 FPS */
            g->ghost_combo = 0;
            for (gint i = 0; i < 4; i++) {
                if (g->ghosts[i].state != GHOST_EATEN) {
                    g->ghosts[i].state = GHOST_FRIGHTENED;
                }
            }
        }
    }

    if (g->frightened_timer > 0) {
        g->frightened_timer--;
        if (g->frightened_timer == 0) {
            for (gint i = 0; i < 4; i++) {
                if (g->ghosts[i].state == GHOST_FRIGHTENED) {
                    g->ghosts[i].state = GHOST_CHASE;
                }
            }
        }
    }

    /* Move Ghosts */
    for (gint i = 0; i < 4; i++) {
        PacGhost* gh = &g->ghosts[i];
        gfloat gspeed = (gh->state == GHOST_FRIGHTENED) ? 0.08f : (gh->state == GHOST_EATEN ? 0.24f : 0.13f);
        update_ghost_dir(g, gh);
        switch (gh->dir) {
            case PAC_DIR_UP: gh->y -= gspeed; break;
            case PAC_DIR_DOWN: gh->y += gspeed; break;
            case PAC_DIR_LEFT: gh->x -= gspeed; break;
            case PAC_DIR_RIGHT: gh->x += gspeed; break;
            default: break;
        }

        if (gh->x < -0.5f) gh->x = (gfloat)(PAC_COLS - 0.5f);
        else if (gh->x > (gfloat)(PAC_COLS - 0.5f)) gh->x = -0.5f;

        if (gh->state == GHOST_EATEN && fabsf(gh->x - gh->home_x) < 0.5f && fabsf(gh->y - gh->home_y) < 0.5f) {
            gh->state = GHOST_CHASE;
        }

        /* Collision with Pacman */
        if (fabsf(gh->x - g->pac_x) < 0.6f && fabsf(gh->y - g->pac_y) < 0.6f) {
            if (gh->state == GHOST_FRIGHTENED) {
                gh->state = GHOST_EATEN;
                g->ghost_combo++;
                g->score += (100 * (1 << g->ghost_combo));
            } else if (gh->state == GHOST_CHASE) {
                g->lives--;
                if (g->lives <= 0) {
                    g->game_over = TRUE;
                } else {
                    g->pac_x = 9.0f;
                    g->pac_y = 15.0f;
                    g->pac_dir = PAC_DIR_NONE;
                    g->pac_next_dir = PAC_DIR_NONE;
                }
            }
        }
    }

    if (g->dots_remaining <= 0) {
        g->score += 1000;
        pac_init_level(g);
    }

    if (g->score > g->high_score) g->high_score = g->score;

    /* Update labels */
    gchar* score_str = g_strdup_printf("Score: %d   High: %d   Lives: %d", g->score, g->high_score, g->lives);
    gtk_label_set_text(GTK_LABEL(g->score_label), score_str);
    g_free(score_str);

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

static void pac_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    PacGame* g = (PacGame*)user_data;
    if (g == NULL) return;

    double bg_r, bg_g, bg_b;
    palette_rgb(PAL_BG_VIEW, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    gfloat tile_w = (gfloat)width / PAC_COLS;
    gfloat tile_h = (gfloat)height / PAC_ROWS;
    gfloat tile = (tile_w < tile_h) ? tile_w : tile_h;
    gfloat ox = (width - tile * PAC_COLS) * 0.5f;
    gfloat oy = (height - tile * PAC_ROWS) * 0.5f;

    double wall_r, wall_g, wall_b;
    palette_rgb(PAL_LINK, &wall_r, &wall_g, &wall_b);

    double dot_r, dot_g, dot_b;
    palette_rgb(PAL_ICON_YELLOW, &dot_r, &dot_g, &dot_b);

    /* Draw Maze */
    for (gint r = 0; r < PAC_ROWS; r++) {
        for (gint c = 0; c < PAC_COLS; c++) {
            gfloat x = ox + c * tile;
            gfloat y = oy + r * tile;
            char ch = g->map[r][c];
            if (ch == '#') {
                cairo_set_source_rgb(cr, wall_r * 0.8, wall_g * 0.8, wall_b * 0.8);
                cairo_rectangle(cr, x + 1, y + 1, tile - 2, tile - 2);
                cairo_fill(cr);
            } else if (ch == '=') {
                cairo_set_source_rgb(cr, dot_r, dot_g, dot_b);
                cairo_rectangle(cr, x, y + tile * 0.4f, tile, tile * 0.2f);
                cairo_fill(cr);
            } else if (ch == '.') {
                cairo_set_source_rgb(cr, dot_r, dot_g, dot_b);
                cairo_arc(cr, x + tile * 0.5f, y + tile * 0.5f, tile * 0.12f, 0, 2 * M_PI);
                cairo_fill(cr);
            } else if (ch == '*') {
                cairo_set_source_rgb(cr, dot_r, dot_g, dot_b);
                cairo_arc(cr, x + tile * 0.5f, y + tile * 0.5f, tile * 0.32f, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }

    /* Draw Pac-Man */
    gfloat px = ox + g->pac_x * tile + tile * 0.5f;
    gfloat py = oy + g->pac_y * tile + tile * 0.5f;
    gfloat pr = tile * 0.45f;

    gdouble angle_offset = 0.0;
    if (g->pac_dir == PAC_DIR_RIGHT) angle_offset = 0.0;
    else if (g->pac_dir == PAC_DIR_DOWN) angle_offset = M_PI * 0.5;
    else if (g->pac_dir == PAC_DIR_LEFT) angle_offset = M_PI;
    else if (g->pac_dir == PAC_DIR_UP) angle_offset = M_PI * 1.5;

    cairo_set_source_rgb(cr, dot_r, dot_g, dot_b);
    cairo_move_to(cr, px, py);
    cairo_arc(cr, px, py, pr, angle_offset + g->mouth_angle, angle_offset + 2 * M_PI - g->mouth_angle);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Draw Ghosts */
    for (gint i = 0; i < 4; i++) {
        PacGhost* gh = &g->ghosts[i];
        gfloat gx = ox + gh->x * tile + tile * 0.5f;
        gfloat gy = oy + gh->y * tile + tile * 0.5f;
        gfloat gr = tile * 0.45f;

        if (gh->state == GHOST_EATEN) {
            /* Only eyes */
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_arc(cr, gx - gr * 0.35f, gy - gr * 0.2f, gr * 0.3f, 0, 2 * M_PI);
            cairo_arc(cr, gx + gr * 0.35f, gy - gr * 0.2f, gr * 0.3f, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.0, 0.2, 0.8);
            cairo_arc(cr, gx - gr * 0.35f, gy - gr * 0.2f, gr * 0.15f, 0, 2 * M_PI);
            cairo_arc(cr, gx + gr * 0.35f, gy - gr * 0.2f, gr * 0.15f, 0, 2 * M_PI);
            cairo_fill(cr);
            continue;
        }

        double gr_r, gr_g, gr_b;
        if (gh->state == GHOST_FRIGHTENED) {
            if (g->frightened_timer < 50 && (g->frightened_timer / 8) % 2 == 0) {
                palette_rgb(PAL_FG_BRIGHT, &gr_r, &gr_g, &gr_b);
            } else {
                palette_rgb(PAL_LINK, &gr_r, &gr_g, &gr_b);
            }
        } else {
            palette_rgb(gh->color_role, &gr_r, &gr_g, &gr_b);
        }

        cairo_set_source_rgb(cr, gr_r, gr_g, gr_b);
        cairo_arc(cr, gx, gy - gr * 0.1f, gr, M_PI, 2 * M_PI);
        cairo_line_to(cr, gx + gr, gy + gr);
        /* Wavy bottom tentacles */
        cairo_line_to(cr, gx + gr * 0.5f, gy + gr * 0.7f);
        cairo_line_to(cr, gx, gy + gr);
        cairo_line_to(cr, gx - gr * 0.5f, gy + gr * 0.7f);
        cairo_line_to(cr, gx - gr, gy + gr);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Eyes */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_arc(cr, gx - gr * 0.35f, gy - gr * 0.2f, gr * 0.28f, 0, 2 * M_PI);
        cairo_arc(cr, gx + gr * 0.35f, gy - gr * 0.2f, gr * 0.28f, 0, 2 * M_PI);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.1, 0.1, 0.8);
        gfloat ex = (gh->dir == PAC_DIR_LEFT) ? -0.1f : ((gh->dir == PAC_DIR_RIGHT) ? 0.1f : 0.0f);
        gfloat ey = (gh->dir == PAC_DIR_UP) ? -0.1f : ((gh->dir == PAC_DIR_DOWN) ? 0.1f : 0.0f);
        cairo_arc(cr, gx - gr * 0.35f + ex * gr, gy - gr * 0.2f + ey * gr, gr * 0.14f, 0, 2 * M_PI);
        cairo_arc(cr, gx + gr * 0.35f + ex * gr, gy - gr * 0.2f + ey * gr, gr * 0.14f, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

static gboolean on_pac_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    PacGame* g = (PacGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE; /* Ctrl+Q quit */
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_w || keyval == GDK_KEY_W || keyval == GDK_KEY_k) {
        g->pac_next_dir = PAC_DIR_UP;
        return TRUE;
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_s || keyval == GDK_KEY_S || keyval == GDK_KEY_j) {
        g->pac_next_dir = PAC_DIR_DOWN;
        return TRUE;
    } else if (keyval == GDK_KEY_Left || keyval == GDK_KEY_a || keyval == GDK_KEY_A || keyval == GDK_KEY_h) {
        g->pac_next_dir = PAC_DIR_LEFT;
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_d || keyval == GDK_KEY_D || keyval == GDK_KEY_l) {
        g->pac_next_dir = PAC_DIR_RIGHT;
        return TRUE;
    } else if (keyval == GDK_KEY_p || keyval == GDK_KEY_P || keyval == GDK_KEY_space) {
        g->paused = !g->paused;
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        g->score = 0;
        g->lives = 3;
        g->game_over = FALSE;
        g->paused = FALSE;
        pac_init_level(g);
        return TRUE;
    }
    return FALSE;
}

static void on_pac_destroy(gpointer data) {
    PacGame* g = (PacGame*)data;
    if (g == NULL) return;
    if (g->timer_id > 0) {
        g_source_remove(g->timer_id);
        g->timer_id = 0;
    }
    g_free(g);
}

static void on_pac_restart_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    PacGame* g = (PacGame*)data;
    if (g == NULL) return;
    g->score = 0;
    g->lives = 3;
    g->game_over = FALSE;
    g->paused = FALSE;
    pac_init_level(g);
    gtk_widget_grab_focus(g->drawing_area);
}

static void on_pac_pause_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    PacGame* g = (PacGame*)data;
    if (g == NULL) return;
    g->paused = !g->paused;
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_pacman_create_tab(AppData* app) {
    PacGame* g = g_new0(PacGame, 1);
    g->app = app;
    g->lives = 3;
    g->high_score = 1000;
    pac_init_level(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header controls bar */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>PAC-MAN</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->score_label = gtk_label_new("Score: 0   High: 1000   Lives: 3");
    gtk_box_append(GTK_BOX(header), g->score_label);

    g->status_label = gtk_label_new("");
    gtk_widget_set_hexpand(g->status_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->status_label);

    GtkWidget* pause_btn = gtk_button_new_with_label("Pause (P)");
    gtk_widget_set_focusable(pause_btn, FALSE);
    g_signal_connect(pause_btn, "clicked", G_CALLBACK(on_pac_pause_clicked), g);
    gtk_box_append(GTK_BOX(header), pause_btn);

    GtkWidget* restart_btn = gtk_button_new_with_label("Restart (R)");
    gtk_widget_set_focusable(restart_btn, FALSE);
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_pac_restart_clicked), g);
    gtk_box_append(GTK_BOX(header), restart_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), pac_draw_func, g, NULL);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_pac_key_press), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Bottom instructions */
    GtkWidget* footer = gtk_label_new("Controls: Arrow Keys / WASD to Move  |  P: Pause  |  R: Restart  |  Ctrl+W: Close Tab");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g->timer_id = g_timeout_add(33, pac_tick, g);
    g_object_set_data_full(G_OBJECT(box), "pac-data", g, on_pac_destroy);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

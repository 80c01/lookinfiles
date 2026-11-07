/* Space Commander (Space Invaders) arcade game for LookinFiles */
#include "game_space.h"
#include "palette.h"
#include "fonts.h"
#include "icons.h"
#include "app_events.h"
#include <stdlib.h>
#include <math.h>

#define SPACE_WIDTH  640.0f
#define SPACE_HEIGHT 480.0f
#define ALIEN_ROWS 5
#define ALIEN_COLS 8

typedef struct {
    gfloat x, y;
    gboolean active;
} Laser;

typedef struct {
    gfloat x, y;
    gboolean alive;
    gint row;
} Alien;

typedef struct {
    gfloat x;
    gint health[3][4]; /* 3x4 block grid per bunker */
} Bunker;

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
    gint wave;

    /* Player */
    gfloat ship_x;
    gboolean key_left;
    gboolean key_right;

    /* Lasers & Bombs */
    Laser player_lasers[3];
    Laser alien_bombs[6];

    /* Aliens */
    Alien aliens[ALIEN_ROWS][ALIEN_COLS];
    gfloat fleet_dir; /* +1 or -1 */
    gfloat fleet_speed;
    gint anim_frame;
    gint aliens_left;

    /* UFO */
    gfloat ufo_x;
    gboolean ufo_active;

    /* Bunkers */
    Bunker bunkers[4];

    /* Starfield */
    gfloat stars_x[40];
    gfloat stars_y[40];
} SpaceGame;

static void space_init_fleet(SpaceGame* g) {
    g->aliens_left = ALIEN_ROWS * ALIEN_COLS;
    g->fleet_dir = 1.0f;
    g->fleet_speed = 1.0f + (g->wave - 1) * 0.3f;
    g->anim_frame = 0;

    for (gint r = 0; r < ALIEN_ROWS; r++) {
        for (gint c = 0; c < ALIEN_COLS; c++) {
            g->aliens[r][c].x = 100.0f + c * 50.0f;
            g->aliens[r][c].y = 70.0f + r * 35.0f;
            g->aliens[r][c].alive = TRUE;
            g->aliens[r][c].row = r;
        }
    }

    /* Bunkers */
    for (gint b = 0; b < 4; b++) {
        g->bunkers[b].x = 80.0f + b * 135.0f;
        for (gint r = 0; r < 3; r++) {
            for (gint c = 0; c < 4; c++) {
                g->bunkers[b].health[r][c] = (r == 2 && (c == 1 || c == 2)) ? 0 : 3;
            }
        }
    }

    g->ufo_active = FALSE;
    g->ufo_x = -50.0f;
}

static void space_init_game(SpaceGame* g) {
    g->score = 0;
    g->lives = 3;
    g->wave = 1;
    g->ship_x = SPACE_WIDTH * 0.5f;
    g->game_over = FALSE;
    g->paused = FALSE;
    g->key_left = FALSE;
    g->key_right = FALSE;

    for (gint i = 0; i < 3; i++) g->player_lasers[i].active = FALSE;
    for (gint i = 0; i < 6; i++) g->alien_bombs[i].active = FALSE;

    for (gint i = 0; i < 40; i++) {
        g->stars_x[i] = (gfloat)(rand() % (gint)SPACE_WIDTH);
        g->stars_y[i] = (gfloat)(rand() % (gint)SPACE_HEIGHT);
    }

    space_init_fleet(g);
}

static gboolean space_tick(gpointer user_data) {
    SpaceGame* g = (SpaceGame*)user_data;
    if (g == NULL || g->paused || g->game_over) return G_SOURCE_CONTINUE;
    /* Wait while the tab is not shown (another tab or workspace) */
    if (!gtk_widget_get_mapped(g->drawing_area)) return G_SOURCE_CONTINUE;

    /* Move stars */
    for (gint i = 0; i < 40; i++) {
        g->stars_y[i] += 0.5f;
        if (g->stars_y[i] > SPACE_HEIGHT) {
            g->stars_y[i] = 0;
            g->stars_x[i] = (gfloat)(rand() % (gint)SPACE_WIDTH);
        }
    }

    /* Move Player */
    if (g->key_left && g->ship_x > 30.0f) g->ship_x -= 5.0f;
    if (g->key_right && g->ship_x < SPACE_WIDTH - 30.0f) g->ship_x += 5.0f;

    /* Move Lasers */
    for (gint i = 0; i < 3; i++) {
        if (g->player_lasers[i].active) {
            g->player_lasers[i].y -= 9.0f;
            if (g->player_lasers[i].y < 0) {
                g->player_lasers[i].active = FALSE;
                continue;
            }

            /* Laser vs Aliens */
            for (gint r = 0; r < ALIEN_ROWS; r++) {
                for (gint c = 0; c < ALIEN_COLS; c++) {
                    Alien* a = &g->aliens[r][c];
                    if (a->alive && fabsf(g->player_lasers[i].x - a->x) < 18.0f &&
                        fabsf(g->player_lasers[i].y - a->y) < 14.0f) {
                        a->alive = FALSE;
                        g->player_lasers[i].active = FALSE;
                        g->aliens_left--;
                        gint pts = (r == 0) ? 30 : ((r <= 2) ? 20 : 10);
                        g->score += pts;
                        break;
                    }
                }
                if (!g->player_lasers[i].active) break;
            }

            /* Laser vs UFO */
            if (g->player_lasers[i].active && g->ufo_active &&
                fabsf(g->player_lasers[i].x - g->ufo_x) < 25.0f &&
                fabsf(g->player_lasers[i].y - 35.0f) < 12.0f) {
                g->ufo_active = FALSE;
                g->player_lasers[i].active = FALSE;
                g->score += (100 + (rand() % 4) * 50);
            }

            /* Laser vs Bunkers */
            if (g->player_lasers[i].active) {
                for (gint b = 0; b < 4; b++) {
                    if (fabsf(g->player_lasers[i].x - (g->bunkers[b].x + 24.0f)) < 30.0f &&
                        g->player_lasers[i].y >= 380.0f && g->player_lasers[i].y <= 410.0f) {
                        gint bc = (gint)((g->player_lasers[i].x - g->bunkers[b].x) / 12.0f);
                        gint br = (gint)((g->player_lasers[i].y - 380.0f) / 10.0f);
                        if (bc >= 0 && bc < 4 && br >= 0 && br < 3 && g->bunkers[b].health[br][bc] > 0) {
                            g->bunkers[b].health[br][bc]--;
                            g->player_lasers[i].active = FALSE;
                            break;
                        }
                    }
                }
            }
        }
    }

    /* Move Fleet */
    gfloat speed = g->fleet_speed * (1.0f + (40 - g->aliens_left) * 0.05f);
    gboolean hit_edge = FALSE;

    for (gint r = 0; r < ALIEN_ROWS; r++) {
        for (gint c = 0; c < ALIEN_COLS; c++) {
            if (g->aliens[r][c].alive) {
                g->aliens[r][c].x += g->fleet_dir * speed;
                if ((g->aliens[r][c].x > SPACE_WIDTH - 30.0f && g->fleet_dir > 0) ||
                    (g->aliens[r][c].x < 30.0f && g->fleet_dir < 0)) {
                    hit_edge = TRUE;
                }
                if (g->aliens[r][c].y >= 430.0f) {
                    g->game_over = TRUE;
                }
            }
        }
    }

    if (hit_edge) {
        g->fleet_dir = -g->fleet_dir;
        for (gint r = 0; r < ALIEN_ROWS; r++) {
            for (gint c = 0; c < ALIEN_COLS; c++) {
                g->aliens[r][c].y += 12.0f;
            }
        }
    }

    /* Alien Bombs */
    for (gint i = 0; i < 6; i++) {
        if (!g->alien_bombs[i].active && (rand() % 120 == 0) && g->aliens_left > 0) {
            gint c = rand() % ALIEN_COLS;
            for (gint r = ALIEN_ROWS - 1; r >= 0; r--) {
                if (g->aliens[r][c].alive) {
                    g->alien_bombs[i].x = g->aliens[r][c].x;
                    g->alien_bombs[i].y = g->aliens[r][c].y + 12.0f;
                    g->alien_bombs[i].active = TRUE;
                    break;
                }
            }
        }

        if (g->alien_bombs[i].active) {
            g->alien_bombs[i].y += 4.5f;
            if (g->alien_bombs[i].y > SPACE_HEIGHT) {
                g->alien_bombs[i].active = FALSE;
                continue;
            }

            /* Bomb vs Player */
            if (fabsf(g->alien_bombs[i].x - g->ship_x) < 18.0f &&
                fabsf(g->alien_bombs[i].y - 440.0f) < 12.0f) {
                g->alien_bombs[i].active = FALSE;
                g->lives--;
                if (g->lives <= 0) {
                    g->game_over = TRUE;
                } else {
                    g->ship_x = SPACE_WIDTH * 0.5f;
                }
            }

            /* Bomb vs Bunkers */
            for (gint b = 0; b < 4; b++) {
                if (g->alien_bombs[i].active &&
                    fabsf(g->alien_bombs[i].x - (g->bunkers[b].x + 24.0f)) < 30.0f &&
                    g->alien_bombs[i].y >= 380.0f && g->alien_bombs[i].y <= 410.0f) {
                    gint bc = (gint)((g->alien_bombs[i].x - g->bunkers[b].x) / 12.0f);
                    gint br = (gint)((g->alien_bombs[i].y - 380.0f) / 10.0f);
                    if (bc >= 0 && bc < 4 && br >= 0 && br < 3 && g->bunkers[b].health[br][bc] > 0) {
                        g->bunkers[b].health[br][bc]--;
                        g->alien_bombs[i].active = FALSE;
                        break;
                    }
                }
            }
        }
    }

    /* UFO */
    if (!g->ufo_active && rand() % 500 == 0) {
        g->ufo_active = TRUE;
        g->ufo_x = -40.0f;
    }
    if (g->ufo_active) {
        g->ufo_x += 2.5f;
        if (g->ufo_x > SPACE_WIDTH + 40.0f) g->ufo_active = FALSE;
    }

    /* Next Wave */
    if (g->aliens_left <= 0) {
        g->wave++;
        g->score += 500;
        space_init_fleet(g);
    }

    if (g->score > g->high_score) g->high_score = g->score;

    /* Update labels */
    gchar* s = g_strdup_printf("Score: %d   High: %d   Lives: %d   Wave: %d",
                               g->score, g->high_score, g->lives, g->wave);
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

static void space_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    SpaceGame* g = (SpaceGame*)user_data;
    if (g == NULL) return;

    double bg_r, bg_g, bg_b;
    palette_rgb(PAL_BG_VIEW, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    gfloat sx = (gfloat)width / SPACE_WIDTH;
    gfloat sy = (gfloat)height / SPACE_HEIGHT;
    gfloat scale = (sx < sy) ? sx : sy;
    gfloat ox = (width - SPACE_WIDTH * scale) * 0.5f;
    gfloat oy = (height - SPACE_HEIGHT * scale) * 0.5f;

    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);

    /* Starfield */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
    for (gint i = 0; i < 40; i++) {
        cairo_arc(cr, g->stars_x[i], g->stars_y[i], 1.2, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    /* Player Ship */
    double ship_r, ship_g, ship_b;
    palette_rgb(PAL_SUCCESS, &ship_r, &ship_g, &ship_b);
    cairo_set_source_rgb(cr, ship_r, ship_g, ship_b);
    cairo_move_to(cr, g->ship_x, 430.0f);
    cairo_line_to(cr, g->ship_x - 16.0f, 448.0f);
    cairo_line_to(cr, g->ship_x + 16.0f, 448.0f);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Player Lasers */
    double laser_r, laser_g, laser_b;
    palette_rgb(PAL_LINK, &laser_r, &laser_g, &laser_b);
    cairo_set_source_rgb(cr, laser_r, laser_g, laser_b);
    for (gint i = 0; i < 3; i++) {
        if (g->player_lasers[i].active) {
            cairo_rectangle(cr, g->player_lasers[i].x - 1.5f, g->player_lasers[i].y, 3.0f, 10.0f);
            cairo_fill(cr);
        }
    }

    /* Aliens */
    for (gint r = 0; r < ALIEN_ROWS; r++) {
        PaletteRole arole = (r == 0) ? PAL_ICON_RED : ((r <= 2) ? PAL_ICON_PURPLE : PAL_ICON_GREEN);
        double ar_r, ar_g, ar_b;
        palette_rgb(arole, &ar_r, &ar_g, &ar_b);
        cairo_set_source_rgb(cr, ar_r, ar_g, ar_b);

        for (gint c = 0; c < ALIEN_COLS; c++) {
            Alien* a = &g->aliens[r][c];
            if (a->alive) {
                cairo_arc(cr, a->x, a->y, 10.0f, 0, 2 * M_PI);
                cairo_fill(cr);
                /* Eyes */
                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_arc(cr, a->x - 4.0f, a->y - 2.0f, 2.0f, 0, 2 * M_PI);
                cairo_arc(cr, a->x + 4.0f, a->y - 2.0f, 2.0f, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, ar_r, ar_g, ar_b);
            }
        }
    }

    /* Alien Bombs */
    double bomb_r, bomb_g, bomb_b;
    palette_rgb(PAL_ICON_YELLOW, &bomb_r, &bomb_g, &bomb_b);
    cairo_set_source_rgb(cr, bomb_r, bomb_g, bomb_b);
    for (gint i = 0; i < 6; i++) {
        if (g->alien_bombs[i].active) {
            cairo_rectangle(cr, g->alien_bombs[i].x - 1.5f, g->alien_bombs[i].y, 3.0f, 8.0f);
            cairo_fill(cr);
        }
    }

    /* UFO */
    if (g->ufo_active) {
        double ufo_r, ufo_g, ufo_b;
        palette_rgb(PAL_ICON_ORANGE, &ufo_r, &ufo_g, &ufo_b);
        cairo_set_source_rgb(cr, ufo_r, ufo_g, ufo_b);
        cairo_arc(cr, g->ufo_x, 35.0f, 16.0f, M_PI, 2 * M_PI);
        cairo_rectangle(cr, g->ufo_x - 22.0f, 35.0f, 44.0f, 6.0f);
        cairo_fill(cr);
    }

    /* Bunkers */
    double bun_r, bun_g, bun_b;
    palette_rgb(PAL_SUCCESS, &bun_r, &bun_g, &bun_b);
    for (gint b = 0; b < 4; b++) {
        for (gint r = 0; r < 3; r++) {
            for (gint c = 0; c < 4; c++) {
                gint hp = g->bunkers[b].health[r][c];
                if (hp > 0) {
                    cairo_set_source_rgba(cr, bun_r, bun_g, bun_b, hp / 3.0f);
                    cairo_rectangle(cr, g->bunkers[b].x + c * 12.0f, 380.0f + r * 10.0f, 11.0f, 9.0f);
                    cairo_fill(cr);
                }
            }
        }
    }

    cairo_restore(cr);
}

static gboolean on_space_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    SpaceGame* g = (SpaceGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE;
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_a || keyval == GDK_KEY_A || keyval == GDK_KEY_h) {
        g->key_left = TRUE;
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_d || keyval == GDK_KEY_D || keyval == GDK_KEY_l) {
        g->key_right = TRUE;
        return TRUE;
    } else if (keyval == GDK_KEY_space) {
        if (!g->paused && !g->game_over) {
            for (gint i = 0; i < 3; i++) {
                if (!g->player_lasers[i].active) {
                    g->player_lasers[i].x = g->ship_x;
                    g->player_lasers[i].y = 425.0f;
                    g->player_lasers[i].active = TRUE;
                    break;
                }
            }
        }
        return TRUE;
    } else if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g->paused = !g->paused;
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        space_init_game(g);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_space_key_release(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    SpaceGame* g = (SpaceGame*)data;
    if (g == NULL) return FALSE;

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_a || keyval == GDK_KEY_A || keyval == GDK_KEY_h) {
        g->key_left = FALSE;
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_d || keyval == GDK_KEY_D || keyval == GDK_KEY_l) {
        g->key_right = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void on_space_destroy(gpointer data) {
    SpaceGame* g = (SpaceGame*)data;
    if (g == NULL) return;
    if (g->timer_id > 0) {
        g_source_remove(g->timer_id);
        g->timer_id = 0;
    }
    g_free(g);
}

static void on_space_restart_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    SpaceGame* g = (SpaceGame*)data;
    if (g == NULL) return;
    space_init_game(g);
    gtk_widget_grab_focus(g->drawing_area);
}

static void on_space_pause_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    SpaceGame* g = (SpaceGame*)data;
    if (g == NULL) return;
    g->paused = !g->paused;
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_space_create_tab(AppData* app) {
    SpaceGame* g = g_new0(SpaceGame, 1);
    g->app = app;
    g->high_score = 3000;
    space_init_game(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>SPACE COMMANDER</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->score_label = gtk_label_new("Score: 0   High: 3000   Lives: 3   Wave: 1");
    gtk_box_append(GTK_BOX(header), g->score_label);

    g->status_label = gtk_label_new("");
    gtk_widget_set_hexpand(g->status_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->status_label);

    GtkWidget* pause_btn = gtk_button_new_with_label("Pause (P)");
    gtk_widget_set_focusable(pause_btn, FALSE);
    g_signal_connect(pause_btn, "clicked", G_CALLBACK(on_space_pause_clicked), g);
    gtk_box_append(GTK_BOX(header), pause_btn);

    GtkWidget* restart_btn = gtk_button_new_with_label("Restart (R)");
    gtk_widget_set_focusable(restart_btn, FALSE);
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_space_restart_clicked), g);
    gtk_box_append(GTK_BOX(header), restart_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), space_draw_func, g, NULL);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_space_key_press), g);
    g_signal_connect(key_ctrl, "key-released", G_CALLBACK(on_space_key_release), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Footer */
    GtkWidget* footer = gtk_label_new("← / →: Steer Ship  |  Space: Fire Lasers  |  P: Pause  |  R: Restart  |  Ctrl+W: Close Tab");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g->timer_id = g_timeout_add(33, space_tick, g);
    g_object_set_data_full(G_OBJECT(box), "space-data", g, on_space_destroy);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

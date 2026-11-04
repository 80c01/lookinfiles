/* Street Fighter 2D arcade fighting game for LookinFiles */
#include "game_fighter.h"
#include "palette.h"
#include "fonts.h"
#include "icons.h"
#include "app_events.h"
#include <stdlib.h>
#include <math.h>

#define STAGE_W 700.0f
#define STAGE_H 400.0f
#define FLOOR_Y 320.0f

typedef enum {
    ACT_IDLE,
    ACT_WALK_FWD,
    ACT_WALK_BACK,
    ACT_CROUCH,
    ACT_JUMP,
    ACT_PUNCH,
    ACT_KICK,
    ACT_HADOKEN,
    ACT_SHORYUKEN,
    ACT_HIT,
    ACT_KO,
    ACT_WIN
} FighterAction;

typedef struct {
    gfloat x, y;
    gfloat vy;
    gint facing; /* +1 right, -1 left */
    gint hp;     /* 0..100 */
    gint rounds_won;
    FighterAction action;
    gint action_timer;
    gboolean is_cpu;
    const gchar* name;
    PaletteRole gi_color;
    PaletteRole hair_color;
} Fighter;

typedef struct {
    gfloat x, y;
    gint dir;
    gboolean active;
    gint owner; /* 0: P1, 1: CPU */
} Fireball;

typedef struct {
    AppData* app;
    GtkWidget* drawing_area;
    GtkWidget* info_label;
    GtkWidget* round_label;
    guint timer_id;
    gboolean paused;
    gboolean match_over;
    gint round_num;
    gint match_timer; /* 99 seconds */
    gint round_state_timer;
    const gchar* announcement;

    Fighter p1;
    Fighter cpu;
    Fireball fireballs[2];

    /* Input state for P1 */
    gboolean in_left;
    gboolean in_right;
    gboolean in_down;
} FighterGame;

static void fighter_reset_round(FighterGame* g) {
    g->p1.x = 220.0f;
    g->p1.y = FLOOR_Y;
    g->p1.vy = 0.0f;
    g->p1.facing = 1;
    g->p1.hp = 100;
    g->p1.action = ACT_IDLE;
    g->p1.action_timer = 0;

    g->cpu.x = 480.0f;
    g->cpu.y = FLOOR_Y;
    g->cpu.vy = 0.0f;
    g->cpu.facing = -1;
    g->cpu.hp = 100;
    g->cpu.action = ACT_IDLE;
    g->cpu.action_timer = 0;

    for (gint i = 0; i < 2; i++) g->fireballs[i].active = FALSE;

    g->match_timer = 99 * 30; /* 99 seconds at 30 fps */
    g->round_state_timer = 45; /* "FIGHT!" duration */
    g->announcement = "FIGHT!";
}

static void fighter_init_game(FighterGame* g) {
    g->round_num = 1;
    g->match_over = FALSE;
    g->paused = FALSE;
    g->in_left = FALSE;
    g->in_right = FALSE;
    g->in_down = FALSE;

    /* P1: Ryu (White Gi, Red Headband) */
    g->p1.name = "RYU";
    g->p1.is_cpu = FALSE;
    g->p1.gi_color = PAL_FG_BRIGHT;
    g->p1.hair_color = PAL_ANSI_BLACK;
    g->p1.rounds_won = 0;

    /* CPU: Ken (Red Gi, Blonde Hair) */
    g->cpu.name = "KEN";
    g->cpu.is_cpu = TRUE;
    g->cpu.gi_color = PAL_ICON_RED;
    g->cpu.hair_color = PAL_ICON_YELLOW;
    g->cpu.rounds_won = 0;

    fighter_reset_round(g);
}

static void fighter_do_attack(Fighter* att, Fighter* def, gint dmg, gfloat reach, FighterAction act, gint duration) {
    att->action = act;
    att->action_timer = duration;

    gfloat dist = fabsf(att->x - def->x);
    gboolean in_front = (att->facing > 0 && def->x > att->x) || (att->facing < 0 && def->x < att->x);

    if (in_front && dist < reach && def->action != ACT_KO) {
        gboolean is_blocking = (def->action == ACT_WALK_BACK);
        gint actual_dmg = is_blocking ? (dmg / 5) : dmg;
        def->hp -= actual_dmg;
        if (def->hp <= 0) {
            def->hp = 0;
            def->action = ACT_KO;
            def->action_timer = 90;
        } else {
            def->action = ACT_HIT;
            def->action_timer = 12;
            def->x += att->facing * 18.0f; /* pushback */
        }
    }
}

static void update_fighter_physics(Fighter* f) {
    /* Gravity and jumping */
    if (f->action == ACT_JUMP || f->y < FLOOR_Y) {
        f->y += f->vy;
        f->vy += 1.2f;
        if (f->y >= FLOOR_Y) {
            f->y = FLOOR_Y;
            f->vy = 0.0f;
            if (f->action == ACT_JUMP) {
                f->action = ACT_IDLE;
            }
        }
    }

    if (f->action_timer > 0) {
        f->action_timer--;
        if (f->action_timer == 0) {
            if (f->action != ACT_KO && f->action != ACT_WIN) {
                f->action = ACT_IDLE;
            }
        }
    }

    /* Screen bounds */
    if (f->x < 50.0f) f->x = 50.0f;
    if (f->x > STAGE_W - 50.0f) f->x = STAGE_W - 50.0f;
}

static void update_cpu_ai(FighterGame* g) {
    Fighter* cpu = &g->cpu;
    Fighter* p1 = &g->p1;
    if (cpu->action == ACT_KO || cpu->action == ACT_WIN || cpu->action_timer > 0) return;

    cpu->facing = (p1->x > cpu->x) ? 1 : -1;
    gfloat dist = fabsf(cpu->x - p1->x);

    /* Check for incoming fireball: jump or block */
    for (gint i = 0; i < 2; i++) {
        if (g->fireballs[i].active && g->fireballs[i].owner == 0) {
            gfloat fb_dist = fabsf(cpu->x - g->fireballs[i].x);
            if (fb_dist < 120.0f && cpu->y >= FLOOR_Y) {
                if (rand() % 2 == 0) {
                    cpu->action = ACT_JUMP;
                    cpu->vy = -16.0f;
                    return;
                } else {
                    cpu->action = ACT_WALK_BACK;
                    return;
                }
            }
        }
    }

    if (dist > 220.0f) {
        /* Advance or shoot Hadoken */
        if (rand() % 60 == 0) {
            cpu->action = ACT_HADOKEN;
            cpu->action_timer = 20;
            if (!g->fireballs[1].active) {
                g->fireballs[1].active = TRUE;
                g->fireballs[1].x = cpu->x + cpu->facing * 35.0f;
                g->fireballs[1].y = FLOOR_Y - 45.0f;
                g->fireballs[1].dir = cpu->facing;
                g->fireballs[1].owner = 1;
            }
        } else {
            cpu->action = ACT_WALK_FWD;
            cpu->x += cpu->facing * 3.5f;
        }
    } else if (dist > 80.0f) {
        if (rand() % 30 == 0) {
            fighter_do_attack(cpu, p1, 14, 90.0f, ACT_KICK, 18);
        } else {
            cpu->action = ACT_WALK_FWD;
            cpu->x += cpu->facing * 3.0f;
        }
    } else {
        /* Close combat */
        gint r = rand() % 25;
        if (r == 0) {
            fighter_do_attack(cpu, p1, 8, 70.0f, ACT_PUNCH, 14);
        } else if (r == 1) {
            fighter_do_attack(cpu, p1, 14, 85.0f, ACT_KICK, 18);
        } else if (r == 2) {
            /* Shoryuken */
            cpu->action = ACT_SHORYUKEN;
            cpu->action_timer = 25;
            cpu->vy = -14.0f;
            cpu->y -= 10.0f;
            fighter_do_attack(cpu, p1, 24, 75.0f, ACT_SHORYUKEN, 25);
        } else if (r < 5) {
            cpu->action = ACT_WALK_BACK;
            cpu->x -= cpu->facing * 3.0f;
        }
    }
}

static gboolean fighter_tick(gpointer user_data) {
    FighterGame* g = (FighterGame*)user_data;
    if (g == NULL || g->paused) return G_SOURCE_CONTINUE;

    if (g->round_state_timer > 0) {
        g->round_state_timer--;
        if (g->round_state_timer == 0) {
            g->announcement = "";
        }
    }

    if (g->match_timer > 0 && !g->match_over && g->p1.hp > 0 && g->cpu.hp > 0) {
        g->match_timer--;
    }

    /* P1 Player Movement */
    Fighter* p1 = &g->p1;
    Fighter* cpu = &g->cpu;

    if (p1->action != ACT_KO && p1->action != ACT_WIN && p1->action_timer == 0) {
        p1->facing = (cpu->x > p1->x) ? 1 : -1;
        if (g->in_down) {
            p1->action = ACT_CROUCH;
        } else if (g->in_left) {
            p1->action = (p1->facing < 0) ? ACT_WALK_FWD : ACT_WALK_BACK;
            p1->x -= 4.0f;
        } else if (g->in_right) {
            p1->action = (p1->facing > 0) ? ACT_WALK_FWD : ACT_WALK_BACK;
            p1->x += 4.0f;
        } else if (p1->y >= FLOOR_Y) {
            p1->action = ACT_IDLE;
        }
    }

    update_fighter_physics(p1);
    update_cpu_ai(g);
    update_fighter_physics(cpu);

    /* Move Fireballs */
    for (gint i = 0; i < 2; i++) {
        if (g->fireballs[i].active) {
            g->fireballs[i].x += g->fireballs[i].dir * 7.5f;
            if (g->fireballs[i].x < 0 || g->fireballs[i].x > STAGE_W) {
                g->fireballs[i].active = FALSE;
                continue;
            }

            Fighter* target = (g->fireballs[i].owner == 0) ? cpu : p1;
            if (fabsf(g->fireballs[i].x - target->x) < 30.0f &&
                fabsf(g->fireballs[i].y - (target->y - 40.0f)) < 35.0f &&
                target->action != ACT_KO) {
                g->fireballs[i].active = FALSE;
                gboolean is_blocking = (target->action == ACT_WALK_BACK);
                target->hp -= is_blocking ? 4 : 18;
                if (target->hp <= 0) {
                    target->hp = 0;
                    target->action = ACT_KO;
                    target->action_timer = 90;
                } else {
                    target->action = ACT_HIT;
                    target->action_timer = 15;
                    target->x += g->fireballs[i].dir * 25.0f;
                }
            }
        }
    }

    /* Check Round Over */
    if (!g->match_over && (p1->hp <= 0 || cpu->hp <= 0 || g->match_timer <= 0)) {
        if (g->round_state_timer == 0) {
            if (p1->hp > cpu->hp) {
                p1->rounds_won++;
                g->announcement = "K.O.! RYU WINS!";
                p1->action = ACT_WIN;
            } else {
                cpu->rounds_won++;
                g->announcement = "K.O.! KEN WINS!";
                cpu->action = ACT_WIN;
            }
            g->round_state_timer = 90; /* 3 seconds celebration */
        } else if (g->round_state_timer == 1) {
            if (p1->rounds_won >= 2) {
                g->match_over = TRUE;
                g->announcement = "★ YOU WIN THE TOURNAMENT! ★";
            } else if (cpu->rounds_won >= 2) {
                g->match_over = TRUE;
                g->announcement = "YOU LOSE! PRESS R TO RETRY";
            } else {
                g->round_num++;
                fighter_reset_round(g);
            }
        }
    }

    /* Update labels */
    gchar* round_str = g_strdup_printf("Round %d   Timer: %d   Rounds: %d - %d",
                                       g->round_num, g->match_timer / 30,
                                       g->p1.rounds_won, g->cpu.rounds_won);
    gtk_label_set_text(GTK_LABEL(g->round_label), round_str);
    g_free(round_str);

    if (g->announcement && g->announcement[0] != '\0') {
        gtk_label_set_text(GTK_LABEL(g->info_label), g->announcement);
    } else {
        gtk_label_set_text(GTK_LABEL(g->info_label), "");
    }

    gtk_widget_queue_draw(g->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void draw_fighter_character(cairo_t* cr, Fighter* f) {
    gfloat fx = f->x;
    gfloat fy = f->y;
    gint d = f->facing;

    double gi_r, gi_g, gi_b;
    palette_rgb(f->gi_color, &gi_r, &gi_g, &gi_b);

    double hair_r, hair_g, hair_b;
    palette_rgb(f->hair_color, &hair_r, &hair_g, &hair_b);

    cairo_save(cr);
    cairo_translate(cr, fx, fy);

    /* Shadow */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    cairo_scale(cr, 1.0, 0.3);
    cairo_arc(cr, 0, 0, 25.0, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, fx, fy);

    if (f->action == ACT_KO) {
        /* Knocked down */
        cairo_rotate(cr, d * M_PI * 0.45);
    }

    gfloat head_y = -85.0f;
    gfloat body_y = -60.0f;
    if (f->action == ACT_CROUCH) {
        head_y = -55.0f;
        body_y = -35.0f;
    }

    /* Legs / Stance */
    cairo_set_source_rgb(cr, gi_r, gi_g, gi_b);
    cairo_set_line_width(cr, 12.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    if (f->action == ACT_KICK) {
        /* High roundhouse kick! */
        cairo_move_to(cr, 0, body_y + 20.0f);
        cairo_line_to(cr, -d * 15.0f, -5.0f); /* standing leg */
        cairo_stroke(cr);

        cairo_move_to(cr, 0, body_y + 15.0f);
        cairo_line_to(cr, d * 55.0f, body_y + 10.0f); /* extended kick leg! */
        cairo_stroke(cr);
    } else if (f->action == ACT_JUMP || f->action == ACT_SHORYUKEN) {
        cairo_move_to(cr, -d * 5.0f, body_y + 20.0f);
        cairo_line_to(cr, -d * 10.0f, body_y + 55.0f);
        cairo_move_to(cr, d * 5.0f, body_y + 20.0f);
        cairo_line_to(cr, d * 15.0f, body_y + 45.0f);
        cairo_stroke(cr);
    } else {
        /* Normal / walk stance */
        cairo_move_to(cr, -d * 10.0f, body_y + 20.0f);
        cairo_line_to(cr, -d * 14.0f, 0);
        cairo_move_to(cr, d * 10.0f, body_y + 20.0f);
        cairo_line_to(cr, d * 14.0f, 0);
        cairo_stroke(cr);
    }

    /* Torso / Karate Gi */
    cairo_set_source_rgb(cr, gi_r, gi_g, gi_b);
    cairo_rectangle(cr, -16.0f, body_y, 32.0f, 35.0f);
    cairo_fill(cr);

    /* Black Belt */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_rectangle(cr, -17.0f, body_y + 26.0f, 34.0f, 7.0f);
    cairo_fill(cr);

    /* Arms & Punches */
    cairo_set_line_width(cr, 10.0);
    cairo_set_source_rgb(cr, gi_r, gi_g, gi_b);

    if (f->action == ACT_PUNCH) {
        /* Extended jab */
        cairo_move_to(cr, 0, body_y + 10.0f);
        cairo_line_to(cr, d * 45.0f, body_y + 10.0f);
        cairo_stroke(cr);
    } else if (f->action == ACT_SHORYUKEN) {
        /* Skyward Dragon Uppercut! */
        cairo_move_to(cr, 0, body_y + 10.0f);
        cairo_line_to(cr, d * 10.0f, head_y - 25.0f);
        cairo_stroke(cr);
    } else if (f->action == ACT_HADOKEN) {
        /* Thrust palms forward */
        cairo_move_to(cr, -5.0f, body_y + 15.0f);
        cairo_line_to(cr, d * 35.0f, body_y + 15.0f);
        cairo_stroke(cr);
    } else {
        /* Guard / idle */
        cairo_move_to(cr, -d * 8.0f, body_y + 10.0f);
        cairo_line_to(cr, d * 15.0f, body_y + 8.0f);
        cairo_stroke(cr);
    }

    /* Head */
    cairo_set_source_rgb(cr, 0.95, 0.78, 0.65); /* Skin tone */
    cairo_arc(cr, 0, head_y, 13.0f, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Hair */
    cairo_set_source_rgb(cr, hair_r, hair_g, hair_b);
    cairo_arc(cr, 0, head_y - 3.0f, 13.0f, M_PI * 0.9, M_PI * 2.1);
    cairo_fill(cr);

    /* Headband (Ryu red, Ken none/brow) */
    if (!f->is_cpu) {
        cairo_set_source_rgb(cr, 0.9, 0.1, 0.1);
        cairo_rectangle(cr, -13.0f, head_y - 4.0f, 26.0f, 5.0f);
        cairo_fill(cr);
        /* Trailing tail */
        cairo_move_to(cr, -d * 12.0f, head_y - 2.0f);
        cairo_line_to(cr, -d * 28.0f, head_y + 6.0f);
        cairo_set_line_width(cr, 4.0);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

static void fighter_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    FighterGame* g = (FighterGame*)user_data;
    if (g == NULL) return;

    gfloat sx = (gfloat)width / STAGE_W;
    gfloat sy = (gfloat)height / STAGE_H;
    gfloat scale = (sx < sy) ? sx : sy;
    gfloat ox = (width - STAGE_W * scale) * 0.5f;
    gfloat oy = (height - STAGE_H * scale) * 0.5f;

    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);

    /* Stage Background: Sunset Sky */
    cairo_pattern_t* sky = cairo_pattern_create_linear(0, 0, 0, FLOOR_Y);
    cairo_pattern_add_color_stop_rgb(sky, 0.0, 0.3, 0.1, 0.4); /* Twilight purple */
    cairo_pattern_add_color_stop_rgb(sky, 0.6, 0.7, 0.25, 0.2); /* Sunset orange */
    cairo_pattern_add_color_stop_rgb(sky, 1.0, 0.85, 0.55, 0.2); /* Golden horizon */
    cairo_set_source(cr, sky);
    cairo_rectangle(cr, 0, 0, STAGE_W, FLOOR_Y);
    cairo_fill(cr);
    cairo_pattern_destroy(sky);

    /* Sun */
    cairo_set_source_rgba(cr, 1.0, 0.9, 0.5, 0.7);
    cairo_arc(cr, STAGE_W * 0.5f, FLOOR_Y * 0.6f, 45.0, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Distant Dojo Temple Silhouette */
    cairo_set_source_rgba(cr, 0.15, 0.08, 0.2, 0.8);
    cairo_move_to(cr, 80, FLOOR_Y);
    cairo_line_to(cr, 120, FLOOR_Y - 80);
    cairo_line_to(cr, 220, FLOOR_Y - 80);
    cairo_line_to(cr, 260, FLOOR_Y);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Floor: Wooden tatami / arena */
    cairo_pattern_t* floor_pat = cairo_pattern_create_linear(0, FLOOR_Y, 0, STAGE_H);
    cairo_pattern_add_color_stop_rgb(floor_pat, 0.0, 0.45, 0.25, 0.15);
    cairo_pattern_add_color_stop_rgb(floor_pat, 1.0, 0.25, 0.12, 0.08);
    cairo_set_source(cr, floor_pat);
    cairo_rectangle(cr, 0, FLOOR_Y, STAGE_W, STAGE_H - FLOOR_Y);
    cairo_fill(cr);
    cairo_pattern_destroy(floor_pat);

    /* Health Bars at Top */
    gfloat bar_w = 260.0f;
    gfloat bar_h = 18.0f;

    /* P1 Health */
    cairo_set_source_rgb(cr, 0.4, 0.1, 0.1);
    cairo_rectangle(cr, 40, 25, bar_w, bar_h);
    cairo_fill(cr);

    double hp_r, hp_g, hp_b;
    palette_rgb(PAL_ICON_YELLOW, &hp_r, &hp_g, &hp_b);
    cairo_set_source_rgb(cr, hp_r, hp_g, hp_b);
    gfloat p1_w = bar_w * (g->p1.hp / 100.0f);
    cairo_rectangle(cr, 40 + (bar_w - p1_w), 25, p1_w, bar_h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, 40, 25, bar_w, bar_h);
    cairo_stroke(cr);

    /* CPU Health */
    cairo_set_source_rgb(cr, 0.4, 0.1, 0.1);
    cairo_rectangle(cr, STAGE_W - 40 - bar_w, 25, bar_w, bar_h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, hp_r, hp_g, hp_b);
    gfloat cpu_w = bar_w * (g->cpu.hp / 100.0f);
    cairo_rectangle(cr, STAGE_W - 40 - bar_w, 25, cpu_w, bar_h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, STAGE_W - 40 - bar_w, 25, bar_w, bar_h);
    cairo_stroke(cr);

    /* Draw Fighters */
    draw_fighter_character(cr, &g->p1);
    draw_fighter_character(cr, &g->cpu);

    /* Draw Hadoken Fireballs */
    double fb_r, fb_g, fb_b;
    palette_rgb(PAL_LINK, &fb_r, &fb_g, &fb_b);
    for (gint i = 0; i < 2; i++) {
        if (g->fireballs[i].active) {
            cairo_set_source_rgba(cr, fb_r, fb_g, fb_b, 0.85);
            cairo_arc(cr, g->fireballs[i].x, g->fireballs[i].y, 16.0, 0, 2 * M_PI);
            cairo_fill(cr);
            /* Inner glowing core */
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_arc(cr, g->fireballs[i].x, g->fireballs[i].y, 8.0, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    cairo_restore(cr);
}

static gboolean on_fighter_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    FighterGame* g = (FighterGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE;
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    Fighter* p1 = &g->p1;
    Fighter* cpu = &g->cpu;

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_h) {
        g->in_left = TRUE;
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_l) {
        g->in_right = TRUE;
        return TRUE;
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j) {
        g->in_down = TRUE;
        return TRUE;
    } else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_k) {
        if (p1->y >= FLOOR_Y && p1->action != ACT_KO) {
            p1->action = ACT_JUMP;
            p1->vy = -17.0f;
        }
        return TRUE;
    } else if (keyval == GDK_KEY_a || keyval == GDK_KEY_A) {
        /* Punch */
        if (p1->action != ACT_KO && p1->action_timer == 0) {
            fighter_do_attack(p1, cpu, 8, 70.0f, ACT_PUNCH, 12);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        /* Kick */
        if (p1->action != ACT_KO && p1->action_timer == 0) {
            fighter_do_attack(p1, cpu, 14, 85.0f, ACT_KICK, 18);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
        /* Hadoken! */
        if (p1->action != ACT_KO && p1->action_timer == 0 && !g->fireballs[0].active) {
            p1->action = ACT_HADOKEN;
            p1->action_timer = 20;
            g->fireballs[0].active = TRUE;
            g->fireballs[0].x = p1->x + p1->facing * 35.0f;
            g->fireballs[0].y = FLOOR_Y - 45.0f;
            g->fireballs[0].dir = p1->facing;
            g->fireballs[0].owner = 0;
        }
        return TRUE;
    } else if (keyval == GDK_KEY_w || keyval == GDK_KEY_W) {
        /* Shoryuken Dragon Uppercut! */
        if (p1->action != ACT_KO && p1->action_timer == 0) {
            p1->action = ACT_SHORYUKEN;
            p1->action_timer = 25;
            p1->vy = -15.0f;
            p1->y -= 10.0f;
            fighter_do_attack(p1, cpu, 24, 75.0f, ACT_SHORYUKEN, 25);
        }
        return TRUE;
    } else if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g->paused = !g->paused;
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        fighter_init_game(g);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_fighter_key_release(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    FighterGame* g = (FighterGame*)data;
    if (g == NULL) return FALSE;

    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_h) {
        g->in_left = FALSE;
        return TRUE;
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_l) {
        g->in_right = FALSE;
        return TRUE;
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j) {
        g->in_down = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void on_fighter_destroy(gpointer data) {
    FighterGame* g = (FighterGame*)data;
    if (g == NULL) return;
    if (g->timer_id > 0) {
        g_source_remove(g->timer_id);
        g->timer_id = 0;
    }
    g_free(g);
}

static void on_fighter_restart_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    FighterGame* g = (FighterGame*)data;
    if (g == NULL) return;
    fighter_init_game(g);
    gtk_widget_grab_focus(g->drawing_area);
}

static void on_fighter_pause_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    FighterGame* g = (FighterGame*)data;
    if (g == NULL) return;
    g->paused = !g->paused;
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_fighter_create_tab(AppData* app) {
    FighterGame* g = g_new0(FighterGame, 1);
    g->app = app;
    fighter_init_game(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>STREET FIGHTER</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->round_label = gtk_label_new("Round 1   Timer: 99   Rounds: 0 - 0");
    gtk_box_append(GTK_BOX(header), g->round_label);

    g->info_label = gtk_label_new("FIGHT!");
    gtk_widget_set_hexpand(g->info_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->info_label);

    GtkWidget* pause_btn = gtk_button_new_with_label("Pause (P)");
    gtk_widget_set_focusable(pause_btn, FALSE);
    g_signal_connect(pause_btn, "clicked", G_CALLBACK(on_fighter_pause_clicked), g);
    gtk_box_append(GTK_BOX(header), pause_btn);

    GtkWidget* restart_btn = gtk_button_new_with_label("Rematch (R)");
    gtk_widget_set_focusable(restart_btn, FALSE);
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_fighter_restart_clicked), g);
    gtk_box_append(GTK_BOX(header), restart_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), fighter_draw_func, g, NULL);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_fighter_key_press), g);
    g_signal_connect(key_ctrl, "key-released", G_CALLBACK(on_fighter_key_release), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Footer */
    GtkWidget* footer = gtk_label_new("← / →: Move/Block  |  ↑: Jump  |  ↓: Crouch  |  A: Punch  |  S: Kick  |  D: Hadoken  |  W: Shoryuken  |  R: Rematch");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g->timer_id = g_timeout_add(33, fighter_tick, g);
    g_object_set_data_full(G_OBJECT(box), "fighter-data", g, on_fighter_destroy);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

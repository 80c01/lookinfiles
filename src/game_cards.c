/* Card games suite (Blackjack, Klondike Solitaire, Memory Match) for LookinFiles */
#include "game_cards.h"
#include "palette.h"
#include "fonts.h"
#include "icons.h"
#include "app_events.h"
#include <stdlib.h>
#include <math.h>

typedef enum {
    SUBGAME_BLACKJACK,
    SUBGAME_SOLITAIRE,
    SUBGAME_MEMORY
} SubgameType;

typedef struct {
    gint suit;  /* 0: Spades, 1: Hearts, 2: Diamonds, 3: Clubs */
    gint rank;  /* 1 (Ace) .. 13 (King) */
    gboolean face_up;
} Card;

/* Solitaire Pile */
typedef struct {
    Card cards[24];
    gint count;
} CardPile;

typedef struct {
    AppData* app;
    GtkWidget* drawing_area;
    GtkWidget* info_label;
    GtkWidget* subgame_label;
    SubgameType type;

    /* Common deck */
    Card deck[52];
    gint deck_pos;

    /* Blackjack state */
    Card bj_player[10];
    gint bj_player_count;
    Card bj_dealer[10];
    gint bj_dealer_count;
    gboolean bj_dealer_revealed;
    gint bj_chips;
    gint bj_bet;
    gboolean bj_round_active;
    const gchar* bj_status;

    /* Solitaire state */
    CardPile sol_tableau[7];
    CardPile sol_foundations[4]; /* Spades, Hearts, Diamonds, Clubs */
    CardPile sol_stock;
    CardPile sol_waste;
    gint sol_sel_pile; /* 0..6 tableau, 7 waste */
    gint sol_sel_idx;

    /* Memory Match state */
    Card mem_cards[16];
    gboolean mem_matched[16];
    gint mem_first_flipped;
    gint mem_second_flipped;
    gint mem_flips;
    gint mem_matches;
} CardsGame;

static void shuffle_deck(Card* d, gint count) {
    for (gint i = count - 1; i > 0; i--) {
        gint j = rand() % (i + 1);
        Card temp = d[i];
        d[i] = d[j];
        d[j] = temp;
    }
}

static void init_deck_52(Card* d) {
    gint idx = 0;
    for (gint s = 0; s < 4; s++) {
        for (gint r = 1; r <= 13; r++) {
            d[idx].suit = s;
            d[idx].rank = r;
            d[idx].face_up = FALSE;
            idx++;
        }
    }
    shuffle_deck(d, 52);
}

/* ------------------------------------------------------------------------- */
/* Blackjack                                                                 */
/* ------------------------------------------------------------------------- */

static gint bj_calc_hand(const Card* hand, gint count) {
    gint sum = 0;
    gint aces = 0;
    for (gint i = 0; i < count; i++) {
        if (!hand[i].face_up) continue;
        gint r = hand[i].rank;
        if (r == 1) {
            aces++;
            sum += 11;
        } else if (r >= 10) {
            sum += 10;
        } else {
            sum += r;
        }
    }
    while (sum > 21 && aces > 0) {
        sum -= 10;
        aces--;
    }
    return sum;
}

static void bj_deal(CardsGame* g) {
    if (g->bj_chips < g->bj_bet) {
        g->bj_status = "Not enough chips to bet!";
        return;
    }
    init_deck_52(g->deck);
    g->deck_pos = 0;

    g->bj_player_count = 0;
    g->bj_dealer_count = 0;

    /* Deal initial cards */
    g->bj_player[0] = g->deck[g->deck_pos++]; g->bj_player[0].face_up = TRUE;
    g->bj_dealer[0] = g->deck[g->deck_pos++]; g->bj_dealer[0].face_up = TRUE;
    g->bj_player[1] = g->deck[g->deck_pos++]; g->bj_player[1].face_up = TRUE;
    g->bj_dealer[1] = g->deck[g->deck_pos++]; g->bj_dealer[1].face_up = FALSE; /* Hole card */

    g->bj_player_count = 2;
    g->bj_dealer_count = 2;
    g->bj_dealer_revealed = FALSE;
    g->bj_round_active = TRUE;

    gint psum = bj_calc_hand(g->bj_player, g->bj_player_count);
    if (psum == 21) {
        /* Natural Blackjack! */
        g->bj_dealer[1].face_up = TRUE;
        g->bj_dealer_revealed = TRUE;
        g->bj_round_active = FALSE;
        gint dsum = bj_calc_hand(g->bj_dealer, g->bj_dealer_count);
        if (dsum == 21) {
            g->bj_status = "Push! Both have Blackjack!";
        } else {
            g->bj_chips += (gint)(g->bj_bet * 1.5f);
            g->bj_status = "Blackjack! You win 3:2 payout!";
        }
    } else {
        g->bj_status = "Hit or Stand?";
    }
}

static void bj_hit(CardsGame* g) {
    if (!g->bj_round_active || g->bj_player_count >= 10) return;
    g->bj_player[g->bj_player_count] = g->deck[g->deck_pos++];
    g->bj_player[g->bj_player_count].face_up = TRUE;
    g->bj_player_count++;

    gint psum = bj_calc_hand(g->bj_player, g->bj_player_count);
    if (psum > 21) {
        g->bj_dealer[1].face_up = TRUE;
        g->bj_dealer_revealed = TRUE;
        g->bj_round_active = FALSE;
        g->bj_chips -= g->bj_bet;
        g->bj_status = "Bust! You went over 21!";
    } else if (psum == 21) {
        g->bj_status = "21! Click Stand to hold.";
    }
}

static void bj_stand(CardsGame* g) {
    if (!g->bj_round_active) return;
    g->bj_dealer[1].face_up = TRUE;
    g->bj_dealer_revealed = TRUE;
    g->bj_round_active = FALSE;

    gint psum = bj_calc_hand(g->bj_player, g->bj_player_count);
    gint dsum = bj_calc_hand(g->bj_dealer, g->bj_dealer_count);

    /* Dealer hits until 17+ */
    while (dsum < 17 && g->bj_dealer_count < 10) {
        g->bj_dealer[g->bj_dealer_count] = g->deck[g->deck_pos++];
        g->bj_dealer[g->bj_dealer_count].face_up = TRUE;
        g->bj_dealer_count++;
        dsum = bj_calc_hand(g->bj_dealer, g->bj_dealer_count);
    }

    if (dsum > 21) {
        g->bj_chips += g->bj_bet;
        g->bj_status = "Dealer busts! You win!";
    } else if (psum > dsum) {
        g->bj_chips += g->bj_bet;
        g->bj_status = "You win!";
    } else if (dsum > psum) {
        g->bj_chips -= g->bj_bet;
        g->bj_status = "Dealer wins!";
    } else {
        g->bj_status = "Push (Tie)! Bet returned.";
    }
}

/* ------------------------------------------------------------------------- */
/* Solitaire                                                                 */
/* ------------------------------------------------------------------------- */

static void sol_init(CardsGame* g) {
    init_deck_52(g->deck);
    g->deck_pos = 0;

    for (gint i = 0; i < 4; i++) g->sol_foundations[i].count = 0;
    g->sol_waste.count = 0;
    g->sol_sel_pile = -1;
    g->sol_sel_idx = -1;

    /* Deal 7 tableau columns */
    for (gint col = 0; col < 7; col++) {
        g->sol_tableau[col].count = col + 1;
        for (gint r = 0; r <= col; r++) {
            g->sol_tableau[col].cards[r] = g->deck[g->deck_pos++];
            g->sol_tableau[col].cards[r].face_up = (r == col);
        }
    }

    /* Rest to stock */
    g->sol_stock.count = 0;
    while (g->deck_pos < 52) {
        g->sol_stock.cards[g->sol_stock.count] = g->deck[g->deck_pos++];
        g->sol_stock.cards[g->sol_stock.count].face_up = FALSE;
        g->sol_stock.count++;
    }
}

static gboolean sol_try_move_to_foundation(CardsGame* g, gint pref) {
    if (g->sol_sel_pile < 0) return FALSE;
    if (g->sol_sel_pile != 7 &&
        g->sol_sel_idx != g->sol_tableau[g->sol_sel_pile].count - 1) {
        return FALSE;
    }

    Card* src = (g->sol_sel_pile == 7) ?
                &g->sol_waste.cards[g->sol_waste.count - 1] :
                &g->sol_tableau[g->sol_sel_pile].cards[g->sol_sel_idx];

    gint target = -1;
    for (gint pass = 0; pass < 2 && target < 0; pass++) {
        for (gint i = 0; i < 4; i++) {
            if (pass == 0 && i != pref) continue;
            if (pass == 1 && i == pref) continue;
            if (g->sol_foundations[i].count == 0) {
                if (src->rank == 1) {
                    target = i;
                    break;
                }
            } else {
                Card* top = &g->sol_foundations[i].cards[g->sol_foundations[i].count - 1];
                if (top->suit == src->suit && src->rank == top->rank + 1) {
                    target = i;
                    break;
                }
            }
        }
    }

    if (target < 0) return FALSE;

    g->sol_foundations[target].cards[g->sol_foundations[target].count++] = *src;
    if (g->sol_sel_pile == 7) {
        g->sol_waste.count--;
    } else {
        g->sol_tableau[g->sol_sel_pile].count--;
        if (g->sol_tableau[g->sol_sel_pile].count > 0) {
            g->sol_tableau[g->sol_sel_pile].cards[g->sol_tableau[g->sol_sel_pile].count - 1].face_up = TRUE;
        }
    }
    g->sol_sel_pile = -1;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Memory Match                                                              */
/* ------------------------------------------------------------------------- */

static void mem_init(CardsGame* g) {
    g->mem_flips = 0;
    g->mem_matches = 0;
    g->mem_first_flipped = -1;
    g->mem_second_flipped = -1;

    Card deck16[16];
    gint idx = 0;
    for (gint r = 1; r <= 8; r++) {
        deck16[idx].suit = 0; deck16[idx].rank = r; deck16[idx].face_up = FALSE; idx++;
        deck16[idx].suit = 1; deck16[idx].rank = r; deck16[idx].face_up = FALSE; idx++;
    }
    shuffle_deck(deck16, 16);
    for (gint i = 0; i < 16; i++) {
        g->mem_cards[i] = deck16[i];
        g->mem_matched[i] = FALSE;
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing Helpers                                                           */
/* ------------------------------------------------------------------------- */

static const gchar* rank_to_str(gint rank) {
    switch (rank) {
        case 1:  return "A";
        case 10: return "10";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default: {
            static gchar buf[4];
            g_snprintf(buf, sizeof(buf), "%d", rank);
            return buf;
        }
    }
}

static const gchar* suit_to_str(gint suit) {
    switch (suit) {
        case 0: return "♠";
        case 1: return "♥";
        case 2: return "♦";
        case 3: return "♣";
        default: return "";
    }
}

static void draw_playing_card(cairo_t* cr, gfloat x, gfloat y, gfloat w, gfloat h, const Card* c, gboolean selected) {
    /* Drop shadow */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
    cairo_rectangle(cr, x + 3.0f, y + 3.0f, w, h);
    cairo_fill(cr);

    if (!c->face_up) {
        /* Card Back (Patterned Deep Blue) */
        cairo_set_source_rgb(cr, 0.15, 0.25, 0.65);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, x + 4, y + 4, w - 8, h - 8);
        cairo_stroke(cr);
        return;
    }

    /* Face-up Card (White paper) */
    cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, selected ? 0.0 : 0.6, selected ? 0.6 : 0.6, selected ? 1.0 : 0.6);
    cairo_set_line_width(cr, selected ? 3.0 : 1.0);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);

    /* Suit & Rank Colors: Hearts/Diamonds Red, Spades/Clubs Dark */
    gboolean is_red = (c->suit == 1 || c->suit == 2);
    double text_r, text_g, text_b;
    if (is_red) {
        palette_rgb(PAL_ICON_RED, &text_r, &text_g, &text_b);
    } else {
        palette_rgb(PAL_ANSI_BLACK, &text_r, &text_g, &text_b);
    }
    cairo_set_source_rgb(cr, text_r, text_g, text_b);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string(fonts_family(FONT_UI));
    pango_font_description_set_size(fd, (gint)(w * 0.28f * PANGO_SCALE));
    pango_layout_set_font_description(layout, fd);

    gchar* txt = g_strdup_printf("%s%s", rank_to_str(c->rank), suit_to_str(c->suit));
    pango_layout_set_text(layout, txt, -1);
    cairo_move_to(cr, x + 6.0f, y + 6.0f);
    pango_cairo_show_layout(cr, layout);
    g_free(txt);

    /* Center Suit Glyph */
    pango_font_description_set_size(fd, (gint)(w * 0.45f * PANGO_SCALE));
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, suit_to_str(c->suit), -1);
    int sw, sh;
    pango_layout_get_pixel_size(layout, &sw, &sh);
    cairo_move_to(cr, x + (w - sw) * 0.5f, y + (h - sh) * 0.5f);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(fd);
    g_object_unref(layout);
}

static void cards_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    (void)height;
    CardsGame* g = (CardsGame*)user_data;
    if (g == NULL) return;

    /* Green Felt Table Background */
    cairo_set_source_rgb(cr, 0.12, 0.42, 0.22);
    cairo_paint(cr);

    if (g->type == SUBGAME_BLACKJACK) {
        /* Dealer Cards */
        gfloat cw = 75.0f;
        gfloat ch = 105.0f;

        /* Dealer Zone */
        for (gint i = 0; i < g->bj_dealer_count; i++) {
            draw_playing_card(cr, 180.0f + i * 85.0f, 60.0f, cw, ch, &g->bj_dealer[i], FALSE);
        }

        /* Player Zone */
        for (gint i = 0; i < g->bj_player_count; i++) {
            draw_playing_card(cr, 180.0f + i * 85.0f, 220.0f, cw, ch, &g->bj_player[i], FALSE);
        }

        /* Status & Chips Banner */
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string(fonts_family(FONT_UI));
        pango_font_description_set_size(fd, 14 * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        gchar* s1 = g_strdup_printf("Dealer's Hand (%s)", g->bj_dealer_revealed ?
                                    g_strdup_printf("%d", bj_calc_hand(g->bj_dealer, g->bj_dealer_count)) : "?");
        pango_layout_set_text(layout, s1, -1);
        cairo_move_to(cr, 40.0f, 60.0f);
        pango_cairo_show_layout(cr, layout);
        g_free(s1);

        gchar* s2 = g_strdup_printf("Your Hand (%d)\nChips: $%d\nBet: $%d",
                                    bj_calc_hand(g->bj_player, g->bj_player_count),
                                    g->bj_chips, g->bj_bet);
        pango_layout_set_text(layout, s2, -1);
        cairo_move_to(cr, 40.0f, 220.0f);
        pango_cairo_show_layout(cr, layout);
        g_free(s2);

        /* Action Buttons Area at bottom */
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
        cairo_rectangle(cr, 40.0f, 360.0f, width - 80.0f, 50.0f);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1.0, 0.9, 0.3);
        pango_layout_set_text(layout, g->bj_status ? g->bj_status : "", -1);
        cairo_move_to(cr, 60.0f, 375.0f);
        pango_cairo_show_layout(cr, layout);

        pango_font_description_free(fd);
        g_object_unref(layout);

    } else if (g->type == SUBGAME_SOLITAIRE) {
        gfloat cw = 65.0f;
        gfloat ch = 90.0f;

        /* Foundations top right */
        for (gint i = 0; i < 4; i++) {
            gfloat fx = width - 360.0f + i * 85.0f;
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
            cairo_rectangle(cr, fx, 20.0f, cw, ch);
            cairo_fill(cr);
            if (g->sol_foundations[i].count > 0) {
                Card* top = &g->sol_foundations[i].cards[g->sol_foundations[i].count - 1];
                draw_playing_card(cr, fx, 20.0f, cw, ch, top, FALSE);
            }
        }

        /* Stock and Waste top left */
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
        cairo_rectangle(cr, 40.0f, 20.0f, cw, ch);
        cairo_fill(cr);
        if (g->sol_stock.count > 0) {
            draw_playing_card(cr, 40.0f, 20.0f, cw, ch, &g->sol_stock.cards[g->sol_stock.count - 1], FALSE);
        }

        cairo_rectangle(cr, 125.0f, 20.0f, cw, ch);
        cairo_fill(cr);
        if (g->sol_waste.count > 0) {
            Card* top = &g->sol_waste.cards[g->sol_waste.count - 1];
            gboolean sel = (g->sol_sel_pile == 7);
            draw_playing_card(cr, 125.0f, 20.0f, cw, ch, top, sel);
        }

        /* Tableau (7 columns) */
        for (gint col = 0; col < 7; col++) {
            gfloat tx = 40.0f + col * 85.0f;
            gfloat ty = 140.0f;
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.2);
            cairo_rectangle(cr, tx, ty, cw, ch);
            cairo_fill(cr);

            for (gint r = 0; r < g->sol_tableau[col].count; r++) {
                Card* c = &g->sol_tableau[col].cards[r];
                gboolean sel = (g->sol_sel_pile == col && r >= g->sol_sel_idx);
                draw_playing_card(cr, tx, ty + r * 22.0f, cw, ch, c, sel);
            }
        }

    } else if (g->type == SUBGAME_MEMORY) {
        /* 4x4 Grid */
        gfloat cw = 75.0f;
        gfloat ch = 105.0f;
        gfloat ox = (width - 4 * 90.0f) * 0.5f;
        gfloat oy = 50.0f;

        for (gint i = 0; i < 16; i++) {
            gint r = i / 4;
            gint c = i % 4;
            gfloat cx = ox + c * 90.0f;
            gfloat cy = oy + r * 115.0f;

            Card card = g->mem_cards[i];
            card.face_up = (g->mem_matched[i] || i == g->mem_first_flipped || i == g->mem_second_flipped);
            draw_playing_card(cr, cx, cy, cw, ch, &card, FALSE);
        }
    }
}

static void on_cards_click(GtkGestureClick* gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    CardsGame* g = (CardsGame*)data;
    if (g == NULL) return;

    if (g->type == SUBGAME_BLACKJACK) {
        /* Check Action Buttons at bottom */
        if (y >= 360.0f && y <= 410.0f) {
            /* Deal / Hit / Stand */
            if (x < 150.0f) {
                bj_deal(g);
            } else if (x < 260.0f) {
                bj_hit(g);
            } else if (x < 370.0f) {
                bj_stand(g);
            }
            gtk_widget_queue_draw(g->drawing_area);
        }
    } else if (g->type == SUBGAME_SOLITAIRE) {
        gfloat cw = 65.0f;
        gfloat ch = 90.0f;

        /* Click on Stock: draw card */
        if (x >= 40.0f && x <= 40.0f + cw && y >= 20.0f && y <= 20.0f + ch) {
            if (g->sol_stock.count > 0) {
                Card c = g->sol_stock.cards[--g->sol_stock.count];
                c.face_up = TRUE;
                g->sol_waste.cards[g->sol_waste.count++] = c;
            } else {
                /* Recycle waste to stock */
                while (g->sol_waste.count > 0) {
                    Card c = g->sol_waste.cards[--g->sol_waste.count];
                    c.face_up = FALSE;
                    g->sol_stock.cards[g->sol_stock.count++] = c;
                }
            }
            g->sol_sel_pile = -1;
            gtk_widget_queue_draw(g->drawing_area);
            return;
        }

        /* Click Waste */
        if (x >= 125.0f && x <= 125.0f + cw && y >= 20.0f && y <= 20.0f + ch && g->sol_waste.count > 0) {
            if (g->sol_sel_pile == 7 || n_press >= 2) {
                g->sol_sel_pile = 7;
                g->sol_sel_idx = g->sol_waste.count - 1;
                if (!sol_try_move_to_foundation(g, -1) && n_press == 1) {
                    g->sol_sel_pile = -1;
                }
            } else {
                g->sol_sel_pile = 7;
                g->sol_sel_idx = g->sol_waste.count - 1;
            }
            gtk_widget_queue_draw(g->drawing_area);
            return;
        }

        /* Click Foundations top right */
        int width = gtk_widget_get_width(g->drawing_area);
        for (gint i = 0; i < 4; i++) {
            gfloat fx = width - 360.0f + i * 85.0f;
            if (x >= fx && x <= fx + cw && y >= 20.0f && y <= 20.0f + ch) {
                if (g->sol_sel_pile >= 0) {
                    sol_try_move_to_foundation(g, i);
                    g->sol_sel_pile = -1;
                    gtk_widget_queue_draw(g->drawing_area);
                }
                return;
            }
        }

        /* Click Tableau Columns */
        for (gint col = 0; col < 7; col++) {
            gfloat tx = 40.0f + col * 85.0f;
            if (x >= tx && x <= tx + cw && y >= 140.0f) {
                if (g->sol_sel_pile == col && g->sol_tableau[col].count > 0) {
                    if (!sol_try_move_to_foundation(g, -1)) {
                        g->sol_sel_pile = -1;
                    }
                } else if (g->sol_sel_pile >= 0) {
                    /* Try to move selected card(s) to this column */
                    Card* src = (g->sol_sel_pile == 7) ?
                                &g->sol_waste.cards[g->sol_waste.count - 1] :
                                &g->sol_tableau[g->sol_sel_pile].cards[g->sol_sel_idx];

                    gboolean valid = FALSE;
                    if (g->sol_tableau[col].count == 0) {
                        valid = (src->rank == 13); /* King on empty space */
                    } else {
                        Card* dst = &g->sol_tableau[col].cards[g->sol_tableau[col].count - 1];
                        gboolean alt_color = ((src->suit == 1 || src->suit == 2) != (dst->suit == 1 || dst->suit == 2));
                        valid = (alt_color && dst->rank == src->rank + 1);
                    }

                    if (valid) {
                        if (g->sol_sel_pile == 7) {
                            g->sol_tableau[col].cards[g->sol_tableau[col].count++] = *src;
                            g->sol_waste.count--;
                        } else {
                            gint move_count = g->sol_tableau[g->sol_sel_pile].count - g->sol_sel_idx;
                            for (gint m = 0; m < move_count; m++) {
                                g->sol_tableau[col].cards[g->sol_tableau[col].count++] =
                                    g->sol_tableau[g->sol_sel_pile].cards[g->sol_sel_idx + m];
                            }
                            g->sol_tableau[g->sol_sel_pile].count = g->sol_sel_idx;
                            if (g->sol_tableau[g->sol_sel_pile].count > 0) {
                                g->sol_tableau[g->sol_sel_pile].cards[g->sol_tableau[g->sol_sel_pile].count - 1].face_up = TRUE;
                            }
                        }
                    }
                    g->sol_sel_pile = -1;
                } else if (g->sol_tableau[col].count > 0) {
                    /* Select top or stack */
                    gint clicked_card = (gint)((y - 140.0f) / 22.0f);
                    if (clicked_card >= g->sol_tableau[col].count) {
                        clicked_card = g->sol_tableau[col].count - 1;
                    }
                    if (g->sol_tableau[col].cards[clicked_card].face_up) {
                        g->sol_sel_pile = col;
                        g->sol_sel_idx = clicked_card;
                    }
                }
                gtk_widget_queue_draw(g->drawing_area);
                return;
            }
        }

    } else if (g->type == SUBGAME_MEMORY) {
        int width = gtk_widget_get_width(g->drawing_area);
        gfloat ox = (width - 4 * 90.0f) * 0.5f;
        gfloat oy = 50.0f;

        for (gint i = 0; i < 16; i++) {
            gint r = i / 4;
            gint c = i % 4;
            gfloat cx = ox + c * 90.0f;
            gfloat cy = oy + r * 115.0f;
            if (x >= cx && x <= cx + 75.0f && y >= cy && y <= cy + 105.0f) {
                if (g->mem_matched[i] || i == g->mem_first_flipped) return;

                if (g->mem_first_flipped < 0) {
                    g->mem_first_flipped = i;
                } else if (g->mem_second_flipped < 0) {
                    g->mem_second_flipped = i;
                    g->mem_flips++;
                    if (g->mem_cards[g->mem_first_flipped].rank == g->mem_cards[g->mem_second_flipped].rank) {
                        g->mem_matched[g->mem_first_flipped] = TRUE;
                        g->mem_matched[g->mem_second_flipped] = TRUE;
                        g->mem_first_flipped = -1;
                        g->mem_second_flipped = -1;
                        g->mem_matches++;
                    }
                } else {
                    g->mem_first_flipped = i;
                    g->mem_second_flipped = -1;
                }
                gtk_widget_queue_draw(g->drawing_area);
                return;
            }
        }
    }
}

static gboolean on_cards_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    CardsGame* g = (CardsGame*)data;
    if (g == NULL) return FALSE;

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        return FALSE;
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        app_emit(g->app, APP_EVENT_CLOSE_TAB);
        return TRUE;
    }

    if (keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
        if (g->type == SUBGAME_BLACKJACK) bj_deal(g);
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_h || keyval == GDK_KEY_H) {
        if (g->type == SUBGAME_BLACKJACK) bj_hit(g);
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        if (g->type == SUBGAME_BLACKJACK) bj_stand(g);
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    } else if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        if (g->type == SUBGAME_BLACKJACK) {
            g->bj_chips = 500;
            g->bj_bet = 25;
            g->bj_round_active = FALSE;
            g->bj_status = "Press Deal (D) to play!";
        } else if (g->type == SUBGAME_SOLITAIRE) {
            sol_init(g);
        } else if (g->type == SUBGAME_MEMORY) {
            mem_init(g);
        }
        gtk_widget_queue_draw(g->drawing_area);
        return TRUE;
    }
    return FALSE;
}

static void on_subgame_btn_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CardsGame* g = (CardsGame*)data;
    if (g == NULL) return;
    g->type = (g->type + 1) % 3;
    if (g->type == SUBGAME_BLACKJACK) {
        gtk_label_set_text(GTK_LABEL(g->subgame_label), "Game: Blackjack (21)");
    } else if (g->type == SUBGAME_SOLITAIRE) {
        gtk_label_set_text(GTK_LABEL(g->subgame_label), "Game: Klondike Solitaire");
    } else {
        gtk_label_set_text(GTK_LABEL(g->subgame_label), "Game: Memory Match");
    }
    gtk_widget_queue_draw(g->drawing_area);
    gtk_widget_grab_focus(g->drawing_area);
}

GtkWidget* game_cards_create_tab(AppData* app, const gchar* subgame) {
    CardsGame* g = g_new0(CardsGame, 1);
    g->app = app;
    g->type = SUBGAME_BLACKJACK;
    if (g_strcmp0(subgame, "solitaire") == 0) g->type = SUBGAME_SOLITAIRE;
    else if (g_strcmp0(subgame, "memory") == 0) g->type = SUBGAME_MEMORY;

    /* Init all modes */
    g->bj_chips = 500;
    g->bj_bet = 25;
    g->bj_status = "Click Deal (D) or button to start!";
    sol_init(g);
    mem_init(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>CARD GAMES</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    const gchar* label_txt = (g->type == SUBGAME_BLACKJACK) ? "Game: Blackjack (21)" :
                             (g->type == SUBGAME_SOLITAIRE ? "Game: Klondike Solitaire" : "Game: Memory Match");
    g->subgame_label = gtk_label_new(label_txt);
    gtk_widget_set_hexpand(g->subgame_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->subgame_label);

    GtkWidget* switch_btn = gtk_button_new_with_label("Switch Card Game");
    gtk_widget_set_focusable(switch_btn, FALSE);
    g_signal_connect(switch_btn, "clicked", G_CALLBACK(on_subgame_btn_clicked), g);
    gtk_box_append(GTK_BOX(header), switch_btn);

    gtk_box_append(GTK_BOX(box), header);

    /* Drawing area */
    g->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g->drawing_area, TRUE);
    gtk_widget_set_hexpand(g->drawing_area, TRUE);
    gtk_widget_set_focusable(g->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->drawing_area), cards_draw_func, g, NULL);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_cards_click), g);
    gtk_widget_add_controller(g->drawing_area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_cards_key_press), g);
    gtk_widget_add_controller(g->drawing_area, key_ctrl);

    gtk_box_append(GTK_BOX(box), g->drawing_area);

    /* Footer */
    GtkWidget* footer = gtk_label_new("Blackjack: D: Deal, H: Hit, S: Stand  |  Solitaire & Memory: Click cards to play  |  R: Reset");
    gtk_widget_set_margin_bottom(footer, 6);
    gtk_box_append(GTK_BOX(box), footer);

    g_object_set_data_full(G_OBJECT(box), "cards-data", g, g_free);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->drawing_area);

    return box;
}

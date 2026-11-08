/* Zork: The Great Underground Empire interactive text adventure for LookinFiles */
#include "game_zork.h"
#include "theme/palette.h"
#include "theme/fonts.h"
#include "theme/icons.h"
#include "core/app_events.h"
#include <string.h>

#define MAX_ITEMS 20
#define MAX_ROOMS 12
#define MAX_HISTORY 30

typedef enum {
    ROOM_WEST_OF_HOUSE,
    ROOM_NORTH_OF_HOUSE,
    ROOM_SOUTH_OF_HOUSE,
    ROOM_BEHIND_HOUSE,
    ROOM_KITCHEN,
    ROOM_LIVING_ROOM,
    ROOM_CELLAR,
    ROOM_TROLL_ROOM,
    ROOM_MAZE,
    ROOM_FOREST,
    ROOM_CLEARING,
    ROOM_CANYON_VIEW
} RoomId;

typedef struct {
    const gchar* name;
    const gchar* desc;
    gint location; /* RoomId or -1 (inventory) or -2 (destroyed) or -3 (inside container) */
    gboolean takeable;
} Item;

typedef struct {
    AppData* app;
    GtkWidget* text_view;
    GtkTextBuffer* text_buf;
    GtkWidget* entry;
    GtkWidget* score_label;

    RoomId current_room;
    gint score;
    gint moves;
    gboolean lantern_lit;
    gboolean rug_moved;
    gboolean trapdoor_open;
    gboolean mailbox_open;
    gboolean window_open;
    gboolean troll_alive;
    gboolean game_over;

    Item items[MAX_ITEMS];

    /* History */
    gchar* history[MAX_HISTORY];
    gint history_count;
    gint history_pos;
} ZorkGame;

static void zork_print(ZorkGame* g, const gchar* text) {
    if (g == NULL || g->text_buf == NULL) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g->text_buf, &end);
    gtk_text_buffer_insert(g->text_buf, &end, text, -1);
    gtk_text_buffer_insert(g->text_buf, &end, "\n", -1);

    /* Auto scroll to bottom */
    GtkTextMark* mark = gtk_text_buffer_get_insert(g->text_buf);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g->text_view), mark, 0.0, TRUE, 0.0, 1.0);
}

static void zork_init_items(ZorkGame* g) {
    g->items[0] = (Item){"mailbox", "A small mailbox.", ROOM_WEST_OF_HOUSE, FALSE};
    g->items[1] = (Item){"leaflet", "'WELCOME TO ZORK!\nZORK is a game of adventure, danger, and low cunning. In it you will explore some of the most amazing territory ever seen by mortals.'", -3, TRUE};
    g->items[2] = (Item){"brass lantern", "A battery-powered brass lantern.", ROOM_LIVING_ROOM, TRUE};
    g->items[3] = (Item){"elvish sword", "An ancient elvish sword of great antiquity. It faintly glows with blue light when dangers lurk nearby.", ROOM_LIVING_ROOM, TRUE};
    g->items[4] = (Item){"persian rug", "An ornate oriental rug lying in the center of the living room.", ROOM_LIVING_ROOM, FALSE};
    g->items[5] = (Item){"trap door", "A closed wooden trap door beneath the rug.", ROOM_LIVING_ROOM, FALSE};
    g->items[6] = (Item){"bottle of water", "A clear glass bottle filled with cool water.", ROOM_KITCHEN, TRUE};
    g->items[7] = (Item){"sack of garlic", "A smelly clove sack of fresh garlic.", ROOM_KITCHEN, TRUE};
    g->items[8] = (Item){"trophy case", "A glass trophy case for displaying precious treasures.", ROOM_LIVING_ROOM, FALSE};
    g->items[9] = (Item){"nasty troll", "A ferocious armed troll brandishing a bloody axe!", ROOM_TROLL_ROOM, FALSE};
    g->items[10] = (Item){"bloody axe", "A heavy battle axe stained with dried blood.", ROOM_TROLL_ROOM, FALSE};
    g->items[11] = (Item){"platinum bar", "A heavy bar of pure glittering platinum!", ROOM_MAZE, TRUE};
    g->items[12] = (Item){"gold coins", "A leather pouch filled with rare ancient gold coins!", ROOM_CANYON_VIEW, TRUE};
}

static void zork_describe_room(ZorkGame* g) {
    /* Dark room check */
    if ((g->current_room == ROOM_CELLAR || g->current_room == ROOM_TROLL_ROOM || g->current_room == ROOM_MAZE) && !g->lantern_lit) {
        zork_print(g, "\nIt is pitch black. You are likely to be eaten by a grue.");
        return;
    }

    switch (g->current_room) {
        case ROOM_WEST_OF_HOUSE:
            zork_print(g, "\nWest of House");
            zork_print(g, "You are standing in an open field west of a white house, with a boarded front door.");
            zork_print(g, "There is a small mailbox here.");
            break;
        case ROOM_NORTH_OF_HOUSE:
            zork_print(g, "\nNorth of House");
            zork_print(g, "You are facing the north side of a white house. There is no door here, and all the windows are boarded up.");
            break;
        case ROOM_SOUTH_OF_HOUSE:
            zork_print(g, "\nSouth of House");
            zork_print(g, "You are facing the south side of the white house. There is no door here, and the windows are heavily boarded.");
            break;
        case ROOM_BEHIND_HOUSE:
            zork_print(g, "\nBehind House");
            zork_print(g, "You are behind the white house. A path leads into the dense forest to the east.");
            if (g->window_open) {
                zork_print(g, "The small kitchen window is wide open.");
            } else {
                zork_print(g, "A small window is slightly ajar.");
            }
            break;
        case ROOM_KITCHEN:
            zork_print(g, "\nKitchen");
            zork_print(g, "You are in the kitchen of the white house. A chimney leads up into the dark.");
            zork_print(g, "Passages lead west into the living room and east through the window.");
            break;
        case ROOM_LIVING_ROOM:
            zork_print(g, "\nLiving Room");
            zork_print(g, "You are in the living room. There is a doorway to the east, and a large oriental rug in the center.");
            if (g->trapdoor_open) {
                zork_print(g, "A trap door in the floor is open, with a steep ramp leading down into darkness.");
            } else if (g->rug_moved) {
                zork_print(g, "With the rug moved aside, a closed trap door is visible in the floorboards.");
            }
            break;
        case ROOM_CELLAR:
            zork_print(g, "\nCellar");
            zork_print(g, "You are in a damp cellar. A ramp leads up to the living room trap door.");
            zork_print(g, "A dark corridor leads north.");
            break;
        case ROOM_TROLL_ROOM:
            zork_print(g, "\nThe Troll Room");
            zork_print(g, "This is a small room with rough stone walls. Passages lead south and east.");
            if (g->troll_alive) {
                zork_print(g, "A menacing troll holding a bloody axe stands in the center, barring the east exit!");
                zork_print(g, "Your sword glows with a bright blue aura!");
            }
            break;
        case ROOM_MAZE:
            zork_print(g, "\nMaze of Twisty Little Passages");
            zork_print(g, "You are in a maze of twisty little passages, all alike.");
            break;
        case ROOM_FOREST:
            zork_print(g, "\nForest Path");
            zork_print(g, "You are in a shadowy forest with towering oak trees. A faint trail leads north and west.");
            break;
        case ROOM_CLEARING:
            zork_print(g, "\nClearing");
            zork_print(g, "You are in a small clearing in the forest. Sunlight filters through the canopy.");
            break;
        case ROOM_CANYON_VIEW:
            zork_print(g, "\nCanyon View");
            zork_print(g, "You are on a high ledge overlooking a breathtaking canyon and raging underground river.");
            break;
    }

    /* Print items in room */
    for (gint i = 0; i < MAX_ITEMS; i++) {
        if (g->items[i].location == (gint)g->current_room) {
            gchar* msg = g_strdup_printf("There is a %s here.", g->items[i].name);
            zork_print(g, msg);
            g_free(msg);
        }
    }
}

static void zork_execute_command(ZorkGame* g, const gchar* input) {
    if (g == NULL || input == NULL) return;

    /* Trim whitespace */
    gchar* cmd = g_strstrip(g_strdup(input));
    if (cmd[0] == '\0') {
        g_free(cmd);
        return;
    }

    /* Print echo with prompt */
    gchar* echo = g_strdup_printf("> %s", cmd);
    zork_print(g, echo);
    g_free(echo);

    g->moves++;

    /* Lowercase for parser */
    gchar* lower = g_ascii_strdown(cmd, -1);
    g_free(cmd);

    /* Grue death check */
    if ((g->current_room == ROOM_CELLAR || g->current_room == ROOM_TROLL_ROOM || g->current_room == ROOM_MAZE) &&
        !g->lantern_lit && strcmp(lower, "turn on lamp") != 0 && strcmp(lower, "light lantern") != 0 &&
        strcmp(lower, "turn on lantern") != 0) {
        zork_print(g, "Oh no! You have walked into the dark without light and have been eaten by a grue!");
        zork_print(g, "\n****  You have died  ****\nType 'restart' to try again.");
        g->game_over = TRUE;
        g_free(lower);
        return;
    }

    /* Navigation Commands */
    if (strcmp(lower, "n") == 0 || strcmp(lower, "north") == 0) {
        if (g->current_room == ROOM_WEST_OF_HOUSE) g->current_room = ROOM_NORTH_OF_HOUSE;
        else if (g->current_room == ROOM_NORTH_OF_HOUSE) g->current_room = ROOM_FOREST;
        else if (g->current_room == ROOM_CELLAR) g->current_room = ROOM_TROLL_ROOM;
        else if (g->current_room == ROOM_FOREST) g->current_room = ROOM_CLEARING;
        else zork_print(g, "You can't go that way.");
        zork_describe_room(g);
    } else if (strcmp(lower, "s") == 0 || strcmp(lower, "south") == 0) {
        if (g->current_room == ROOM_WEST_OF_HOUSE) g->current_room = ROOM_SOUTH_OF_HOUSE;
        else if (g->current_room == ROOM_SOUTH_OF_HOUSE) g->current_room = ROOM_FOREST;
        else if (g->current_room == ROOM_TROLL_ROOM) g->current_room = ROOM_CELLAR;
        else if (g->current_room == ROOM_CLEARING) g->current_room = ROOM_FOREST;
        else zork_print(g, "You can't go that way.");
        zork_describe_room(g);
    } else if (strcmp(lower, "e") == 0 || strcmp(lower, "east") == 0) {
        if (g->current_room == ROOM_NORTH_OF_HOUSE || g->current_room == ROOM_SOUTH_OF_HOUSE) g->current_room = ROOM_BEHIND_HOUSE;
        else if (g->current_room == ROOM_BEHIND_HOUSE) {
            if (g->window_open) g->current_room = ROOM_KITCHEN;
            else zork_print(g, "The kitchen window is closed. Maybe you can open it.");
        } else if (g->current_room == ROOM_LIVING_ROOM) g->current_room = ROOM_KITCHEN;
        else if (g->current_room == ROOM_TROLL_ROOM) {
            if (g->troll_alive) zork_print(g, "The angry troll blocks your path with his axe!");
            else g->current_room = ROOM_MAZE;
        } else if (g->current_room == ROOM_MAZE) g->current_room = ROOM_CANYON_VIEW;
        else zork_print(g, "You can't go that way.");
        zork_describe_room(g);
    } else if (strcmp(lower, "w") == 0 || strcmp(lower, "west") == 0) {
        if (g->current_room == ROOM_NORTH_OF_HOUSE || g->current_room == ROOM_SOUTH_OF_HOUSE) g->current_room = ROOM_WEST_OF_HOUSE;
        else if (g->current_room == ROOM_BEHIND_HOUSE) g->current_room = ROOM_SOUTH_OF_HOUSE;
        else if (g->current_room == ROOM_KITCHEN) g->current_room = ROOM_LIVING_ROOM;
        else if (g->current_room == ROOM_CANYON_VIEW) g->current_room = ROOM_MAZE;
        else if (g->current_room == ROOM_MAZE) g->current_room = ROOM_TROLL_ROOM;
        else zork_print(g, "You can't go that way.");
        zork_describe_room(g);
    } else if (strcmp(lower, "d") == 0 || strcmp(lower, "down") == 0) {
        if (g->current_room == ROOM_LIVING_ROOM) {
            if (g->trapdoor_open) {
                g->current_room = ROOM_CELLAR;
                zork_describe_room(g);
            } else {
                zork_print(g, "The trap door is closed.");
            }
        } else {
            zork_print(g, "You can't go down here.");
        }
    } else if (strcmp(lower, "u") == 0 || strcmp(lower, "up") == 0) {
        if (g->current_room == ROOM_CELLAR) {
            g->current_room = ROOM_LIVING_ROOM;
            zork_describe_room(g);
        } else {
            zork_print(g, "You can't go up here.");
        }
    }
    /* Look */
    else if (strcmp(lower, "look") == 0 || strcmp(lower, "l") == 0) {
        zork_describe_room(g);
    }
    /* Inventory */
    else if (strcmp(lower, "i") == 0 || strcmp(lower, "inventory") == 0) {
        zork_print(g, "You are carrying:");
        gboolean any = FALSE;
        for (gint i = 0; i < MAX_ITEMS; i++) {
            if (g->items[i].location == -1) {
                gchar* msg = g_strdup_printf("  A %s", g->items[i].name);
                zork_print(g, msg);
                g_free(msg);
                any = TRUE;
            }
        }
        if (!any) zork_print(g, "  Nothing (your hands are empty).");
    }
    /* Open Window / Mailbox / Trapdoor */
    else if (strstr(lower, "open window") != NULL) {
        if (g->current_room == ROOM_BEHIND_HOUSE || g->current_room == ROOM_KITCHEN) {
            g->window_open = TRUE;
            zork_print(g, "With great effort, you pry open the small kitchen window.");
        } else {
            zork_print(g, "There is no window here to open.");
        }
    } else if (strstr(lower, "open mailbox") != NULL) {
        if (g->current_room == ROOM_WEST_OF_HOUSE) {
            g->mailbox_open = TRUE;
            g->items[1].location = ROOM_WEST_OF_HOUSE; /* Reveal leaflet */
            zork_print(g, "Opening the small mailbox reveals a leaflet.");
        } else {
            zork_print(g, "There is no mailbox here.");
        }
    } else if (strstr(lower, "open trap") != NULL) {
        if (g->current_room == ROOM_LIVING_ROOM && g->rug_moved) {
            g->trapdoor_open = TRUE;
            zork_print(g, "The wooden trap door swings open, revealing a dark stone ramp leading down.");
        } else {
            zork_print(g, "You don't see any trap door you can open.");
        }
    } else if (strstr(lower, "move rug") != NULL) {
        if (g->current_room == ROOM_LIVING_ROOM) {
            g->rug_moved = TRUE;
            zork_print(g, "With a heave, you roll aside the Persian rug, exposing a closed wooden trap door!");
        } else {
            zork_print(g, "There is no rug here.");
        }
    }
    /* Light / Lantern */
    else if (strstr(lower, "light") != NULL || strstr(lower, "turn on") != NULL) {
        if (g->items[2].location == -1 || g->items[2].location == (gint)g->current_room) {
            g->lantern_lit = TRUE;
            zork_print(g, "The brass lantern clicks on, casting a warm beam of golden light.");
            zork_describe_room(g);
        } else {
            zork_print(g, "You don't have a lantern to light.");
        }
    } else if (strstr(lower, "turn off") != NULL || strstr(lower, "extinguish") != NULL) {
        g->lantern_lit = FALSE;
        zork_print(g, "You switch off the brass lantern.");
    }
    /* Attack Troll */
    else if (strstr(lower, "kill troll") != NULL || strstr(lower, "attack troll") != NULL) {
        if (g->current_room == ROOM_TROLL_ROOM && g->troll_alive) {
            if (g->items[3].location == -1) { /* Has sword */
                g->troll_alive = FALSE;
                g->score += 25;
                zork_print(g, "You lunge forward with the glowing elvish sword!");
                zork_print(g, "A mighty thrust slays the menacing troll! His axe clatters to the ground, and the east passage is now clear!");
            } else {
                zork_print(g, "Attacking a giant troll with your bare hands is suicidal! The troll swats you aside!");
            }
        } else {
            zork_print(g, "There is no troll here to fight.");
        }
    }
    /* Read Leaflet */
    else if (strstr(lower, "read") != NULL) {
        if (strstr(lower, "leaflet") != NULL && (g->items[1].location == -1 || g->items[1].location == (gint)g->current_room)) {
            zork_print(g, g->items[1].desc);
        } else {
            zork_print(g, "There is nothing readable here.");
        }
    }
    /* Take / Get */
    else if (strncmp(lower, "take ", 5) == 0 || strncmp(lower, "get ", 4) == 0) {
        const gchar* target = (strncmp(lower, "take ", 5) == 0) ? (lower + 5) : (lower + 4);
        gboolean found = FALSE;
        for (gint i = 0; i < MAX_ITEMS; i++) {
            if (strstr(g->items[i].name, target) != NULL) {
                if (g->items[i].location == (gint)g->current_room) {
                    if (g->items[i].takeable) {
                        g->items[i].location = -1;
                        gchar* msg = g_strdup_printf("Taken: %s.", g->items[i].name);
                        zork_print(g, msg);
                        g_free(msg);
                    } else {
                        zork_print(g, "That is too heavy or securely attached to move.");
                    }
                    found = TRUE;
                    break;
                } else if (g->items[i].location == -1) {
                    zork_print(g, "You already have that in your inventory.");
                    found = TRUE;
                    break;
                }
            }
        }
        if (!found) zork_print(g, "You don't see that here.");
    }
    /* Put treasure in trophy case */
    else if (strstr(lower, "put") != NULL && strstr(lower, "case") != NULL) {
        if (g->current_room == ROOM_LIVING_ROOM) {
            if (g->items[11].location == -1) { /* Platinum bar */
                g->items[11].location = ROOM_LIVING_ROOM;
                g->score += 50;
                zork_print(g, "You place the platinum bar into the trophy case! (+50 points!)");
            } else if (g->items[12].location == -1) { /* Gold coins */
                g->items[12].location = ROOM_LIVING_ROOM;
                g->score += 50;
                zork_print(g, "You place the gold coins into the trophy case! (+50 points!)");
            } else {
                zork_print(g, "You don't have any treasures to place in the case.");
            }
        } else {
            zork_print(g, "There is no trophy case here.");
        }
    }
    /* Score */
    else if (strcmp(lower, "score") == 0) {
        gchar* s = g_strdup_printf("Your score is %d (out of 350 total points) in %d moves.", g->score, g->moves);
        zork_print(g, s);
        g_free(s);
    }
    /* Help */
    else if (strcmp(lower, "help") == 0) {
        zork_print(g, "Useful Commands:");
        zork_print(g, "  n, s, e, w, u, d : Directions to move");
        zork_print(g, "  look (l)         : Look around room");
        zork_print(g, "  inventory (i)    : Check your items");
        zork_print(g, "  take <item>      : Pick up an item");
        zork_print(g, "  open <item>      : Open doors/boxes/windows");
        zork_print(g, "  turn on lamp     : Light your lantern in the dark");
        zork_print(g, "  attack troll     : Slay the vicious troll");
        zork_print(g, "  score            : Check your adventure score");
    }
    /* Restart */
    else if (strcmp(lower, "restart") == 0) {
        g->current_room = ROOM_WEST_OF_HOUSE;
        g->score = 0;
        g->moves = 0;
        g->lantern_lit = FALSE;
        g->rug_moved = FALSE;
        g->trapdoor_open = FALSE;
        g->mailbox_open = FALSE;
        g->window_open = FALSE;
        g->troll_alive = TRUE;
        g->game_over = FALSE;
        zork_init_items(g);
        zork_print(g, "\n=== RESTARTING ZORK ===\n");
        zork_describe_room(g);
    } else {
        zork_print(g, "I don't understand that command. Type 'help' for ideas.");
    }

    g_free(lower);

    /* Update score header */
    gchar* stat = g_strdup_printf("Score: %d   Moves: %d", g->score, g->moves);
    gtk_label_set_text(GTK_LABEL(g->score_label), stat);
    g_free(stat);
}

static void on_entry_activate(GtkEntry* entry, gpointer data) {
    ZorkGame* g = (ZorkGame*)data;
    if (g == NULL) return;

    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text && text[0] != '\0') {
        /* Add to history */
        if (g->history_count < MAX_HISTORY) {
            g->history[g->history_count++] = g_strdup(text);
        } else {
            g_free(g->history[0]);
            memmove(g->history, g->history + 1, sizeof(gchar*) * (MAX_HISTORY - 1));
            g->history[MAX_HISTORY - 1] = g_strdup(text);
        }
        g->history_pos = g->history_count;

        zork_execute_command(g, text);
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
    }
}

static gboolean on_entry_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    ZorkGame* g = (ZorkGame*)data;
    if (g == NULL) return FALSE;

    if (keyval == GDK_KEY_Up) {
        if (g->history_count > 0 && g->history_pos > 0) {
            g->history_pos--;
            gtk_editable_set_text(GTK_EDITABLE(g->entry), g->history[g->history_pos]);
            gtk_editable_set_position(GTK_EDITABLE(g->entry), -1);
            return TRUE;
        }
    } else if (keyval == GDK_KEY_Down) {
        if (g->history_pos < g->history_count - 1) {
            g->history_pos++;
            gtk_editable_set_text(GTK_EDITABLE(g->entry), g->history[g->history_pos]);
            gtk_editable_set_position(GTK_EDITABLE(g->entry), -1);
            return TRUE;
        } else if (g->history_pos == g->history_count - 1) {
            g->history_pos++;
            gtk_editable_set_text(GTK_EDITABLE(g->entry), "");
            return TRUE;
        }
    }
    return FALSE;
}

static void on_zork_destroy(gpointer data) {
    ZorkGame* g = (ZorkGame*)data;
    if (g == NULL) return;
    for (gint i = 0; i < g->history_count; i++) {
        g_free(g->history[i]);
    }
    g_free(g);
}

GtkWidget* game_zork_create_tab(AppData* app) {
    ZorkGame* g = g_new0(ZorkGame, 1);
    g->app = app;
    g->current_room = ROOM_WEST_OF_HOUSE;
    g->troll_alive = TRUE;
    zork_init_items(g);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);

    /* Header */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget* title = gtk_label_new("<b>ZORK: THE GREAT UNDERGROUND EMPIRE</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(header), title);

    g->score_label = gtk_label_new("Score: 0   Moves: 0");
    gtk_widget_set_hexpand(g->score_label, TRUE);
    gtk_box_append(GTK_BOX(header), g->score_label);

    gtk_box_append(GTK_BOX(box), header);

    /* Terminal Scrolled Window */
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    g->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g->text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g->text_view), 14);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(g->text_view), 14);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g->text_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(g->text_view), 12);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g->text_view), TRUE);

    g->text_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g->text_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g->text_view);
    gtk_box_append(GTK_BOX(box), scroll);

    /* Command prompt input box at bottom */
    GtkWidget* input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(input_box, 14);
    gtk_widget_set_margin_end(input_box, 14);
    gtk_widget_set_margin_top(input_box, 6);
    gtk_widget_set_margin_bottom(input_box, 10);

    GtkWidget* prompt_lbl = gtk_label_new(">");
    gtk_box_append(GTK_BOX(input_box), prompt_lbl);

    g->entry = gtk_entry_new();
    gtk_widget_set_hexpand(g->entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(g->entry), "Type a command (e.g. look, n, s, take lamp, open mailbox, help)...");
    g_signal_connect(g->entry, "activate", G_CALLBACK(on_entry_activate), g);

    GtkEventController* entry_ctrl = gtk_event_controller_key_new();
    g_signal_connect(entry_ctrl, "key-pressed", G_CALLBACK(on_entry_key_press), g);
    gtk_widget_add_controller(g->entry, entry_ctrl);

    gtk_box_append(GTK_BOX(input_box), g->entry);
    gtk_box_append(GTK_BOX(box), input_box);

    /* Welcome message and room description */
    zork_print(g, "============================================================");
    zork_print(g, "           ZORK I: The Great Underground Empire");
    zork_print(g, "    Copyright (c) 1981, 1982, 1983 Infocom, Inc.");
    zork_print(g, "============================================================");
    zork_describe_room(g);

    g_object_set_data_full(G_OBJECT(box), "zork-data", g, on_zork_destroy);
    g_object_set_data(G_OBJECT(box), "game-focus-widget", g->entry);

    return box;
}

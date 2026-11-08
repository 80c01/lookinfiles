/* Games mode: secret arcade and retro games explorer view and player. */
#include "games.h"
#include "i18n.h"
#include "app_events.h"
#include "icons.h"
#include "palette.h"
#include "fonts.h"
#include "explorer_row.h"
#include "explorer_tree.h"
#include "tree_nav.h"
#include "focus_route.h"
#include "game_pacman.h"
#include "game_tetris.h"
#include "game_space.h"
#include "game_fighter.h"
#include "game_chess.h"
#include "game_cards.h"
#include "game_zork.h"

/* Secret sequence token history buffer */
static gchar s_history[32] = {0};
static gint s_history_len = 0;
static gint64 s_key_times_us[32] = {0}; /* when each history token was typed */

/* Longest pause allowed between two keys of the secret sequence */
#define SEQUENCE_KEY_TIMEOUT_US (1000 * G_TIME_SPAN_MILLISECOND)
/* Longest time allowed to type the whole secret sequence */
#define SEQUENCE_TOTAL_TIMEOUT_US (4000 * G_TIME_SPAN_MILLISECOND)
#define SEQUENCE_LEN 11 /* length of "UUDDLRLRABE" */

void games_init(AppData* app) {
    if (app == NULL) return;
    app->game_mode_unlocked = FALSE;
    app->game_mode_just_unlocked = FALSE;
}

/* Check keyval in help panel or user guide for the Konami / easter egg sequence:
 * Up Up Down Down Left Right Left Right A B Return (or B A Return).
 * The sequence restarts when a key comes more than SEQUENCE_KEY_TIMEOUT_US
 * after the previous one, and must be typed within SEQUENCE_TOTAL_TIMEOUT_US. */
gboolean games_check_help_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL) return FALSE;

    /* Ignore when Ctrl, Alt or Super are held */
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0) {
        s_history_len = 0;
        s_history[0] = '\0';
        return FALSE;
    }

    /* Standalone modifier presses should not reset history */
    if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
        keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
        keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
        keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
        keyval == GDK_KEY_Caps_Lock || keyval == GDK_KEY_Num_Lock) {
        return FALSE;
    }

    gchar token = 'X';
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up || keyval == GDK_KEY_k || keyval == GDK_KEY_K) {
        token = 'U';
    } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j || keyval == GDK_KEY_J) {
        token = 'D';
    } else if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left || keyval == GDK_KEY_h || keyval == GDK_KEY_H) {
        token = 'L';
    } else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right || keyval == GDK_KEY_l || keyval == GDK_KEY_L) {
        token = 'R';
    } else if (keyval == GDK_KEY_a || keyval == GDK_KEY_A) {
        token = 'A';
    } else if (keyval == GDK_KEY_b || keyval == GDK_KEY_B) {
        token = 'B';
    } else if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter || keyval == GDK_KEY_space) {
        token = 'E';
    }

    if (token == 'X') {
        s_history_len = 0;
        s_history[0] = '\0';
        return FALSE;
    }

    gint64 now = g_get_monotonic_time();
    if (s_history_len > 0 &&
        now - s_key_times_us[s_history_len - 1] > SEQUENCE_KEY_TIMEOUT_US) {
        s_history_len = 0;
        s_history[0] = '\0';
    }

    if (s_history_len >= (gint)sizeof(s_history) - 1) {
        memmove(s_history, s_history + 1, sizeof(s_history) - 2);
        memmove(s_key_times_us, s_key_times_us + 1,
                (sizeof(s_history) - 2) * sizeof(s_key_times_us[0]));
        s_history_len = (gint)sizeof(s_history) - 2;
    }
    s_key_times_us[s_history_len] = now;
    s_history[s_history_len++] = token;
    s_history[s_history_len] = '\0';

    gboolean matched = FALSE;
    if ((g_str_has_suffix(s_history, "UUDDLRLRABE") ||
         g_str_has_suffix(s_history, "UUDDLRLRBAE")) &&
        now - s_key_times_us[s_history_len - SEQUENCE_LEN] <= SEQUENCE_TOTAL_TIMEOUT_US) {
        matched = TRUE;
    }

    if (matched) {
        s_history_len = 0;
        s_history[0] = '\0';
        app->game_mode_unlocked = TRUE;
        app->game_mode_just_unlocked = TRUE;
        if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
            app->workspaces[app->current_ws].games_unlocked = TRUE;
        }

        /* Close keyboard shortcuts cheat sheet if open */
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            gtk_widget_set_visible(app->help_window, FALSE);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }

        /* Ensure explorer panel is visible */
        if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
            gtk_widget_set_visible(app->left_vbox, TRUE);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }

        /* Switch notebook away from user guide back to page 0 (Preview) to show game details */
        if (app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }

        /* Switch sidebar to Games view */
        explorer_set_view(app, SIDEBAR_VIEW_GAMES);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        games_refresh(app);

        if (app->tree_view != NULL) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        }

        app_notice(app, _("★ Game Mode Unlocked! Select a game to play ★"), FALSE);
        return TRUE;
    }

    return FALSE;
}

/* Append all available games to the explorer root */
void games_append_rows(ExplorerRow* root, AppData* app) {
    (void)app;
    if (root == NULL) return;

    const gchar* fg_badge = palette_color(PAL_ACCENT_FG);

    /* 1. Pac-Man */
    ExplorerRow* r1 = explorer_row_append(root, "game", "pacman", "Pac-Man", "game:pacman");
    r1->glyph = g_strdup("applications-games");
    r1->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Arcade"));

    /* 2. Tetris */
    ExplorerRow* r2 = explorer_row_append(root, "game", "tetris", "Tetris", "game:tetris");
    r2->glyph = g_strdup("applications-games");
    r2->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Puzzle"));

    /* 3. Space Commander */
    ExplorerRow* r3 = explorer_row_append(root, "game", "space_commander", "Space Commander", "game:space_commander");
    r3->glyph = g_strdup("applications-games");
    r3->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Arcade"));

    /* 4. Street Fighter */
    ExplorerRow* r4 = explorer_row_append(root, "game", "street_fighter", "Street Fighter", "game:street_fighter");
    r4->glyph = g_strdup("applications-games");
    r4->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Fighting"));

    /* 5. Chess */
    ExplorerRow* r5 = explorer_row_append(root, "game", "chess", _("Chess"), "game:chess");
    r5->glyph = g_strdup("applications-games");
    r5->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Board"));

    /* 6. Card Games (Expandable folder) */
    ExplorerRow* cards_folder = explorer_row_append_container(root, "game_folder", "card_games", _("Card Games"), "game_folder:cards");
    cards_folder->glyph = g_strdup("folder");
    cards_folder->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, _("3 games"));

    ExplorerRow* c1 = explorer_row_append(cards_folder, "game", "cards_blackjack", "Blackjack (21)", "game:cards_blackjack");
    c1->glyph = g_strdup("applications-games");
    c1->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Cards"));

    ExplorerRow* c2 = explorer_row_append(cards_folder, "game", "cards_solitaire", _("Klondike Solitaire"), "game:cards_solitaire");
    c2->glyph = g_strdup("applications-games");
    c2->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Cards"));

    ExplorerRow* c3 = explorer_row_append(cards_folder, "game", "cards_memory", _("Memory Match"), "game:cards_memory");
    c3->glyph = g_strdup("applications-games");
    c3->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Cards"));

    /* 7. Zork */
    ExplorerRow* r7 = explorer_row_append(root, "game", "zork", "Zork: The Great Underground Empire", "game:zork");
    r7->glyph = g_strdup("applications-games");
    r7->badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", fg_badge, C_("game genre", "Adventure"));
}

void games_refresh(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_GAMES) {
        return;
    }

    ExplorerRow* selected = tree_nav_selected(app);
    gchar* sel_path = (selected != NULL && selected->path[0] != '\0') ? g_strdup(selected->path) : NULL;

    tree_nav_block_selection_handler(app);
    explorer_tree_clear(app->tree);
    games_append_rows(explorer_tree_get_root(app->tree), app);
    tree_nav_unblock_selection_handler(app);

    ExplorerRow* row = explorer_row_find_by_path(explorer_tree_get_root(app->tree), sel_path);
    if (row != NULL) {
        if (!reveal_row(app, row)) {
            select_first_row(app);
        }
    } else {
        select_first_row(app);
    }
    g_free(sel_path);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* Returns preview summary card text for the selected game */
gchar* games_get_preview_summary(ExplorerRow* row) {
    if (row == NULL) return NULL;
    const gchar* path = row->path ? row->path : "";

    if (g_str_has_prefix(path, "game_folder:cards")) {
        return g_strdup(
            "============================================================\n"
            "                   CARD GAMES COLLECTION\n"
            "============================================================\n\n"
            "Classic tabletop card games rendered with casino green felt!\n\n"
            "Included Games:\n"
            "  1. Blackjack (21)      - Beat the dealer without going bust!\n"
            "  2. Klondike Solitaire  - The timeless 7-column card puzzle.\n"
            "  3. Memory Match        - Flip and pair matching cards.\n\n"
            "▶ Expand folder and select a game, then press ENTER to play!\n");
    }

    if (g_str_has_prefix(path, "game:pacman")) {
        return g_strdup(
            "============================================================\n"
            "                        PAC-MAN\n"
            "============================================================\n\n"
            "Genre:   Arcade / Maze Classic (1980)\n"
            "Author:  Namco / LookinFiles Retro Arcade\n\n"
            "Objective:\n"
            "  Navigate the maze, eat all pellets and power energizers,\n"
            "  and avoid the colorful ghosts (Blinky, Pinky, Inky, Clyde)!\n"
            "  Eat an Energizer to turn the ghosts blue and munch them for\n"
            "  massive bonus points!\n\n"
            "Controls:\n"
            "  Arrow Keys / WASD : Change Pac-Man's direction\n"
            "  P                 : Pause / Resume game\n"
            "  R                 : Restart game\n"
            "  Ctrl+W            : Close game tab\n\n"
            "▶ Press ENTER or Double-Click to PLAY NOW!\n");
    }

    if (g_str_has_prefix(path, "game:tetris")) {
        return g_strdup(
            "============================================================\n"
            "                        TETRIS\n"
            "============================================================\n\n"
            "Genre:   Falling Block Puzzle (1984)\n"
            "Author:  Alexey Pajitnov / LookinFiles Retro Arcade\n\n"
            "Objective:\n"
            "  Manipulate falling tetrominoes to create complete horizontal\n"
            "  lines. Cleared lines vanish and grant points. The game speeds\n"
            "  up as your level rises!\n\n"
            "Features:\n"
            "  - Ghost piece drop guide\n"
            "  - Hold piece reserve (Shift / C)\n"
            "  - Next piece preview\n"
            "  - Super Rotation System (SRS) wall kicks\n\n"
            "Controls:\n"
            "  Left / Right      : Move piece\n"
            "  Up                : Rotate clockwise\n"
            "  Down              : Soft drop (faster fall)\n"
            "  Space             : Hard drop (instant lock)\n"
            "  C / Shift         : Hold piece\n"
            "  P                 : Pause / Resume\n"
            "  R                 : Restart game\n"
            "  Ctrl+W            : Close game tab\n\n"
            "▶ Press ENTER or Double-Click to PLAY NOW!\n");
    }

    if (g_str_has_prefix(path, "game:space_commander")) {
        return g_strdup(
            "============================================================\n"
            "                    SPACE COMMANDER\n"
            "============================================================\n\n"
            "Genre:   Arcade Space Shooter (1978 / 1981)\n"
            "Author:  LookinFiles Retro Arcade\n\n"
            "Objective:\n"
            "  Defend Earth against waves of alien invaders marching down!\n"
            "  Take shelter behind defense bunkers, dodge enemy bombs, and\n"
            "  shoot the mystery Command UFO flying across the top!\n\n"
            "Controls:\n"
            "  Left / Right      : Steer defense ship\n"
            "  Space             : Fire laser cannons\n"
            "  P                 : Pause / Resume\n"
            "  R                 : Restart game\n"
            "  Ctrl+W            : Close game tab\n\n"
            "▶ Press ENTER or Double-Click to PLAY NOW!\n");
    }

    if (g_str_has_prefix(path, "game:street_fighter")) {
        return g_strdup(
            "============================================================\n"
            "                    STREET FIGHTER\n"
            "============================================================\n\n"
            "Genre:   2D Martial Arts Fighting (1991)\n"
            "Author:  LookinFiles Retro Arcade\n\n"
            "Fighters: Ryu vs Ken\n\n"
            "Objective:\n"
            "  Engage in head-to-head martial arts combat! Deplete your\n"
            "  opponent's health bar across best-of-3 intense rounds!\n\n"
            "Attacks & Moves:\n"
            "  Left / Right      : Advance / Retreat (Walk back to block!)\n"
            "  Up                : Jump into the air\n"
            "  Down              : Crouch\n"
            "  A or J            : Fast Punch\n"
            "  S or K            : Power Kick\n"
            "  D or L            : Hadoken (Energy Fireball projectile!)\n"
            "  W or I            : Shoryuken (Dragon Uppercut!)\n"
            "  R                 : Rematch / Restart\n"
            "  Ctrl+W            : Close game tab\n\n"
            "▶ Press ENTER or Double-Click to FIGHT!\n");
    }

    if (g_str_has_prefix(path, "game:chess")) {
        return g_strdup(
            "============================================================\n"
            "                        CHESS\n"
            "============================================================\n\n"
            "Genre:   Grand Strategy Board Game (Classic)\n"
            "Author:  LookinFiles Intelligent Chess Engine\n\n"
            "Features:\n"
            "  - Play White vs built-in Minimax AI or 2-player local mode\n"
            "  - Full standard chess rules: Castling, En Passant, Promotion\n"
            "  - Check, Checkmate and Stalemate detection\n"
            "  - Legal move highlights\n"
            "  - Move history and captured piece displays\n\n"
            "Controls:\n"
            "  Mouse Click       : Select piece / destination square\n"
            "  Arrow Keys        : Move cursor square\n"
            "  Space / Enter     : Select piece or execute move\n"
            "  R                 : New Game\n"
            "  Ctrl+W            : Close game tab\n\n"
            "▶ Press ENTER or Double-Click to PLAY CHESS!\n");
    }

    if (g_str_has_prefix(path, "game:cards_blackjack")) {
        return g_strdup(
            "============================================================\n"
            "                   BLACKJACK (21)\n"
            "============================================================\n\n"
            "Genre:   Casino Card Game\n\n"
            "Objective:\n"
            "  Get a card total closer to 21 than the dealer without going\n"
            "  over. Dealer stands on 17. Blackjack pays 3 to 2!\n\n"
            "Actions:\n"
            "  Deal, Hit, Stand, Double Down, Change Bet ($10, $25, $50, $100)\n\n"
            "▶ Press ENTER or Double-Click to DEAL!\n");
    }

    if (g_str_has_prefix(path, "game:cards_solitaire")) {
        return g_strdup(
            "============================================================\n"
            "                 KLONDIKE SOLITAIRE\n"
            "============================================================\n\n"
            "Genre:   Card Patience Puzzle\n\n"
            "Objective:\n"
            "  Build 4 foundation piles from Ace up to King by suit.\n"
            "  Stack tableau columns descending in alternating colors.\n\n"
            "▶ Press ENTER or Double-Click to PLAY SOLITAIRE!\n");
    }

    if (g_str_has_prefix(path, "game:cards_memory")) {
        return g_strdup(
            "============================================================\n"
            "                    MEMORY MATCH\n"
            "============================================================\n\n"
            "Genre:   Concentration / Memory Card Game\n\n"
            "Objective:\n"
            "  Turn over two cards at a time to find all matching pairs\n"
            "  in as few attempts as possible!\n\n"
            "▶ Press ENTER or Double-Click to TEST YOUR MEMORY!\n");
    }

    if (g_str_has_prefix(path, "game:zork")) {
        return g_strdup(
            "============================================================\n"
            "         ZORK: THE GREAT UNDERGROUND EMPIRE\n"
            "============================================================\n\n"
            "Genre:   Interactive Fiction / Text Adventure (1977)\n"
            "Author:  Infocom / Tim Anderson, Marc Blank, Bruce Daniels, Dave Lebling\n\n"
            "Description:\n"
            "  You stand in an open field west of a white house, with a boarded\n"
            "  front door. A small mailbox rests nearby.\n\n"
            "  Explore the dark dungeon caverns, solve tricky puzzles, find\n"
            "  precious treasures, wield the glowing elvish sword against the\n"
            "  vicious troll, and beware of the dark -- for you are likely\n"
            "  to be eaten by a Grue!\n\n"
            "Controls:\n"
            "  Type commands in the interactive terminal prompt:\n"
            "  - Navigation: north (n), south (s), east (e), west (w), up, down\n"
            "  - Actions:    look, examine <item>, inventory (i), take <item>\n"
            "  - Mechanics:  open <box>, read <leaflet>, turn on lamp\n"
            "  - Combat:     kill troll with sword\n"
            "  - History:    Up / Down arrow keys cycle command history\n\n"
            "▶ Press ENTER or Double-Click to ENTER THE EMPIRE!\n");
    }

    return g_strdup_printf(_("Game: %s\nPress Enter to play!\n"), row->name);
}

void games_activate_selected(AppData* app) {
    if (app == NULL || app->tree == NULL) return;
    ExplorerRow* row = tree_nav_selected(app);
    if (row == NULL) return;

    if (explorer_row_is(row, "game_folder")) {
        explorer_tree_toggle_expanded(app->tree, row);
        return;
    }

    games_launch(app, row->path);
}

static void on_game_tab_close_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWidget* page = GTK_WIDGET(user_data);
    AppData* app = g_object_get_data(G_OBJECT(page), "app-data");
    if (app != NULL) {
        games_close_tab(app, page);
    }
}

void games_close_tab(AppData* app, GtkWidget* page) {
    if (app == NULL || app->notebook == NULL || page == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint num_pages = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < num_pages; i++) {
        if (gtk_notebook_get_nth_page(nb, i) == page) {
            gtk_notebook_remove_page(nb, i);
            break;
        }
    }
}

gboolean games_tab_close_shown(AppData* app) {
    if (app == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint cur = gtk_notebook_get_current_page(nb);
    if (cur < 0) return FALSE;
    GtkWidget* page = gtk_notebook_get_nth_page(nb, cur);
    if (page != NULL && g_object_get_data(G_OBJECT(page), "is-game-tab") != NULL) {
        games_close_tab(app, page);
        return TRUE;
    }
    return FALSE;
}

void games_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (g_object_get_data(G_OBJECT(page), "is-game-tab") == NULL) continue;
        if (ws->game_pages == NULL) ws->game_pages = g_ptr_array_new_with_free_func(g_object_unref);
        g_ptr_array_insert(ws->game_pages, 0, g_object_ref(page));
        gtk_notebook_remove_page(nb, i);
    }
}

void games_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->game_pages == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (guint i = 0; i < ws->game_pages->len; i++) {
        GtkWidget* page = g_ptr_array_index(ws->game_pages, i);
        gtk_notebook_append_page(nb, page, g_object_get_data(G_OBJECT(page), "game-tab-label"));
    }
    g_clear_pointer(&ws->game_pages, g_ptr_array_unref);
}

void games_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        games_workspace_hidden(app, ws);
    }
    g_clear_pointer(&ws->game_pages, g_ptr_array_unref);
}

void games_launch(AppData* app, const gchar* game_id) {
    if (app == NULL || app->notebook == NULL || game_id == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);

    /* If already open in a tab, switch to it */
    gint num_pages = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < num_pages; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        const gchar* existing_id = g_object_get_data(G_OBJECT(page), "game-id");
        if (existing_id != NULL && g_strcmp0(existing_id, game_id) == 0) {
            gtk_notebook_set_current_page(nb, i);
            GtkWidget* game_widget = g_object_get_data(G_OBJECT(page), "game-focus-widget");
            if (game_widget != NULL) {
                gtk_widget_grab_focus(game_widget);
            }
            return;
        }
    }

    GtkWidget* tab_content = NULL;
    const gchar* tab_title = _("Game");

    if (g_strcmp0(game_id, "game:pacman") == 0) {
        tab_content = game_pacman_create_tab(app);
        tab_title = "Pac-Man";
    } else if (g_strcmp0(game_id, "game:tetris") == 0) {
        tab_content = game_tetris_create_tab(app);
        tab_title = "Tetris";
    } else if (g_strcmp0(game_id, "game:space_commander") == 0) {
        tab_content = game_space_create_tab(app);
        tab_title = "Space Commander";
    } else if (g_strcmp0(game_id, "game:street_fighter") == 0) {
        tab_content = game_fighter_create_tab(app);
        tab_title = "Street Fighter";
    } else if (g_strcmp0(game_id, "game:chess") == 0) {
        tab_content = game_chess_create_tab(app);
        tab_title = _("Chess");
    } else if (g_strcmp0(game_id, "game:cards_blackjack") == 0) {
        tab_content = game_cards_create_tab(app, "blackjack");
        tab_title = "Blackjack";
    } else if (g_strcmp0(game_id, "game:cards_solitaire") == 0) {
        tab_content = game_cards_create_tab(app, "solitaire");
        tab_title = _("Solitaire");
    } else if (g_strcmp0(game_id, "game:cards_memory") == 0) {
        tab_content = game_cards_create_tab(app, "memory");
        tab_title = _("Memory Match");
    } else if (g_strcmp0(game_id, "game:zork") == 0) {
        tab_content = game_zork_create_tab(app);
        tab_title = "Zork";
    }

    if (tab_content == NULL) return;

    g_object_set_data(G_OBJECT(tab_content), "is-game-tab", GINT_TO_POINTER(1));
    g_object_set_data_full(G_OBJECT(tab_content), "game-id", g_strdup(game_id), g_free);
    g_object_set_data(G_OBJECT(tab_content), "app-data", app);

    /* Tab header: icon, title, close button */
    GtkWidget* tab_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon = glyph_image_new("applications-games");
    GtkWidget* label = gtk_label_new(tab_title);
    GtkWidget* close_btn = icon_button_new("window-close", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_focusable(close_btn, FALSE);
    gtk_widget_add_css_class(close_btn, "tab-close-btn");
    gtk_widget_set_tooltip_text(close_btn, _("Close Game (Ctrl+W)"));
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_game_tab_close_clicked), tab_content);

    gtk_box_append(GTK_BOX(tab_header), icon);
    gtk_box_append(GTK_BOX(tab_header), label);
    gtk_box_append(GTK_BOX(tab_header), close_btn);

    /* Kept with the page while its workspace is hidden */
    g_object_set_data_full(G_OBJECT(tab_content), "game-tab-label", g_object_ref(tab_header), g_object_unref);

    gint page_idx = gtk_notebook_append_page(nb, tab_content, tab_header);
    gtk_notebook_set_current_page(nb, page_idx);

    GtkWidget* game_widget = g_object_get_data(G_OBJECT(tab_content), "game-focus-widget");
    if (game_widget != NULL) {
        focus_route_mark_focus(game_widget);
        gtk_widget_grab_focus(game_widget);
    }
}

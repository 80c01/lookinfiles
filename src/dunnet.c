/* Dunnet text adventure (Ron Schnell, 1992) for the Emacs *dungeon* buffer. */
#include "dunnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUN_MAX_SCORE 100

typedef enum {
    DUN_ROOM_DEAD_END = 0,
    DUN_ROOM_EW_DIRT_ROAD,
    DUN_ROOM_FORK,
    DUN_ROOM_NE_SW_ROAD,
    DUN_ROOM_END_OF_ROAD,
    DUN_ROOM_SE_NW_ROAD,
    DUN_ROOM_HIDDEN_AREA,
    DUN_ROOM_HIGH_CLIFF,
    DUN_ROOM_BUILDING_HALLWAY,
    DUN_ROOM_MAILROOM,
    DUN_ROOM_COMPUTER_ROOM,
    DUN_ROOM_RECEIVING_ROOM,
    DUN_ROOM_NS_EW_TUNNEL,
    DUN_ROOM_WEIGHT_ROOM,
    DUN_ROOM_LAKEFRONT,
    DUN_ROOM_IN_LAKE,
    DUN_ROOM_NITRO_ROOM,
    DUN_ROOM_SAUNA_ENTRANCE,
    DUN_ROOM_SAUNA,
    DUN_ROOM_CAVE_WALL,
    DUN_ROOM_MAZE_1,
    DUN_ROOM_MAZE_2,
    DUN_ROOM_MAZE_3,
    DUN_ROOM_MUD_ROOM,
    DUN_ROOM_MUSEUM_LOBBY,
    DUN_ROOM_RESTROOM,
    DUN_ROOM_COMPUTER_MUSEUM,
    DUN_ROOM_TRAIN_STATION,
    DUN_ROOM_ENDGAME_HALL,
    DUN_ROOM_ENDGAME_TREASURE,
    DUN_ROOM_COUNT
} DunRoomId;

typedef enum {
    DUN_DIR_N = 0,
    DUN_DIR_S,
    DUN_DIR_E,
    DUN_DIR_W,
    DUN_DIR_NE,
    DUN_DIR_SE,
    DUN_DIR_NW,
    DUN_DIR_SW,
    DUN_DIR_U,
    DUN_DIR_D,
    DUN_DIR_IN,
    DUN_DIR_OUT,
    DUN_DIR_COUNT
} DunDir;

typedef enum {
    DUN_OBJ_SHOVEL = 0,
    DUN_OBJ_CPU,
    DUN_OBJ_BOULDER,
    DUN_OBJ_FOOD,
    DUN_OBJ_BEAR,
    DUN_OBJ_KEY,
    DUN_OBJ_BRACELET,
    DUN_OBJ_LAMP,
    DUN_OBJ_STRANGE,
    DUN_OBJ_WEIGHT,
    DUN_OBJ_PRESERVER,
    DUN_OBJ_AMETHYST,
    DUN_OBJ_NITRO,
    DUN_OBJ_WAX,
    DUN_OBJ_DIAMOND,
    DUN_OBJ_SILVER,
    DUN_OBJ_FLOPPY,
    DUN_OBJ_EGG,
    DUN_OBJ_COINS,
    DUN_OBJ_RUBY,
    DUN_OBJ_MONA_LISA,
    DUN_OBJ_PAPER,
    DUN_OBJ_COUNT
} DunObjId;

#define DUN_LOC_INVENTORY (-1)
#define DUN_LOC_NOWHERE   (-2)
#define DUN_LOC_URINAL    (-3)

typedef enum {
    DUN_MODE_DUNGEON = 0,
    DUN_MODE_UNIX_LOGIN,
    DUN_MODE_UNIX_PASS,
    DUN_MODE_UNIX_SHELL,
    DUN_MODE_FTP_USER,
    DUN_MODE_FTP_PASS,
    DUN_MODE_FTP_CMD,
    DUN_MODE_RLOGIN_PASS,
    DUN_MODE_DOS
} DunInputMode;

typedef struct {
    const gchar* title;
    const gchar* desc;
    gboolean dark;
    gint exits[DUN_DIR_COUNT];
} DunRoomDef;

typedef struct {
    const gchar* id_word;    /* primary noun ("shovel", "cpu", "lamp") */
    const gchar* alt_word;   /* secondary noun ("card", "lantern", "board") */
    const gchar* extra_word; /* third noun ("chip", "disk", "statuette") */
    const gchar* room_desc;  /* "There is a shovel here." */
    const gchar* inv_desc;   /* "A shovel" */
    const gchar* examine;    /* detailed look text */
    const gchar* unix_file;  /* "shovel.o" in /usr/toukmond, or NULL */
    gint initial_loc;
    gboolean takeable;
    gboolean treasure;
} DunObjDef;

struct DunnetState {
    DunRoomId room;
    DunInputMode mode;
    gint obj_loc[DUN_OBJ_COUNT];
    gboolean visited[DUN_ROOM_COUNT];
    gboolean treasure_scored[DUN_OBJ_COUNT];
    gint score;
    gint moves;
    gboolean dug_cpu;
    gboolean vax_running;
    gboolean lamp_lit;
    gboolean paper_uncompressed;
    gboolean dungeon_c_exists;
    gboolean ted_c_exists;
    gboolean paper_exists;
    gboolean ftp_binary;
    gboolean ftp_anonymous;
    gint sauna_level;
    gboolean wall_blasted;
    gboolean cabinet_open;
    gboolean floppy_in_pc;
    gboolean train_powered;
    gint endgame_step; /* 0..2 = riddles, 3 = solved */
    gboolean dead;
    gboolean won;
    gboolean quit;
    gchar* unix_user;
    gchar* unix_cwd;
};

#define X (-1)

static const DunRoomDef ROOMS[DUN_ROOM_COUNT] = {
    [DUN_ROOM_DEAD_END] = {
        "Dead end",
        "You are at a dead end of a dirt road. The road goes to the east.\n"
        "In the distance you can see that it will eventually fork off. The\n"
        "trees here are very tall royal palms, and they are spaced equidistant\n"
        "from each other.",
        FALSE, {X, X, DUN_ROOM_EW_DIRT_ROAD, X, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_EW_DIRT_ROAD] = {
        "E/W Dirt road",
        "You are on the continuation of a dirt road. There are more trees on\n"
        "both sides of you. The road continues to the east and west.",
        FALSE, {X, X, DUN_ROOM_FORK, DUN_ROOM_DEAD_END, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_FORK] = {
        "Fork",
        "You are at a fork of two passages, one to the northeast, and one to the\n"
        "southeast. The ground here seems very soft. You can also go back west.",
        FALSE, {X, X, X, DUN_ROOM_EW_DIRT_ROAD, DUN_ROOM_NE_SW_ROAD, DUN_ROOM_SE_NW_ROAD, X, X, X, X, X, X}
    },
    [DUN_ROOM_NE_SW_ROAD] = {
        "NE/SW road",
        "You are on a northeast/southwest road.",
        FALSE, {X, X, X, X, DUN_ROOM_END_OF_ROAD, X, X, DUN_ROOM_FORK, X, X, X, X}
    },
    [DUN_ROOM_END_OF_ROAD] = {
        "End of road",
        "You are at the end of the road. There is a building in front of you\n"
        "to the northeast, and the road leads back to the southwest.",
        FALSE, {X, X, X, X, DUN_ROOM_BUILDING_HALLWAY, X, X, DUN_ROOM_NE_SW_ROAD, X, X, DUN_ROOM_BUILDING_HALLWAY, X}
    },
    [DUN_ROOM_SE_NW_ROAD] = {
        "SE/NW road",
        "You are on a southeast/northwest road.",
        FALSE, {X, X, X, X, X, DUN_ROOM_HIDDEN_AREA, DUN_ROOM_FORK, X, X, X, X, X}
    },
    [DUN_ROOM_HIDDEN_AREA] = {
        "Hidden area",
        "You are in a hidden area of the forest. There are some vines here\n"
        "that seem to cover the ground to the southwest.",
        FALSE, {X, X, X, X, X, X, DUN_ROOM_SE_NW_ROAD, DUN_ROOM_HIGH_CLIFF, X, X, X, X}
    },
    [DUN_ROOM_HIGH_CLIFF] = {
        "High cliff",
        "You are on top of a high cliff. Below you is a steep drop into a\n"
        "canyon. The only way out is to the northeast.",
        FALSE, {X, X, X, X, DUN_ROOM_HIDDEN_AREA, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_BUILDING_HALLWAY] = {
        "Old Building hallway",
        "You are in the hallway of an old building. There are rooms to the east\n"
        "and west, and a door leading out to the southwest.",
        FALSE, {X, X, DUN_ROOM_MAILROOM, DUN_ROOM_COMPUTER_ROOM, X, X, X, DUN_ROOM_END_OF_ROAD, X, X, X, DUN_ROOM_END_OF_ROAD}
    },
    [DUN_ROOM_MAILROOM] = {
        "Mailroom",
        "You are in a mailroom. There are many bins where the mail is usually\n"
        "kept. The exit is to the west.",
        FALSE, {X, X, X, DUN_ROOM_BUILDING_HALLWAY, X, X, X, X, X, X, X, DUN_ROOM_BUILDING_HALLWAY}
    },
    [DUN_ROOM_COMPUTER_ROOM] = {
        "Computer room",
        "You are in a computer room. It seems like most of the equipment has\n"
        "been removed. There is a VAX 11/780 in front of you, however, with\n"
        "one of the cabinets wide open. A sign on the front of the machine\n"
        "says: This VAX is named 'pokey'. To type on the console, use the\n"
        "'type' command. The exit is to the east.",
        FALSE, {X, X, DUN_ROOM_BUILDING_HALLWAY, X, X, X, X, X, X, X, X, DUN_ROOM_BUILDING_HALLWAY}
    },
    [DUN_ROOM_RECEIVING_ROOM] = {
        "Receiving room",
        "You are in a round stone room with a door to the east. There is a\n"
        "sign on the wall that reads: 'receiving room'.",
        FALSE, {X, X, DUN_ROOM_NS_EW_TUNNEL, X, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_NS_EW_TUNNEL] = {
        "N/S-E/W junction",
        "You are at the intersection of two passages, one going north/south, and\n"
        "the other going east/west. There is a garbage chute here leading down.",
        TRUE, {DUN_ROOM_WEIGHT_ROOM, DUN_ROOM_MAZE_1, DUN_ROOM_SAUNA_ENTRANCE, DUN_ROOM_RECEIVING_ROOM, X, X, X, X, X, DUN_ROOM_MUD_ROOM, DUN_ROOM_MUD_ROOM, X}
    },
    [DUN_ROOM_WEIGHT_ROOM] = {
        "Weight room",
        "You are in a room with a pressure scale in the center. A hallway leads\n"
        "back to the south, and a heavy stone door leads to the north.",
        TRUE, {DUN_ROOM_LAKEFRONT, DUN_ROOM_NS_EW_TUNNEL, X, X, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_LAKEFRONT] = {
        "Lakefront",
        "You are standing on the shore of a large underground lake. A passage\n"
        "leads south, and you can swim out into the lake to the north.",
        TRUE, {DUN_ROOM_IN_LAKE, DUN_ROOM_WEIGHT_ROOM, X, X, X, X, X, X, X, X, DUN_ROOM_IN_LAKE, X}
    },
    [DUN_ROOM_IN_LAKE] = {
        "In lake",
        "You are floating in the middle of the underground lake. The shore is\n"
        "to the south, and a rocky ledge is to the north.",
        TRUE, {DUN_ROOM_NITRO_ROOM, DUN_ROOM_LAKEFRONT, X, X, X, X, X, X, X, X, X, DUN_ROOM_LAKEFRONT}
    },
    [DUN_ROOM_NITRO_ROOM] = {
        "Rocky ledge",
        "You are on a narrow rocky ledge on the north side of the lake. You\n"
        "can re-enter the water to the south.",
        TRUE, {X, DUN_ROOM_IN_LAKE, X, X, X, X, X, X, X, X, DUN_ROOM_IN_LAKE, X}
    },
    [DUN_ROOM_SAUNA_ENTRANCE] = {
        "Cave hallway",
        "You are in a rocky cave hallway. To the north is a wooden door with\n"
        "steam coming from underneath it. The passage continues east and west.",
        TRUE, {DUN_ROOM_SAUNA, X, DUN_ROOM_CAVE_WALL, DUN_ROOM_NS_EW_TUNNEL, X, X, X, X, X, X, DUN_ROOM_SAUNA, X}
    },
    [DUN_ROOM_SAUNA] = {
        "Sauna",
        "You are in a cedar-lined sauna. There is a dial on the wall that\n"
        "controls the temperature. The exit is to the south.",
        FALSE, {X, DUN_ROOM_SAUNA_ENTRANCE, X, X, X, X, X, X, X, X, X, DUN_ROOM_SAUNA_ENTRANCE}
    },
    [DUN_ROOM_CAVE_WALL] = {
        "Cracked cave wall",
        "You are at the east end of the cave passage. The north wall here has\n"
        "a deep crack in the rock, as if a room lies behind it. A passage\n"
        "leads back west.",
        TRUE, {X, X, X, DUN_ROOM_SAUNA_ENTRANCE, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_MAZE_1] = {
        "Maze",
        "You are in a maze of twisty little passages, all alike.",
        TRUE, {DUN_ROOM_NS_EW_TUNNEL, DUN_ROOM_MAZE_1, DUN_ROOM_MAZE_2, DUN_ROOM_MAZE_1, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_MAZE_2] = {
        "Maze",
        "You are in a little maze of twisty passages, all alike.",
        TRUE, {DUN_ROOM_MAZE_2, DUN_ROOM_MAZE_3, DUN_ROOM_MAZE_1, DUN_ROOM_MAZE_1, X, X, X, X, X, X, X, X}
    },
    [DUN_ROOM_MAZE_3] = {
        "Maze",
        "You are in a twisty little maze of passages, all alike.",
        TRUE, {DUN_ROOM_MAZE_2, DUN_ROOM_MAZE_1, DUN_ROOM_MAZE_2, DUN_ROOM_MAZE_1, X, X, X, X, DUN_ROOM_MUSEUM_LOBBY, DUN_ROOM_MUD_ROOM, X, X}
    },
    [DUN_ROOM_MUD_ROOM] = {
        "Mud room",
        "You are in a damp mud room at the bottom of the garbage chute. Stairs\n"
        "lead up to the northeast into a building lobby.",
        TRUE, {X, X, X, X, DUN_ROOM_MUSEUM_LOBBY, X, X, X, DUN_ROOM_MUSEUM_LOBBY, X, X, DUN_ROOM_MUSEUM_LOBBY}
    },
    [DUN_ROOM_MUSEUM_LOBBY] = {
        "Museum lobby",
        "You are in the lobby of the History of Technology Museum. Exhibits\n"
        "lie to the east, a restroom is to the west, a stairway leads down\n"
        "to the mud room, a passage leads south into the caves, and a subway\n"
        "station entrance is to the north.",
        FALSE, {DUN_ROOM_TRAIN_STATION, DUN_ROOM_CAVE_WALL, DUN_ROOM_COMPUTER_MUSEUM, DUN_ROOM_RESTROOM, X, X, X, DUN_ROOM_MUD_ROOM, X, DUN_ROOM_MUD_ROOM, X, X}
    },
    [DUN_ROOM_RESTROOM] = {
        "Restroom",
        "You are in the museum restroom. There is a urinal on the wall with\n"
        "a large drain pipe, and a trophy bin beside it. The exit is to the east.",
        FALSE, {X, X, DUN_ROOM_MUSEUM_LOBBY, X, X, X, X, X, X, X, X, DUN_ROOM_MUSEUM_LOBBY}
    },
    [DUN_ROOM_COMPUTER_MUSEUM] = {
        "Computer exhibit",
        "You are in the Vintage Computer Exhibit. On a desk sits a working\n"
        "IBM PC XT with a 5.25-inch floppy drive, and a locked steel cabinet\n"
        "with a 3-digit combination dial. To use the PC, type 'type'.\n"
        "The lobby is to the west.",
        FALSE, {X, X, X, DUN_ROOM_MUSEUM_LOBBY, X, X, X, X, X, X, X, DUN_ROOM_MUSEUM_LOBBY}
    },
    [DUN_ROOM_TRAIN_STATION] = {
        "Subway station",
        "You are at the museum subway platform. A sleek automated subway train\n"
        "is waiting on the tracks to the north. The museum lobby is to the south.",
        FALSE, {DUN_ROOM_ENDGAME_HALL, DUN_ROOM_MUSEUM_LOBBY, X, X, X, X, X, X, X, X, DUN_ROOM_ENDGAME_HALL, DUN_ROOM_MUSEUM_LOBBY}
    },
    [DUN_ROOM_ENDGAME_HALL] = {
        "Endgame hallway",
        "You are in the shimmering marble hallway of the Endgame. A grand\n"
        "treasure vault lies to the north, guarded by a voice from above.\n"
        "To answer a question, use 'answer <word>'.",
        FALSE, {DUN_ROOM_ENDGAME_TREASURE, DUN_ROOM_TRAIN_STATION, X, X, X, X, X, X, X, X, DUN_ROOM_ENDGAME_TREASURE, DUN_ROOM_TRAIN_STATION}
    },
    [DUN_ROOM_ENDGAME_TREASURE] = {
        "Endgame treasure room",
        "You are in the Endgame Treasure Vault! In the center is a shimmering\n"
        "pedestal for the Mona Lisa. The hallway is to the south.",
        FALSE, {X, DUN_ROOM_ENDGAME_HALL, X, X, X, X, X, X, X, X, X, DUN_ROOM_ENDGAME_HALL}
    },
};

#undef X

static const DunObjDef OBJECTS[DUN_OBJ_COUNT] = {
    [DUN_OBJ_SHOVEL] = {
        "shovel", "spade", NULL,
        "There is a shovel here.", "A shovel",
        "It is a normal shovel with a price tag attached that says $19.99.",
        "shovel.o", DUN_ROOM_DEAD_END, TRUE, FALSE
    },
    [DUN_OBJ_CPU] = {
        "cpu", "card", "board",
        "There is a CPU card here.", "A computer board",
        "The CPU board has a VAX chip on it. It seems to have 2 Megabytes of\nRAM onboard.",
        "cpu.o", DUN_LOC_NOWHERE, TRUE, FALSE
    },
    [DUN_OBJ_BOULDER] = {
        "boulder", "rock", NULL,
        "There is a large boulder here.", "A boulder",
        "It is just a boulder. It cannot be moved.",
        NULL, DUN_ROOM_EW_DIRT_ROAD, FALSE, FALSE
    },
    [DUN_OBJ_FOOD] = {
        "food", "meat", "snack",
        "There is some food here.", "Some food",
        "It looks like some kind of meat. It smells quite bad.",
        "food.o", DUN_ROOM_NE_SW_ROAD, TRUE, FALSE
    },
    [DUN_OBJ_BEAR] = {
        "bear", "grizzly", NULL,
        "There is a ferocious bear here!", "A bear",
        "It is a large brown bear. It looks very hungry.",
        NULL, DUN_ROOM_SE_NW_ROAD, FALSE, FALSE
    },
    [DUN_OBJ_KEY] = {
        "key", "brass", NULL,
        "There is a shiny brass key here.", "A brass key",
        "It is a shiny brass key with teeth cut for an old building lock.",
        "key.o", DUN_LOC_NOWHERE, TRUE, FALSE
    },
    [DUN_OBJ_BRACELET] = {
        "bracelet", "emerald", NULL,
        "There is an emerald bracelet here.", "An emerald bracelet",
        "It is a beautiful bracelet studded with glowing green emeralds.",
        "bracelet.o", DUN_ROOM_HIDDEN_AREA, TRUE, TRUE
    },
    [DUN_OBJ_LAMP] = {
        "lamp", "lantern", "light",
        "There is a shiny brass lamp nearby.", "A lamp",
        "It is a battery-powered brass lamp.",
        "lamp.o", DUN_ROOM_BUILDING_HALLWAY, TRUE, FALSE
    },
    [DUN_OBJ_STRANGE] = {
        "strange", "object", "junk",
        "There is a strange looking object here.", "A strange object",
        "It looks like an object mangled by an ASCII-mode FTP transfer.",
        NULL, DUN_LOC_NOWHERE, TRUE, FALSE
    },
    [DUN_OBJ_WEIGHT] = {
        "weight", "ten", NULL,
        "There is a 10 pound weight here.", "A 10 pound weight",
        "It is a heavy cast-iron 10 pound weight.",
        "weight.o", DUN_ROOM_WEIGHT_ROOM, TRUE, FALSE
    },
    [DUN_OBJ_PRESERVER] = {
        "preserver", "life", "vest",
        "There is a life preserver here.", "A life preserver",
        "It is an orange flotation ring that says S.S. Minnow.",
        "preserver.o", DUN_ROOM_WEIGHT_ROOM, TRUE, FALSE
    },
    [DUN_OBJ_AMETHYST] = {
        "amethyst", "gem", NULL,
        "There is an amethyst here.", "An amethyst",
        "It is a deep purple amethyst crystal of museum quality.",
        "amethyst.o", DUN_ROOM_IN_LAKE, TRUE, TRUE
    },
    [DUN_OBJ_NITRO] = {
        "nitroglycerin", "nitro", "canister",
        "There is a canister of nitroglycerin here.", "Some nitroglycerin",
        "It is a shock-sensitive canister of liquid nitroglycerin.",
        "nitro.o", DUN_ROOM_NITRO_ROOM, TRUE, FALSE
    },
    [DUN_OBJ_WAX] = {
        "wax", "statuette", "candle",
        "There is a wax statuette here.", "A wax statuette",
        "It is a small statuette molded out of soft paraffin wax. Something\n"
        "hard glints inside it.",
        "wax.o", DUN_ROOM_NITRO_ROOM, TRUE, FALSE
    },
    [DUN_OBJ_DIAMOND] = {
        "diamond", "jewel", NULL,
        "There is a diamond here.", "A diamond",
        "It is a flawless, brilliant-cut diamond.",
        "diamond.o", DUN_LOC_NOWHERE, TRUE, TRUE
    },
    [DUN_OBJ_SILVER] = {
        "silver", "bar", "platinum",
        "There is a silver bar here.", "A silver bar",
        "It is a heavy bullion bar of pure gleaming silver.",
        "silver.o", DUN_ROOM_MAZE_2, TRUE, TRUE
    },
    [DUN_OBJ_FLOPPY] = {
        "floppy", "disk", "diskette",
        "There is a floppy disk here.", "A floppy disk",
        "It is a 5.25-inch PC-DOS boot diskette labeled 'SUBWAY RESET'.",
        "floppy.o", DUN_ROOM_MAZE_2, TRUE, FALSE
    },
    [DUN_OBJ_EGG] = {
        "egg", "jeweled", NULL,
        "There is a jeweled egg here.", "A jeweled egg",
        "It is an ornate Faberge-style egg encrusted with gold filigree.",
        "egg.o", DUN_ROOM_MAZE_3, TRUE, TRUE
    },
    [DUN_OBJ_COINS] = {
        "coins", "gold", "coin",
        "There are some gold coins here.", "Some gold coins",
        "They are rare doubloons minted in solid gold.",
        "coins.o", DUN_ROOM_MUD_ROOM, TRUE, TRUE
    },
    [DUN_OBJ_RUBY] = {
        "ruby", "stone", NULL,
        "There is a glittering ruby here.", "A ruby",
        "It is a crimson pigeon-blood ruby.",
        "ruby.o", DUN_LOC_NOWHERE, TRUE, TRUE
    },
    [DUN_OBJ_MONA_LISA] = {
        "mona", "lisa", "painting",
        "The Mona Lisa is here.", "The Mona Lisa",
        "Leonardo da Vinci's masterpiece, smiling enigmatically at you.",
        "mona.o", DUN_ROOM_ENDGAME_TREASURE, TRUE, TRUE
    },
    [DUN_OBJ_PAPER] = {
        "paper", "slip", "note",
        "There is a slip of paper here.", "A slip of paper",
        "The paper says: 'Remember, the password to rlogin to gamma is worms.\n"
        "Also, the combination to the museum cabinet is 458.'",
        NULL, DUN_LOC_NOWHERE, TRUE, FALSE
    },
};

static const gchar* ENDGAME_QUESTIONS[3] = {
    "A booming voice asks:\n'What is the password on the VAX named pokey?'",
    "The voice speaks again:\n'What is the password to rlogin to gamma?'",
    "The voice asks one final question:\n'How many megabytes of RAM are on the VAX CPU board?'"
};

DunnetState* dunnet_new(void) {
    DunnetState* d = g_new0(DunnetState, 1);
    dunnet_reset(d);
    return d;
}

void dunnet_reset(DunnetState* d) {
    if (d == NULL) return;
    g_free(d->unix_user);
    g_free(d->unix_cwd);
    memset(d, 0, sizeof(*d));
    d->room = DUN_ROOM_DEAD_END;
    d->mode = DUN_MODE_DUNGEON;
    for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
        d->obj_loc[i] = OBJECTS[i].initial_loc;
    }
    d->dungeon_c_exists = TRUE;
    d->ted_c_exists = TRUE;
    d->paper_exists = TRUE;
    d->unix_cwd = g_strdup("/usr/toukmond");
}

void dunnet_free(DunnetState* d) {
    if (d == NULL) return;
    g_free(d->unix_user);
    g_free(d->unix_cwd);
    g_free(d);
}

static gboolean room_is_lit(const DunnetState* d, DunRoomId r) {
    if (!ROOMS[r].dark) return TRUE;
    if (d->lamp_lit && (d->obj_loc[DUN_OBJ_LAMP] == DUN_LOC_INVENTORY ||
                        d->obj_loc[DUN_OBJ_LAMP] == (gint)r)) {
        return TRUE;
    }
    return FALSE;
}

static void append_room_description(DunnetState* d, GString* out, gboolean full) {
    if (!room_is_lit(d, d->room)) {
        g_string_append(out, "It is pitch dark. You are likely to be eaten by a grue.");
        return;
    }
    const DunRoomDef* r = &ROOMS[d->room];
    g_string_append(out, r->title);
    if (full || !d->visited[d->room]) {
        g_string_append_c(out, '\n');
        g_string_append(out, r->desc);
        if (d->room == DUN_ROOM_COMPUTER_ROOM) {
            g_string_append(out, d->vax_running
                ? "\nThe lights on the VAX are flashing."
                : "\nThe lights on the VAX are off.");
        } else if (d->room == DUN_ROOM_CAVE_WALL && d->wall_blasted) {
            g_string_append(out, "\nA blasted opening in the north wall leads into a museum lobby.");
        }
    }
    d->visited[d->room] = TRUE;

    for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
        if (d->obj_loc[i] == (gint)d->room) {
            g_string_append_c(out, '\n');
            g_string_append(out, OBJECTS[i].room_desc);
        }
    }
    if (d->room == DUN_ROOM_ENDGAME_HALL && d->endgame_step < 3) {
        g_string_append_c(out, '\n');
        g_string_append(out, ENDGAME_QUESTIONS[d->endgame_step]);
    }
}

gchar* dunnet_opening_text(DunnetState* d) {
    if (d == NULL) return g_strdup("");
    GString* out = g_string_new(NULL);
    append_room_description(d, out, TRUE);
    return g_string_free(out, FALSE);
}

const gchar* dunnet_prompt(const DunnetState* d) {
    if (d == NULL) return ">";
    switch (d->mode) {
        case DUN_MODE_UNIX_LOGIN:  return "login: ";
        case DUN_MODE_UNIX_PASS:   return "Password: ";
        case DUN_MODE_UNIX_SHELL:  return "$ ";
        case DUN_MODE_FTP_USER:    return "Name (gamma:toukmond): ";
        case DUN_MODE_FTP_PASS:    return "Password: ";
        case DUN_MODE_FTP_CMD:     return "ftp> ";
        case DUN_MODE_RLOGIN_PASS: return "Password: ";
        case DUN_MODE_DOS:         return "A>";
        default:                   return ">";
    }
}

gboolean dunnet_is_password_prompt(const DunnetState* d) {
    if (d == NULL) return FALSE;
    return d->mode == DUN_MODE_UNIX_PASS ||
           d->mode == DUN_MODE_FTP_PASS ||
           d->mode == DUN_MODE_RLOGIN_PASS;
}

gboolean dunnet_is_over(const DunnetState* d) {
    return d != NULL && (d->dead || d->won || d->quit);
}

gint dunnet_score(const DunnetState* d) {
    return d != NULL ? d->score : 0;
}

gint dunnet_max_score(void) {
    return DUN_MAX_SCORE;
}

static gchar* die_with(DunnetState* d, const gchar* msg) {
    d->dead = TRUE;
    d->mode = DUN_MODE_DUNGEON;
    return g_strdup_printf("%s\nYou are dead.\nYou have scored %d out of a possible %d points.",
                           msg, d->score, DUN_MAX_SCORE);
}

static gint find_object_by_word(const gchar* word) {
    if (word == NULL || word[0] == '\0') return -1;
    for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
        if (g_strcmp0(word, OBJECTS[i].id_word) == 0 ||
            (OBJECTS[i].alt_word != NULL && g_strcmp0(word, OBJECTS[i].alt_word) == 0) ||
            (OBJECTS[i].extra_word != NULL && g_strcmp0(word, OBJECTS[i].extra_word) == 0)) {
            return i;
        }
    }
    if (g_strcmp0(word, "chip") == 0) return DUN_OBJ_CPU;
    return -1;
}

static gint find_object_in_text(const gchar* text) {
    if (text == NULL) return -1;
    gchar** tokens = g_strsplit_set(text, " \t,.", -1);
    gint found = -1;
    for (gint i = 0; tokens[i] != NULL && found < 0; i++) {
        found = find_object_by_word(tokens[i]);
    }
    g_strfreev(tokens);
    return found;
}

static gboolean parse_direction(const gchar* cmd, DunDir* out_dir) {
    static const struct { const gchar* name; DunDir dir; } DIRS[] = {
        {"n", DUN_DIR_N}, {"north", DUN_DIR_N},
        {"s", DUN_DIR_S}, {"south", DUN_DIR_S},
        {"e", DUN_DIR_E}, {"east", DUN_DIR_E},
        {"w", DUN_DIR_W}, {"west", DUN_DIR_W},
        {"ne", DUN_DIR_NE}, {"northeast", DUN_DIR_NE},
        {"se", DUN_DIR_SE}, {"southeast", DUN_DIR_SE},
        {"nw", DUN_DIR_NW}, {"northwest", DUN_DIR_NW},
        {"sw", DUN_DIR_SW}, {"southwest", DUN_DIR_SW},
        {"u", DUN_DIR_U}, {"up", DUN_DIR_U},
        {"d", DUN_DIR_D}, {"down", DUN_DIR_D},
        {"in", DUN_DIR_IN}, {"enter", DUN_DIR_IN},
        {"out", DUN_DIR_OUT}, {"exit", DUN_DIR_OUT}, {"leave", DUN_DIR_OUT},
    };
    const gchar* p = cmd;
    if (g_str_has_prefix(p, "go ")) p += 3;
    else if (g_str_has_prefix(p, "walk ")) p += 5;
    while (*p == ' ') p++;
    for (guint i = 0; i < G_N_ELEMENTS(DIRS); i++) {
        if (g_strcmp0(p, DIRS[i].name) == 0) {
            *out_dir = DIRS[i].dir;
            return TRUE;
        }
    }
    return FALSE;
}

static gchar* handle_move(DunnetState* d, DunDir dir) {
    /* Moving inside a pitch-dark room without returning to a lit room kills the player */
    gboolean was_dark = !room_is_lit(d, d->room);

    if (d->room == DUN_ROOM_HIGH_CLIFF && dir == DUN_DIR_D) {
        return die_with(d, "You jump off the cliff and plunge into the canyon below.");
    }
    if (d->room == DUN_ROOM_SE_NW_ROAD && dir == DUN_DIR_SE &&
        d->obj_loc[DUN_OBJ_BEAR] == DUN_ROOM_SE_NW_ROAD) {
        return die_with(d,
            "The bear is very annoyed that you would be so presumptuous as to try\n"
            "and walk right by it. He tells you so by tearing your head off.");
    }
    if (d->room == DUN_ROOM_END_OF_ROAD && (dir == DUN_DIR_NE || dir == DUN_DIR_IN) &&
        d->obj_loc[DUN_OBJ_KEY] != DUN_LOC_INVENTORY) {
        return g_strdup("You don't have a key that can open this door.");
    }
    if (d->room == DUN_ROOM_WEIGHT_ROOM && dir == DUN_DIR_N &&
        d->obj_loc[DUN_OBJ_WEIGHT] != DUN_ROOM_WEIGHT_ROOM) {
        return g_strdup("The north door is closed. A sign by the scale says:\n"
                        "'Place 10 lb weight here to open door.'");
    }
    if (d->room == DUN_ROOM_LAKEFRONT && (dir == DUN_DIR_N || dir == DUN_DIR_IN) &&
        d->obj_loc[DUN_OBJ_PRESERVER] != DUN_LOC_INVENTORY) {
        return die_with(d,
            "You dive in the water, and at first notice it is quite cold. You then\n"
            "start to get used to it as you realize that you never learned how to\n"
            "swim.");
    }
    if (d->room == DUN_ROOM_CAVE_WALL && dir == DUN_DIR_N) {
        if (!d->wall_blasted) {
            return g_strdup("You can't go that way.");
        }
        d->room = DUN_ROOM_MUSEUM_LOBBY;
        GString* out = g_string_new(NULL);
        append_room_description(d, out, FALSE);
        return g_string_free(out, FALSE);
    }
    if (d->room == DUN_ROOM_TRAIN_STATION && (dir == DUN_DIR_N || dir == DUN_DIR_IN)) {
        if (!d->train_powered) {
            return g_strdup("The train doors are locked. A sign says:\n"
                            "'Power offline - reset from Computer Exhibit PC.'");
        }
        d->room = DUN_ROOM_ENDGAME_HALL;
        GString* out = g_string_new(
            "You board the train. The doors close and it whisks you away to the\n"
            "Endgame Hall!\n\n");
        append_room_description(d, out, FALSE);
        return g_string_free(out, FALSE);
    }
    if (d->room == DUN_ROOM_ENDGAME_HALL && (dir == DUN_DIR_N || dir == DUN_DIR_IN)) {
        if (d->endgame_step < 3) {
            return g_strdup("An invisible force field blocks the way! You must answer the voice's\n"
                            "questions first (use 'answer <word>').");
        }
    }

    gint next = ROOMS[d->room].exits[dir];
    if (next < 0) {
        if (was_dark) {
            return die_with(d,
                "You trip over a grue and fall into a pit and break every bone in your\n"
                "body.");
        }
        return g_strdup("You can't go that way.");
    }
    if (was_dark && !room_is_lit(d, (DunRoomId)next)) {
        return die_with(d,
            "You trip over a grue and fall into a pit and break every bone in your\n"
            "body.");
    }

    GString* out = g_string_new(NULL);
    if (d->room == DUN_ROOM_NS_EW_TUNNEL && (dir == DUN_DIR_D || dir == DUN_DIR_IN)) {
        g_string_append(out, "You slide down the chute and land in a pile of mud!\n\n");
    }
    d->room = (DunRoomId)next;
    append_room_description(d, out, FALSE);
    return g_string_free(out, FALSE);
}

static void score_treasure(DunnetState* d, DunObjId obj) {
    if (OBJECTS[obj].treasure && !d->treasure_scored[obj]) {
        d->treasure_scored[obj] = TRUE;
        d->score += 10;
    }
}

/* ------------------------------------------------------------------------- */
/* UNIX Shell, FTP & Rlogin on VAX 'pokey'                                   */
/* ------------------------------------------------------------------------- */

static gchar* handle_unix_login(DunnetState* d, const gchar* line) {
    gchar* trimmed = g_strstrip(g_strdup(line));
    if (trimmed[0] == '\0') {
        g_free(trimmed);
        return g_strdup("");
    }
    g_free(d->unix_user);
    d->unix_user = trimmed;
    d->mode = DUN_MODE_UNIX_PASS;
    return g_strdup("");
}

static gchar* handle_unix_pass(DunnetState* d, const gchar* line) {
    gchar* pass = g_strstrip(g_ascii_strdown(line, -1));
    gboolean ok = (g_strcmp0(d->unix_user, "toukmond") == 0 && g_strcmp0(pass, "robert") == 0);
    g_free(pass);
    if (!ok) {
        d->mode = DUN_MODE_UNIX_LOGIN;
        return g_strdup("\nLogin incorrect");
    }
    d->mode = DUN_MODE_UNIX_SHELL;
    return g_strdup(
        "\nWelcome to Unix\n"
        "Please clean up your directories! The filesystem is getting full.\n"
        "Our tcp/ip link to gamma is a little flaky, but seems to work.\n"
        "The current version of ftp can only send files from the current\n"
        "directory, and deletes them after they are sent! Be careful.\n\n"
        "Note: Restricted bourne shell in use.");
}

static gint find_unix_file_obj(const DunnetState* d, const gchar* filename) {
    for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
        if (d->obj_loc[i] == DUN_LOC_INVENTORY && OBJECTS[i].unix_file != NULL &&
            g_strcmp0(OBJECTS[i].unix_file, filename) == 0) {
            return i;
        }
    }
    return -1;
}

static gchar* handle_unix_shell(DunnetState* d, const gchar* line) {
    gchar* cmd = g_strstrip(g_strdup(line));
    if (cmd[0] == '\0') {
        g_free(cmd);
        return g_strdup("");
    }

    if (g_strcmp0(cmd, "exit") == 0 || g_strcmp0(cmd, "logout") == 0 || g_strcmp0(cmd, "quit") == 0) {
        g_free(cmd);
        d->mode = DUN_MODE_DUNGEON;
        return g_strdup("You step away from the console.");
    }
    if (g_strcmp0(cmd, "pwd") == 0) {
        g_free(cmd);
        return g_strdup(d->unix_cwd);
    }
    if (g_strcmp0(cmd, "whoami") == 0) {
        g_free(cmd);
        return g_strdup("toukmond");
    }
    if (g_strcmp0(cmd, "hostname") == 0 || g_strcmp0(cmd, "uname") == 0 || g_strcmp0(cmd, "uname -n") == 0) {
        g_free(cmd);
        return g_strdup("pokey");
    }
    if (g_strcmp0(cmd, "id") == 0) {
        g_free(cmd);
        return g_strdup("uid=204(toukmond) gid=20(users)");
    }
    if (g_strcmp0(cmd, "ps") == 0) {
        g_free(cmd);
        return g_strdup("  PID TTY      TIME COMMAND\n  102 console  0:01 -rsh\n  109 console  0:00 ps");
    }
    if (g_str_has_prefix(cmd, "echo ")) {
        gchar* out = g_strdup(cmd + 5);
        g_free(cmd);
        return out;
    }
    if (g_strcmp0(cmd, "cd") == 0 || g_str_has_prefix(cmd, "cd ")) {
        const gchar* arg = (cmd[2] == '\0') ? "/usr/toukmond" : g_strstrip(cmd + 3);
        if (g_strcmp0(arg, "/") == 0 || g_strcmp0(arg, "/usr") == 0 ||
            g_strcmp0(arg, "/usr/toukmond") == 0 || g_strcmp0(arg, "/etc") == 0 ||
            g_strcmp0(arg, "/bin") == 0) {
            g_free(d->unix_cwd);
            d->unix_cwd = g_strdup(arg);
            g_free(cmd);
            return g_strdup("");
        } else if (g_strcmp0(arg, "..") == 0) {
            if (g_strcmp0(d->unix_cwd, "/usr/toukmond") == 0) {
                g_free(d->unix_cwd);
                d->unix_cwd = g_strdup("/usr");
            } else {
                g_free(d->unix_cwd);
                d->unix_cwd = g_strdup("/");
            }
            g_free(cmd);
            return g_strdup("");
        }
        gchar* err = g_strdup_printf("%s: bad directory", arg);
        g_free(cmd);
        return err;
    }
    if (g_strcmp0(cmd, "ls") == 0 || g_str_has_prefix(cmd, "ls ")) {
        const gchar* target = d->unix_cwd;
        if (g_str_has_prefix(cmd, "ls ")) {
            const gchar* arg = g_strstrip(cmd + 3);
            if (arg[0] != '-' && arg[0] != '\0') target = arg;
        }
        if (g_strcmp0(target, "/") == 0) {
            g_free(cmd);
            return g_strdup("bin\netc\nrooms\nunix\nusr");
        }
        if (g_strcmp0(target, "/usr") == 0) {
            g_free(cmd);
            return g_strdup("toukmond");
        }
        if (g_strcmp0(target, "/etc") == 0) {
            g_free(cmd);
            return g_strdup("hosts\nmotd\npasswd");
        }
        if (g_strcmp0(target, "/bin") == 0) {
            g_free(cmd);
            return g_strdup("cat\ncompress\necho\nftp\nls\npwd\nrm\nrlogin\nsh\nuncompress");
        }
        GString* list = g_string_new(NULL);
        if (d->dungeon_c_exists) g_string_append(list, "dungeon.c\n");
        if (d->ted_c_exists) g_string_append(list, "ted.c\n");
        if (d->paper_exists) {
            g_string_append(list, d->paper_uncompressed ? "paper.o\n" : "paper.o.Z\n");
        }
        for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
            if (d->obj_loc[i] == DUN_LOC_INVENTORY && OBJECTS[i].unix_file != NULL) {
                g_string_append_printf(list, "%s\n", OBJECTS[i].unix_file);
            }
        }
        if (list->len > 0 && list->str[list->len - 1] == '\n') {
            g_string_truncate(list, list->len - 1);
        }
        g_free(cmd);
        return g_string_free(list, FALSE);
    }
    if (g_str_has_prefix(cmd, "uncompress ") || g_str_has_prefix(cmd, "gunzip ")) {
        const gchar* arg = g_strstrip(strchr(cmd, ' ') + 1);
        if (d->paper_exists && !d->paper_uncompressed &&
            (g_strcmp0(arg, "paper.o.Z") == 0 || g_strcmp0(arg, "paper.o") == 0)) {
            d->paper_uncompressed = TRUE;
            g_free(cmd);
            return g_strdup("");
        }
        gchar* err = g_strdup_printf("%s: no such compressed file", arg);
        g_free(cmd);
        return err;
    }
    if (g_str_has_prefix(cmd, "compress ")) {
        const gchar* arg = g_strstrip(cmd + 9);
        if (d->paper_exists && d->paper_uncompressed && g_strcmp0(arg, "paper.o") == 0) {
            d->paper_uncompressed = FALSE;
            g_free(cmd);
            return g_strdup("");
        }
        gchar* err = g_strdup_printf("%s: cannot compress", arg);
        g_free(cmd);
        return err;
    }
    if (g_str_has_prefix(cmd, "cat ") || g_str_has_prefix(cmd, "more ") || g_str_has_prefix(cmd, "strings ")) {
        const gchar* arg = g_strstrip(strchr(cmd, ' ') + 1);
        if (g_strcmp0(arg, "dungeon.c") == 0 && d->dungeon_c_exists) {
            g_free(cmd);
            return g_strdup("/* dungeon.c - Ron Schnell, 1992 */\n"
                            "#include \"dungeon.h\"\n"
                            "int main() { init_rooms(); play_loop(); return 0; }");
        }
        if (g_strcmp0(arg, "ted.c") == 0 && d->ted_c_exists) {
            g_free(cmd);
            return g_strdup("/* ted.c - text editor stub */\n"
                            "void ted_main(void) { /* under construction */ }");
        }
        if (g_strcmp0(arg, "paper.o.Z") == 0 && d->paper_exists && !d->paper_uncompressed) {
            g_free(cmd);
            return g_strdup("\x1f\x40#$%^&*!~`compressed binary data -- use uncompress");
        }
        if (g_strcmp0(arg, "paper.o") == 0 && d->paper_exists && d->paper_uncompressed) {
            g_free(cmd);
            return g_strdup("Remember, the password to rlogin to gamma is 'worms'.\n"
                            "Also, the combination to the museum cabinet is 458.");
        }
        if (g_strcmp0(arg, "/etc/passwd") == 0 || g_strcmp0(arg, "passwd") == 0) {
            g_free(cmd);
            return g_strdup("root:x:0:1:Super-User:/:/bin/sh\n"
                            "toukmond:x:204:20:Robert Toukmond:/usr/toukmond:/bin/rsh\n"
                            "ftp:x:500:500:Anonymous FTP:/usr/ftp:/bin/false");
        }
        if (g_strcmp0(arg, "/etc/hosts") == 0 || g_strcmp0(arg, "hosts") == 0) {
            g_free(cmd);
            return g_strdup("127.0.0.1       localhost\n"
                            "192.9.200.1     pokey\n"
                            "192.9.200.2     gamma");
        }
        if (find_unix_file_obj(d, arg) >= 0) {
            g_free(cmd);
            return g_strdup("ELF/VAX object file (binary data)");
        }
        gchar* err = g_strdup_printf("cat: cannot open %s", arg);
        g_free(cmd);
        return err;
    }
    if (g_str_has_prefix(cmd, "rm ")) {
        const gchar* arg = g_strstrip(cmd + 3);
        gint obj = find_unix_file_obj(d, arg);
        if (obj >= 0) {
            d->obj_loc[obj] = DUN_LOC_NOWHERE;
            g_free(cmd);
            return g_strdup("");
        }
        if (g_strcmp0(arg, "dungeon.c") == 0 && d->dungeon_c_exists) {
            d->dungeon_c_exists = FALSE;
            g_free(cmd);
            return g_strdup("");
        }
        if (g_strcmp0(arg, "ted.c") == 0 && d->ted_c_exists) {
            d->ted_c_exists = FALSE;
            g_free(cmd);
            return g_strdup("");
        }
        if ((g_strcmp0(arg, "paper.o") == 0 || g_strcmp0(arg, "paper.o.Z") == 0) && d->paper_exists) {
            d->paper_exists = FALSE;
            g_free(cmd);
            return g_strdup("");
        }
        gchar* err = g_strdup_printf("rm: %s non-existent", arg);
        g_free(cmd);
        return err;
    }
    if (g_strcmp0(cmd, "ftp") == 0 || g_str_has_prefix(cmd, "ftp ")) {
        const gchar* host = (cmd[3] == '\0') ? "" : g_strstrip(cmd + 4);
        if (g_strcmp0(host, "gamma") != 0) {
            g_free(cmd);
            return g_strdup("ftp: usage: ftp gamma");
        }
        g_free(cmd);
        d->ftp_binary = FALSE;
        d->mode = DUN_MODE_FTP_USER;
        return g_strdup("Connected to gamma. FTP server ready.");
    }
    if (g_strcmp0(cmd, "rlogin") == 0 || g_str_has_prefix(cmd, "rlogin ") ||
        g_str_has_prefix(cmd, "ssh ") || g_str_has_prefix(cmd, "telnet ")) {
        const gchar* sp = strchr(cmd, ' ');
        const gchar* host = sp != NULL ? g_strstrip((gchar*)(sp + 1)) : "";
        if (g_strcmp0(host, "pokey") == 0) {
            g_free(cmd);
            return g_strdup("You are already on pokey.");
        }
        if (g_strcmp0(host, "gamma") != 0) {
            g_free(cmd);
            return g_strdup("rlogin: unknown host (try: rlogin gamma)");
        }
        g_free(cmd);
        d->mode = DUN_MODE_RLOGIN_PASS;
        return g_strdup("");
    }
    if (g_strcmp0(cmd, "help") == 0) {
        g_free(cmd);
        return g_strdup("Available commands: ls, cat, uncompress, compress, pwd, cd, rm, ftp, rlogin, exit");
    }

    gchar* first = g_strdup(cmd);
    gchar* sp = strchr(first, ' ');
    if (sp != NULL) *sp = '\0';
    gchar* err = g_strdup_printf("rsh: %s: not found", first);
    g_free(first);
    g_free(cmd);
    return err;
}

static gchar* handle_ftp_user(DunnetState* d, const gchar* line) {
    gchar* user = g_strstrip(g_ascii_strdown(line, -1));
    d->ftp_anonymous = (g_strcmp0(user, "anonymous") == 0 || g_strcmp0(user, "ftp") == 0 ||
                        g_strcmp0(user, "toukmond") == 0);
    g_free(user);
    d->mode = DUN_MODE_FTP_PASS;
    return g_strdup(d->ftp_anonymous
        ? "Guest login ok, send ident as password."
        : "Password required.");
}

static gchar* handle_ftp_pass(DunnetState* d, const gchar* line) {
    (void)line;
    if (!d->ftp_anonymous) {
        d->mode = DUN_MODE_UNIX_SHELL;
        return g_strdup("\nLogin failed.");
    }
    d->mode = DUN_MODE_FTP_CMD;
    return g_strdup("\nGuest login ok, access restrictions apply.");
}

static gchar* handle_ftp_cmd(DunnetState* d, const gchar* line) {
    gchar* cmd = g_strstrip(g_ascii_strdown(line, -1));
    if (cmd[0] == '\0') {
        g_free(cmd);
        return g_strdup("");
    }
    if (g_strcmp0(cmd, "quit") == 0 || g_strcmp0(cmd, "bye") == 0 || g_strcmp0(cmd, "exit") == 0) {
        g_free(cmd);
        d->mode = DUN_MODE_UNIX_SHELL;
        return g_strdup("221 Goodbye.");
    }
    if (g_strcmp0(cmd, "binary") == 0 || g_strcmp0(cmd, "type binary") == 0 || g_strcmp0(cmd, "type i") == 0) {
        g_free(cmd);
        d->ftp_binary = TRUE;
        return g_strdup("200 Type set to I.");
    }
    if (g_strcmp0(cmd, "ascii") == 0 || g_strcmp0(cmd, "type ascii") == 0 || g_strcmp0(cmd, "type a") == 0) {
        g_free(cmd);
        d->ftp_binary = FALSE;
        return g_strdup("200 Type set to A.");
    }
    if (g_strcmp0(cmd, "help") == 0 || g_strcmp0(cmd, "?") == 0) {
        g_free(cmd);
        return g_strdup("Commands may be abbreviated. Commands are:\n"
                        "ascii    binary   put      send     quit     help");
    }
    if (g_str_has_prefix(cmd, "send ") || g_str_has_prefix(cmd, "put ")) {
        const gchar* file = g_strstrip(strchr(cmd, ' ') + 1);
        gint obj = find_unix_file_obj(d, file);
        if (obj >= 0) {
            if (d->ftp_binary) {
                d->obj_loc[obj] = DUN_ROOM_RECEIVING_ROOM;
            } else {
                d->obj_loc[obj] = DUN_LOC_NOWHERE;
                d->obj_loc[DUN_OBJ_STRANGE] = DUN_ROOM_RECEIVING_ROOM;
            }
            gchar* ok = g_strdup_printf(
                "200 PORT command successful.\n"
                "150 Opening %s mode data connection for %s.\n"
                "226 Transfer complete.",
                d->ftp_binary ? "BINARY" : "ASCII", file);
            g_free(cmd);
            return ok;
        }
        if (g_strcmp0(file, "paper.o") == 0 && d->paper_exists && d->paper_uncompressed) {
            d->paper_exists = FALSE;
            d->obj_loc[DUN_OBJ_PAPER] = DUN_ROOM_RECEIVING_ROOM;
            g_free(cmd);
            return g_strdup("200 PORT command successful.\n"
                            "150 Opening data connection for paper.o.\n"
                            "226 Transfer complete.");
        }
        gchar* err = g_strdup_printf("%s: No such file or directory.", file);
        g_free(cmd);
        return err;
    }
    g_free(cmd);
    return g_strdup("?Invalid command (try: binary, send <file>, quit)");
}

static gchar* handle_rlogin_pass(DunnetState* d, const gchar* line) {
    gchar* pass = g_strstrip(g_ascii_strdown(line, -1));
    gboolean ok = (g_strcmp0(pass, "worms") == 0);
    g_free(pass);
    if (!ok) {
        d->mode = DUN_MODE_UNIX_SHELL;
        return g_strdup("\nLogin incorrect");
    }
    d->mode = DUN_MODE_DUNGEON;
    /* Items not sent via FTP do not travel through rlogin */
    for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
        if (d->obj_loc[i] == DUN_LOC_INVENTORY) {
            d->obj_loc[i] = DUN_ROOM_COMPUTER_ROOM;
        }
    }
    d->room = DUN_ROOM_RECEIVING_ROOM;
    GString* out = g_string_new(
        "\nYou feel yourself being disassembled into bits and sent across the\n"
        "ethernet cable to gamma!\n\n");
    append_room_description(d, out, TRUE);
    return g_string_free(out, FALSE);
}

/* ------------------------------------------------------------------------- */
/* IBM PC XT DOS Prompt in Computer Exhibit                                  */
/* ------------------------------------------------------------------------- */

static gchar* handle_dos(DunnetState* d, const gchar* line) {
    gchar* cmd = g_strstrip(g_ascii_strdown(line, -1));
    if (cmd[0] == '\0') {
        g_free(cmd);
        return g_strdup("");
    }
    if (g_strcmp0(cmd, "exit") == 0 || g_strcmp0(cmd, "quit") == 0) {
        g_free(cmd);
        d->mode = DUN_MODE_DUNGEON;
        return g_strdup("You step away from the PC.");
    }
    if (g_strcmp0(cmd, "dir") == 0 || g_strcmp0(cmd, "ls") == 0) {
        g_free(cmd);
        return g_strdup(" Volume in drive A is SUBWAY_BOOT\n"
                        " Directory of A:\\\n\n"
                        "COMMAND  COM     25307   3-07-85  12:00p\n"
                        "RESET    EXE     14848   3-07-85  12:00p\n"
                        "NOTES    TXT       128   3-07-85  12:00p\n"
                        "        3 File(s)     319488 bytes free");
    }
    if (g_str_has_prefix(cmd, "type ") || g_str_has_prefix(cmd, "cat ")) {
        const gchar* arg = g_strstrip(strchr(cmd, ' ') + 1);
        if (g_strcmp0(arg, "notes.txt") == 0 || g_strcmp0(arg, "notes") == 0) {
            g_free(cmd);
            return g_strdup("Run RESET.EXE to energize the museum subway train to the Endgame.");
        }
        g_free(cmd);
        return g_strdup("File not found");
    }
    if (g_strcmp0(cmd, "reset") == 0 || g_strcmp0(cmd, "reset.exe") == 0) {
        g_free(cmd);
        d->train_powered = TRUE;
        return g_strdup("Subway power grid online.\n"
                        "Train at North Station is now ready for boarding.");
    }
    if (g_strcmp0(cmd, "help") == 0) {
        g_free(cmd);
        return g_strdup("DOS commands: DIR, TYPE NOTES.TXT, RESET, EXIT");
    }
    g_free(cmd);
    return g_strdup("Bad command or file name");
}

/* ------------------------------------------------------------------------- */
/* Dungeon Mode Parser                                                       */
/* ------------------------------------------------------------------------- */

static gchar* handle_examine(DunnetState* d, const gchar* target) {
    if (target == NULL || target[0] == '\0') {
        GString* out = g_string_new(NULL);
        append_room_description(d, out, TRUE);
        return g_string_free(out, FALSE);
    }
    if (!room_is_lit(d, d->room)) {
        return g_strdup("It is too dark to see anything.");
    }
    if (strstr(target, "bin") != NULL || strstr(target, "mail") != NULL) {
        if (d->room == DUN_ROOM_MAILROOM) {
            return g_strdup(
                "There are bins labeled with the names of all the users here. The only\n"
                "bin with anything in it is labeled 'toukmond', and it has a slip of\n"
                "paper in it. The slip of paper says: \"Don't forget to type your first\n"
                "name as your password, Robert.\"");
        }
        if (d->room == DUN_ROOM_RESTROOM) {
            return g_strdup("It is a trophy collection bin beside the urinal.");
        }
    }
    if (strstr(target, "vax") != NULL || strstr(target, "pokey") != NULL ||
        (strstr(target, "computer") != NULL && d->room == DUN_ROOM_COMPUTER_ROOM)) {
        if (d->room == DUN_ROOM_COMPUTER_ROOM) {
            return g_strdup(d->vax_running
                ? "The VAX 11/780 is running smoothly. Its console waits for you to 'type'."
                : "The VAX 11/780 has an open cabinet with an empty slot for a CPU board.");
        }
    }
    if (strstr(target, "pc") != NULL || strstr(target, "ibm") != NULL || strstr(target, "drive") != NULL) {
        if (d->room == DUN_ROOM_COMPUTER_MUSEUM) {
            return g_strdup(d->floppy_in_pc
                ? "The IBM PC XT has a boot floppy in drive A:. Use 'type' to use it."
                : "The IBM PC XT has an empty 5.25-inch floppy drive.");
        }
    }
    if (strstr(target, "cabinet") != NULL || strstr(target, "safe") != NULL || strstr(target, "locker") != NULL) {
        if (d->room == DUN_ROOM_COMPUTER_MUSEUM) {
            return g_strdup(d->cabinet_open
                ? "The steel cabinet is open."
                : "The steel cabinet is locked with a 3-digit combination dial (try 'dial <number>').");
        }
    }
    if (strstr(target, "dial") != NULL && d->room == DUN_ROOM_SAUNA) {
        static const gchar* const LEVELS[] = {
            "The dial is set to 0 (normal room temperature).",
            "The dial is set to 1 (luke warm).",
            "The dial is set to 2 (comfortably hot).",
            "The dial is set to 3 (refreshingly hot)."
        };
        return g_strdup(LEVELS[CLAMP(d->sauna_level, 0, 3)]);
    }
    if ((strstr(target, "tree") != NULL || strstr(target, "palm") != NULL || strstr(target, "coconut") != NULL) &&
        (d->room == DUN_ROOM_DEAD_END || d->room == DUN_ROOM_EW_DIRT_ROAD)) {
        return g_strdup("They are tall royal palm trees with heavy coconuts high in the branches.");
    }
    if (strstr(target, "chute") != NULL && d->room == DUN_ROOM_NS_EW_TUNNEL) {
        return g_strdup("It is a steep metal chute leading down into darkness. You can put items\n"
                        "in the chute or slide down it.");
    }
    if (strstr(target, "urinal") != NULL && d->room == DUN_ROOM_RESTROOM) {
        return g_strdup("It is a porcelain urinal with a wide drain. Putting treasures in it and\n"
                        "typing 'flush' sends them to the score vault!");
    }

    gint obj = find_object_in_text(target);
    if (obj >= 0 && (d->obj_loc[obj] == DUN_LOC_INVENTORY || d->obj_loc[obj] == (gint)d->room)) {
        return g_strdup(OBJECTS[obj].examine);
    }
    return g_strdup("I see nothing special about that.");
}

static gchar* handle_take(DunnetState* d, const gchar* target) {
    if (!room_is_lit(d, d->room)) {
        return g_strdup("It is too dark to see anything.");
    }
    if (g_strcmp0(target, "all") == 0 || g_strcmp0(target, "everything") == 0) {
        GString* res = g_string_new(NULL);
        gboolean any = FALSE;
        for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
            if (d->obj_loc[i] == (gint)d->room && OBJECTS[i].takeable) {
                d->obj_loc[i] = DUN_LOC_INVENTORY;
                if (any) g_string_append_c(res, '\n');
                g_string_append_printf(res, "%s: Taken.", OBJECTS[i].inv_desc);
                any = TRUE;
            }
        }
        if (!any) {
            g_string_free(res, TRUE);
            return g_strdup("There is nothing here to take.");
        }
        return g_string_free(res, FALSE);
    }
    gint obj = find_object_in_text(target);
    if (obj < 0) {
        return g_strdup("I don't see that here.");
    }
    if (d->obj_loc[obj] == DUN_LOC_INVENTORY) {
        return g_strdup("You already have it.");
    }
    if (d->obj_loc[obj] != (gint)d->room) {
        return g_strdup("I don't see that here.");
    }
    if (!OBJECTS[obj].takeable) {
        if (obj == DUN_OBJ_BEAR) {
            return die_with(d, "The bear is not in the mood to be picked up. He mauls you.");
        }
        return g_strdup("You cannot take that.");
    }
    d->obj_loc[obj] = DUN_LOC_INVENTORY;
    return g_strdup("Taken.");
}

static gchar* feed_bear(DunnetState* d) {
    if (d->room != DUN_ROOM_SE_NW_ROAD || d->obj_loc[DUN_OBJ_BEAR] != DUN_ROOM_SE_NW_ROAD) {
        return g_strdup("There is no bear here.");
    }
    if (d->obj_loc[DUN_OBJ_FOOD] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_FOOD] != DUN_ROOM_SE_NW_ROAD) {
        return g_strdup("You have nothing to feed the bear with.");
    }
    d->obj_loc[DUN_OBJ_FOOD] = DUN_LOC_NOWHERE;
    d->obj_loc[DUN_OBJ_BEAR] = DUN_LOC_NOWHERE;
    d->obj_loc[DUN_OBJ_KEY] = DUN_ROOM_SE_NW_ROAD;
    return g_strdup("The bear takes the food and runs away with it. He left something behind.");
}

static gchar* handle_put(DunnetState* d, const gchar* args) {
    gint obj = find_object_in_text(args);
    if (obj == DUN_OBJ_CPU && (strstr(args, "vax") != NULL || strstr(args, "pokey") != NULL ||
                               strstr(args, "computer") != NULL || strstr(args, "cabinet") != NULL ||
                               strstr(args, "slot") != NULL)) {
        if (d->room != DUN_ROOM_COMPUTER_ROOM) {
            return g_strdup("There is no VAX computer here.");
        }
        if (d->obj_loc[DUN_OBJ_CPU] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_CPU] != DUN_ROOM_COMPUTER_ROOM) {
            return g_strdup("You don't have the CPU board.");
        }
        d->obj_loc[DUN_OBJ_CPU] = DUN_LOC_NOWHERE;
        if (!d->vax_running) {
            d->vax_running = TRUE;
            d->score += 10;
        }
        return g_strdup("As you put the CPU board in the computer, it immediately springs to life.\n"
                        "The lights start flashing, and the fans seem to startup.");
    }
    if (obj == DUN_OBJ_FLOPPY && (strstr(args, "pc") != NULL || strstr(args, "drive") != NULL ||
                                  strstr(args, "computer") != NULL || strstr(args, "ibm") != NULL)) {
        if (d->room != DUN_ROOM_COMPUTER_MUSEUM) {
            return g_strdup("There is no PC floppy drive here.");
        }
        if (d->obj_loc[DUN_OBJ_FLOPPY] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_FLOPPY] != DUN_ROOM_COMPUTER_MUSEUM) {
            return g_strdup("You don't have the floppy disk.");
        }
        d->obj_loc[DUN_OBJ_FLOPPY] = DUN_LOC_NOWHERE;
        d->floppy_in_pc = TRUE;
        return g_strdup("You insert the floppy disk into drive A: of the IBM PC.");
    }
    if (obj == DUN_OBJ_FOOD && strstr(args, "bear") != NULL) {
        return feed_bear(d);
    }
    if (obj >= 0 && (d->obj_loc[obj] == DUN_LOC_INVENTORY || d->obj_loc[obj] == (gint)d->room)) {
        if (d->room == DUN_ROOM_NS_EW_TUNNEL && strstr(args, "chute") != NULL) {
            d->obj_loc[obj] = DUN_ROOM_MUD_ROOM;
            return g_strdup("You hear it slide down the chute and off into the distance.");
        }
        if (d->room == DUN_ROOM_RESTROOM &&
            (strstr(args, "urinal") != NULL || strstr(args, "pipe") != NULL ||
             strstr(args, "drain") != NULL || strstr(args, "bin") != NULL ||
             strstr(args, "case") != NULL)) {
            if (strstr(args, "bin") != NULL || strstr(args, "case") != NULL) {
                d->obj_loc[obj] = DUN_LOC_NOWHERE;
                if (OBJECTS[obj].treasure) {
                    score_treasure(d, (DunObjId)obj);
                    return g_strdup_printf("You place %s in the trophy bin. Your score is now %d!",
                                           OBJECTS[obj].inv_desc, d->score);
                }
                return g_strdup("You place it in the bin.");
            }
            d->obj_loc[obj] = DUN_LOC_URINAL;
            return g_strdup("You put it into the urinal.");
        }
        if (d->room == DUN_ROOM_ENDGAME_TREASURE && obj == DUN_OBJ_MONA_LISA &&
            (strstr(args, "pedestal") != NULL || strstr(args, "vault") != NULL)) {
            d->obj_loc[DUN_OBJ_MONA_LISA] = DUN_LOC_NOWHERE;
            score_treasure(d, DUN_OBJ_MONA_LISA);
            d->won = TRUE;
            return g_strdup_printf(
                "You place the Mona Lisa onto the shimmering pedestal!\n"
                "Congratulations! You have solved Dunnet and won the game!\n"
                "You have scored %d out of a possible %d points in %d moves.",
                d->score, DUN_MAX_SCORE, d->moves);
        }
        if (d->room == DUN_ROOM_WEIGHT_ROOM && obj == DUN_OBJ_WEIGHT && strstr(args, "scale") != NULL) {
            d->obj_loc[DUN_OBJ_WEIGHT] = DUN_ROOM_WEIGHT_ROOM;
            return g_strdup("You place the 10 pound weight on the scale. The north door grinds open.");
        }
    }
    return g_strdup("I don't know how to put that there.");
}

static gchar* handle_drop(DunnetState* d, const gchar* target) {
    gint obj = find_object_in_text(target);
    if (obj < 0 || d->obj_loc[obj] != DUN_LOC_INVENTORY) {
        return g_strdup("You aren't carrying that.");
    }
    if (obj == DUN_OBJ_FOOD && d->room == DUN_ROOM_SE_NW_ROAD &&
        d->obj_loc[DUN_OBJ_BEAR] == DUN_ROOM_SE_NW_ROAD) {
        return feed_bear(d);
    }
    if (obj == DUN_OBJ_NITRO && d->room == DUN_ROOM_CAVE_WALL) {
        d->obj_loc[DUN_OBJ_NITRO] = DUN_LOC_NOWHERE;
        d->wall_blasted = TRUE;
        return g_strdup("BOOOOM! The nitroglycerin explodes, blasting a passage through the north wall!");
    }
    if (obj == DUN_OBJ_MONA_LISA && d->room == DUN_ROOM_ENDGAME_TREASURE) {
        return handle_put(d, "mona in pedestal");
    }
    d->obj_loc[obj] = (gint)d->room;
    return g_strdup("Done.");
}

gchar* dunnet_command(DunnetState* d, const gchar* line) {
    if (d == NULL) return g_strdup("");
    if (line == NULL) line = "";

    /* Dispatch to sub-mode if active */
    switch (d->mode) {
        case DUN_MODE_UNIX_LOGIN:  return handle_unix_login(d, line);
        case DUN_MODE_UNIX_PASS:   return handle_unix_pass(d, line);
        case DUN_MODE_UNIX_SHELL:  return handle_unix_shell(d, line);
        case DUN_MODE_FTP_USER:    return handle_ftp_user(d, line);
        case DUN_MODE_FTP_PASS:    return handle_ftp_pass(d, line);
        case DUN_MODE_FTP_CMD:     return handle_ftp_cmd(d, line);
        case DUN_MODE_RLOGIN_PASS: return handle_rlogin_pass(d, line);
        case DUN_MODE_DOS:         return handle_dos(d, line);
        case DUN_MODE_DUNGEON:
            break;
    }

    gchar* lower = g_strstrip(g_ascii_strdown(line, -1));
    if (lower[0] == '\0') {
        g_free(lower);
        return g_strdup("");
    }

    if (g_strcmp0(lower, "restart") == 0) {
        g_free(lower);
        dunnet_reset(d);
        return dunnet_opening_text(d);
    }
    if (g_strcmp0(lower, "score") == 0) {
        g_free(lower);
        return g_strdup_printf("You have scored %d out of a possible %d points in %d moves.",
                               d->score, DUN_MAX_SCORE, d->moves);
    }
    if (g_strcmp0(lower, "quit") == 0 || g_strcmp0(lower, "q") == 0) {
        g_free(lower);
        d->quit = TRUE;
        return g_strdup_printf("You have scored %d out of a possible %d points.\n"
                               "Type 'restart' to play again.",
                               d->score, DUN_MAX_SCORE);
    }

    if (d->dead || d->won || d->quit) {
        g_free(lower);
        return g_strdup("The game is over. Type 'restart' to start a new game.");
    }

    d->moves++;

    DunDir dir;
    if (parse_direction(lower, &dir)) {
        g_free(lower);
        return handle_move(d, dir);
    }

    if (g_strcmp0(lower, "look") == 0 || g_strcmp0(lower, "l") == 0) {
        g_free(lower);
        GString* out = g_string_new(NULL);
        append_room_description(d, out, TRUE);
        return g_string_free(out, FALSE);
    }
    if (g_str_has_prefix(lower, "look at ") || g_str_has_prefix(lower, "look in ")) {
        gchar* res = handle_examine(d, g_strstrip(lower + 8));
        g_free(lower);
        return res;
    }
    if (g_str_has_prefix(lower, "look ") || g_str_has_prefix(lower, "examine ") ||
        g_str_has_prefix(lower, "read ") || g_str_has_prefix(lower, "x ")) {
        const gchar* sp = strchr(lower, ' ');
        gchar* res = handle_examine(d, g_strstrip((gchar*)(sp + 1)));
        g_free(lower);
        return res;
    }

    if (g_strcmp0(lower, "i") == 0 || g_strcmp0(lower, "inv") == 0 || g_strcmp0(lower, "inventory") == 0) {
        g_free(lower);
        GString* inv = g_string_new("You currently have:");
        gboolean any = FALSE;
        for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
            if (d->obj_loc[i] == DUN_LOC_INVENTORY) {
                g_string_append_printf(inv, "\n%s", OBJECTS[i].inv_desc);
                any = TRUE;
            }
        }
        if (!any) g_string_append(inv, "\nNothing.");
        return g_string_free(inv, FALSE);
    }

    if (g_str_has_prefix(lower, "get ") || g_str_has_prefix(lower, "take ") ||
        g_str_has_prefix(lower, "pick up ")) {
        const gchar* target = g_str_has_prefix(lower, "pick up ")
            ? (lower + 8)
            : (strchr(lower, ' ') + 1);
        gchar* res = handle_take(d, g_strstrip((gchar*)target));
        g_free(lower);
        return res;
    }

    if (g_str_has_prefix(lower, "put ") || g_str_has_prefix(lower, "insert ") ||
        g_str_has_prefix(lower, "place ")) {
        const gchar* sp = strchr(lower, ' ');
        gchar* res = handle_put(d, g_strstrip((gchar*)(sp + 1)));
        g_free(lower);
        return res;
    }

    if (g_str_has_prefix(lower, "drop ") || g_str_has_prefix(lower, "throw ")) {
        const gchar* sp = strchr(lower, ' ');
        gchar* res = handle_drop(d, g_strstrip((gchar*)(sp + 1)));
        g_free(lower);
        return res;
    }

    if (g_strcmp0(lower, "dig") == 0 || g_str_has_prefix(lower, "dig ")) {
        g_free(lower);
        if (d->obj_loc[DUN_OBJ_SHOVEL] != DUN_LOC_INVENTORY) {
            return g_strdup("You have nothing with which to dig.");
        }
        if (d->room == DUN_ROOM_DEAD_END && !d->dug_cpu) {
            d->dug_cpu = TRUE;
            d->obj_loc[DUN_OBJ_CPU] = DUN_ROOM_DEAD_END;
            return g_strdup("You think you found something.");
        }
        return g_strdup("Digging here reveals nothing.");
    }

    if (g_str_has_prefix(lower, "climb")) {
        g_free(lower);
        if (d->room == DUN_ROOM_DEAD_END || d->room == DUN_ROOM_EW_DIRT_ROAD) {
            return g_strdup("You manage to get about two feet up the tree and fall back down. You\n"
                            "notice that the tree is very unsteady.");
        }
        return g_strdup("There is nothing here to climb.");
    }

    if (g_str_has_prefix(lower, "shake") || g_str_has_prefix(lower, "wave")) {
        if (strstr(lower, "tree") != NULL || strstr(lower, "palm") != NULL ||
            (g_strcmp0(lower, "shake") == 0 && (d->room == DUN_ROOM_DEAD_END || d->room == DUN_ROOM_EW_DIRT_ROAD))) {
            g_free(lower);
            return die_with(d,
                "You begin to shake a tree, and notice a coconut begin to fall from the air.\n"
                "As you try to get your hand up to block it, you feel the impact as it lands\n"
                "on your head.");
        }
        g_free(lower);
        return g_strdup("Shaking that accomplishes nothing.");
    }

    if (g_str_has_prefix(lower, "feed ") || g_str_has_prefix(lower, "give ")) {
        g_free(lower);
        return feed_bear(d);
    }

    if (g_str_has_prefix(lower, "eat ")) {
        gint obj = find_object_in_text(lower + 4);
        g_free(lower);
        if (obj < 0 || d->obj_loc[obj] != DUN_LOC_INVENTORY) {
            return g_strdup("You don't have that.");
        }
        if (obj == DUN_OBJ_FOOD) {
            d->obj_loc[DUN_OBJ_FOOD] = DUN_LOC_NOWHERE;
            return g_strdup("That tasted horrible.");
        }
        return die_with(d, "You forcefully shove it down your throat, and start choking.");
    }

    if (g_strcmp0(lower, "turn on lamp") == 0 || g_strcmp0(lower, "turn on lantern") == 0 ||
        g_strcmp0(lower, "light lamp") == 0 || g_strcmp0(lower, "light lantern") == 0 ||
        g_strcmp0(lower, "turn lamp on") == 0) {
        g_free(lower);
        if (d->obj_loc[DUN_OBJ_LAMP] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_LAMP] != (gint)d->room) {
            return g_strdup("You don't have a lamp.");
        }
        d->lamp_lit = TRUE;
        return g_strdup("The lamp is now on.");
    }

    if (g_strcmp0(lower, "turn off lamp") == 0 || g_strcmp0(lower, "turn off lantern") == 0 ||
        g_strcmp0(lower, "extinguish lamp") == 0 || g_strcmp0(lower, "turn lamp off") == 0) {
        g_free(lower);
        if (d->obj_loc[DUN_OBJ_LAMP] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_LAMP] != (gint)d->room) {
            return g_strdup("You don't have a lamp.");
        }
        d->lamp_lit = FALSE;
        return g_strdup("The lamp is now off.");
    }

    if (g_strcmp0(lower, "type") == 0 || g_strcmp0(lower, "use computer") == 0 ||
        g_strcmp0(lower, "use vax") == 0 || g_strcmp0(lower, "use pc") == 0) {
        g_free(lower);
        if (d->room == DUN_ROOM_COMPUTER_ROOM) {
            if (!d->vax_running) {
                return g_strdup("You type on the keyboard, but your characters do not even echo.");
            }
            d->mode = DUN_MODE_UNIX_LOGIN;
            return g_strdup("\nUNIX System V Release 2.0 (pokey)\n");
        }
        if (d->room == DUN_ROOM_COMPUTER_MUSEUM) {
            if (!d->floppy_in_pc) {
                return g_strdup("Non-System disk or disk error.\nInsert system floppy disk into drive A: first.");
            }
            d->mode = DUN_MODE_DOS;
            return g_strdup("IBM Personal Computer DOS Version 2.10");
        }
        return g_strdup("There is nothing here on which you can type.");
    }

    if (g_strcmp0(lower, "swim") == 0) {
        g_free(lower);
        if (d->room == DUN_ROOM_LAKEFRONT) {
            return handle_move(d, DUN_DIR_N);
        }
        if (d->room == DUN_ROOM_IN_LAKE) {
            return handle_move(d, DUN_DIR_N);
        }
        return g_strdup("There is no water here to swim in.");
    }

    if (g_str_has_prefix(lower, "turn dial") || g_str_has_prefix(lower, "set dial")) {
        g_free(lower);
        if (d->room != DUN_ROOM_SAUNA) {
            return g_strdup("There is no dial here.");
        }
        d->sauna_level++;
        if (d->sauna_level == 1) return g_strdup("The temperature is now luke warm.");
        if (d->sauna_level == 2) return g_strdup("It is now comfortably hot in here.");
        if (d->sauna_level == 3) {
            if (d->obj_loc[DUN_OBJ_NITRO] == DUN_LOC_INVENTORY || d->obj_loc[DUN_OBJ_NITRO] == DUN_ROOM_SAUNA) {
                return die_with(d, "The intense heat detonates the nitroglycerin! BOOOOM!");
            }
            if (d->obj_loc[DUN_OBJ_WAX] == DUN_LOC_INVENTORY || d->obj_loc[DUN_OBJ_WAX] == DUN_ROOM_SAUNA) {
                gint loc = d->obj_loc[DUN_OBJ_WAX];
                d->obj_loc[DUN_OBJ_WAX] = DUN_LOC_NOWHERE;
                d->obj_loc[DUN_OBJ_DIAMOND] = loc;
                return g_strdup("It is refreshingly hot in here.\n"
                                "The wax statuette melts away, leaving a sparkling diamond!");
            }
            return g_strdup("It is refreshingly hot in here.");
        }
        return die_with(d, "It is now way too hot in here. You drop dead from heat exhaustion.");
    }

    if (g_str_has_prefix(lower, "blast") || g_str_has_prefix(lower, "ignite") ||
        g_str_has_prefix(lower, "detonate") || g_strcmp0(lower, "use nitro") == 0 ||
        g_strcmp0(lower, "use nitroglycerin") == 0) {
        g_free(lower);
        if (d->obj_loc[DUN_OBJ_NITRO] != DUN_LOC_INVENTORY && d->obj_loc[DUN_OBJ_NITRO] != (gint)d->room) {
            return g_strdup("You have no explosives.");
        }
        if (d->room == DUN_ROOM_CAVE_WALL) {
            d->obj_loc[DUN_OBJ_NITRO] = DUN_LOC_NOWHERE;
            d->wall_blasted = TRUE;
            return g_strdup("BOOOOM! The nitroglycerin explodes, blasting a passage through the north wall!");
        }
        return die_with(d, "BOOOOM! You blow yourself to smithereens!");
    }

    if (g_str_has_prefix(lower, "dial ") || g_str_has_prefix(lower, "unlock ") ||
        g_str_has_prefix(lower, "open cabinet") || g_strcmp0(lower, "458") == 0) {
        gboolean has_code = (strstr(lower, "458") != NULL);
        g_free(lower);
        if (d->room != DUN_ROOM_COMPUTER_MUSEUM) {
            return g_strdup("There is nothing to unlock here.");
        }
        if (d->cabinet_open) {
            return g_strdup("The cabinet is already open.");
        }
        if (!has_code) {
            return g_strdup("The cabinet is locked with a 3-digit combination (use 'dial <code>').");
        }
        d->cabinet_open = TRUE;
        d->obj_loc[DUN_OBJ_RUBY] = DUN_ROOM_COMPUTER_MUSEUM;
        return g_strdup("Click! The steel cabinet swings open, revealing a glittering ruby!");
    }

    if (g_strcmp0(lower, "flush") == 0 || g_strcmp0(lower, "flush urinal") == 0) {
        g_free(lower);
        if (d->room != DUN_ROOM_RESTROOM) {
            return g_strdup("There is nothing to flush here.");
        }
        gboolean scored_any = FALSE;
        for (gint i = 0; i < DUN_OBJ_COUNT; i++) {
            if (d->obj_loc[i] == DUN_LOC_URINAL) {
                d->obj_loc[i] = DUN_LOC_NOWHERE;
                if (OBJECTS[i].treasure) {
                    score_treasure(d, (DunObjId)i);
                    scored_any = TRUE;
                }
            }
        }
        if (scored_any) {
            return g_strdup_printf("Whoooosh! You hear treasures swoosh down the drain!\n"
                                   "Your score is now %d out of %d!",
                                   d->score, DUN_MAX_SCORE);
        }
        return g_strdup("Whoooosh!");
    }

    if (g_str_has_prefix(lower, "answer ")) {
        const gchar* ans = g_strstrip(lower + 7);
        if (d->room != DUN_ROOM_ENDGAME_HALL) {
            g_free(lower);
            return g_strdup("No one is asking you a question here.");
        }
        if (d->endgame_step >= 3) {
            g_free(lower);
            return g_strdup("You have already answered all the questions. The vault to the north is open.");
        }
        gboolean correct = FALSE;
        if (d->endgame_step == 0 && g_strcmp0(ans, "robert") == 0) correct = TRUE;
        else if (d->endgame_step == 1 && g_strcmp0(ans, "worms") == 0) correct = TRUE;
        else if (d->endgame_step == 2 && (g_strcmp0(ans, "2") == 0 || g_strcmp0(ans, "two") == 0)) correct = TRUE;
        g_free(lower);
        if (!correct) {
            return g_strdup("The voice booms: 'Incorrect! Think back on your journey.'");
        }
        d->endgame_step++;
        if (d->endgame_step < 3) {
            return g_strdup_printf("Correct!\n%s", ENDGAME_QUESTIONS[d->endgame_step]);
        }
        d->score += 10;
        return g_strdup("Correct! The force field vanishes, opening the way north into the\n"
                        "Endgame Treasure Vault!");
    }

    if (g_strcmp0(lower, "help") == 0 || g_strcmp0(lower, "?") == 0) {
        g_free(lower);
        return g_strdup(
            "Welcome to Dunnet (Emacs Dungeon Adventure)!\n"
            "Useful commands:\n"
            "  Directions : n, s, e, w, ne, se, nw, sw, u, d, in, out\n"
            "  Inspection : look (l), examine <obj>, inventory (i), score\n"
            "  Items      : get <obj>, get all, drop <obj>, put <obj> in <target>\n"
            "  Actions    : dig, feed bear, turn on lamp, type, swim, turn dial,\n"
            "               dial <code>, flush, answer <word>, restart, quit");
    }

    g_free(lower);
    return g_strdup("I don't understand that.");
}

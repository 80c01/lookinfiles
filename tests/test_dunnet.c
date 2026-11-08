#include <glib.h>
#include <string.h>
#include "games/dunnet.h"

static void assert_cmd_contains(DunnetState* d, const gchar* cmd, const gchar* expected) {
    gchar* out = dunnet_command(d, cmd);
    if (strstr(out, expected) == NULL) {
        g_test_message("Command '%s' output:\n%s\ndid not contain:\n%s", cmd, out, expected);
    }
    g_assert_nonnull(strstr(out, expected));
    g_free(out);
}

static void test_opening_and_dig_cpu(void) {
    DunnetState* d = dunnet_new();
    gchar* open = dunnet_opening_text(d);
    g_assert_nonnull(strstr(open, "Dead end"));
    g_assert_nonnull(strstr(open, "There is a shovel here."));
    g_assert_cmpstr(dunnet_prompt(d), ==, ">");
    g_free(open);

    assert_cmd_contains(d, "dig", "nothing with which to dig");
    assert_cmd_contains(d, "get shovel", "Taken.");
    assert_cmd_contains(d, "dig", "You think you found something.");
    assert_cmd_contains(d, "look", "There is a CPU card here.");
    assert_cmd_contains(d, "get cpu", "Taken.");
    assert_cmd_contains(d, "examine cpu", "2 Megabytes of");
    assert_cmd_contains(d, "i", "A computer board");

    dunnet_free(d);
}

static void test_hazards_and_restart(void) {
    DunnetState* d = dunnet_new();

    /* Shaking a palm tree drops a fatal coconut */
    assert_cmd_contains(d, "climb tree", "two feet up the tree");
    assert_cmd_contains(d, "shake tree", "coconut");
    g_assert_true(dunnet_is_over(d));

    /* Restart brings the player back to Dead end */
    assert_cmd_contains(d, "restart", "Dead end");
    g_assert_false(dunnet_is_over(d));

    /* Walking past the bear without feeding it is fatal */
    assert_cmd_contains(d, "e", "E/W Dirt road");
    assert_cmd_contains(d, "e", "Fork");
    assert_cmd_contains(d, "se", "ferocious bear");
    assert_cmd_contains(d, "se", "tearing your head off");
    g_assert_true(dunnet_is_over(d));

    dunnet_free(d);
}

static void test_full_walkthrough_to_victory(void) {
    DunnetState* d = dunnet_new();

    /* 1. Forest & Old Building */
    assert_cmd_contains(d, "get shovel", "Taken.");
    assert_cmd_contains(d, "dig", "found something");
    assert_cmd_contains(d, "get card", "Taken.");
    assert_cmd_contains(d, "e", "E/W Dirt road");
    assert_cmd_contains(d, "e", "Fork");
    assert_cmd_contains(d, "ne", "There is some food here.");
    assert_cmd_contains(d, "get food", "Taken.");
    assert_cmd_contains(d, "sw", "Fork");
    assert_cmd_contains(d, "se", "ferocious bear");
    assert_cmd_contains(d, "feed bear", "left something behind");
    assert_cmd_contains(d, "get key", "Taken.");
    assert_cmd_contains(d, "se", "emerald bracelet");
    assert_cmd_contains(d, "get bracelet", "Taken.");
    assert_cmd_contains(d, "nw", "SE/NW road");
    assert_cmd_contains(d, "nw", "Fork");
    assert_cmd_contains(d, "ne", "NE/SW road");
    assert_cmd_contains(d, "ne", "End of road");
    assert_cmd_contains(d, "ne", "Old Building hallway");
    assert_cmd_contains(d, "get lamp", "Taken.");
    assert_cmd_contains(d, "e", "Mailroom");
    assert_cmd_contains(d, "examine bins", "Robert");
    assert_cmd_contains(d, "w", "Old Building hallway");
    assert_cmd_contains(d, "w", "Computer room");

    /* 2. VAX 'pokey', UNIX shell, FTP & rlogin to gamma */
    assert_cmd_contains(d, "put cpu in vax", "springs to life");
    g_assert_cmpint(dunnet_score(d), ==, 10);
    assert_cmd_contains(d, "type", "UNIX System V");
    g_assert_cmpstr(dunnet_prompt(d), ==, "login: ");
    g_assert_false(dunnet_is_password_prompt(d));

    g_free(dunnet_command(d, "toukmond"));
    g_assert_cmpstr(dunnet_prompt(d), ==, "Password: ");
    g_assert_true(dunnet_is_password_prompt(d));

    assert_cmd_contains(d, "robert", "Welcome to Unix");
    g_assert_cmpstr(dunnet_prompt(d), ==, "$ ");

    assert_cmd_contains(d, "ls", "paper.o.Z");
    assert_cmd_contains(d, "ls", "lamp.o");
    assert_cmd_contains(d, "ls", "bracelet.o");
    g_free(dunnet_command(d, "uncompress paper.o.Z"));
    assert_cmd_contains(d, "cat paper.o", "worms");
    assert_cmd_contains(d, "cat paper.o", "458");

    assert_cmd_contains(d, "ftp gamma", "Connected to gamma");
    g_assert_cmpstr(dunnet_prompt(d), ==, "Name (gamma:toukmond): ");
    assert_cmd_contains(d, "anonymous", "Guest login ok");
    g_assert_true(dunnet_is_password_prompt(d));
    assert_cmd_contains(d, "toukmond@pokey", "access restrictions apply");
    g_assert_cmpstr(dunnet_prompt(d), ==, "ftp> ");
    assert_cmd_contains(d, "binary", "Type set to I");
    assert_cmd_contains(d, "send lamp.o", "Transfer complete");
    assert_cmd_contains(d, "send bracelet.o", "Transfer complete");
    assert_cmd_contains(d, "quit", "Goodbye");
    g_assert_cmpstr(dunnet_prompt(d), ==, "$ ");

    g_free(dunnet_command(d, "rlogin gamma"));
    g_assert_true(dunnet_is_password_prompt(d));
    assert_cmd_contains(d, "worms", "Receiving room");
    g_assert_cmpstr(dunnet_prompt(d), ==, ">");

    /* 3. Caves, Lake, Sauna & Maze */
    assert_cmd_contains(d, "get all", "Taken.");
    assert_cmd_contains(d, "turn on lamp", "lamp is now on");
    assert_cmd_contains(d, "e", "N/S-E/W junction");
    assert_cmd_contains(d, "n", "Weight room");
    assert_cmd_contains(d, "get preserver", "Taken.");
    assert_cmd_contains(d, "n", "Lakefront");
    assert_cmd_contains(d, "swim", "In lake");
    assert_cmd_contains(d, "get amethyst", "Taken.");
    assert_cmd_contains(d, "n", "Rocky ledge");
    assert_cmd_contains(d, "get all", "Taken.");
    assert_cmd_contains(d, "s", "In lake");
    assert_cmd_contains(d, "s", "Lakefront");
    assert_cmd_contains(d, "s", "Weight room");
    assert_cmd_contains(d, "s", "N/S-E/W junction");

    /* Explore Maze for silver bar, floppy disk and jeweled egg */
    assert_cmd_contains(d, "s", "Maze");
    assert_cmd_contains(d, "e", "Maze");
    assert_cmd_contains(d, "get silver", "Taken.");
    assert_cmd_contains(d, "get floppy", "Taken.");
    assert_cmd_contains(d, "s", "Maze");
    assert_cmd_contains(d, "get egg", "Taken.");
    assert_cmd_contains(d, "d", "Mud room");
    assert_cmd_contains(d, "get coins", "Taken.");
    assert_cmd_contains(d, "u", "Museum lobby");

    /* Go south to Cracked cave wall, blast it, and visit Sauna to melt wax into diamond */
    assert_cmd_contains(d, "s", "Cracked cave wall");
    assert_cmd_contains(d, "use nitro", "BOOOOM!");
    assert_cmd_contains(d, "w", "Cave hallway");
    assert_cmd_contains(d, "n", "Sauna");
    assert_cmd_contains(d, "turn dial", "luke warm");
    assert_cmd_contains(d, "turn dial", "comfortably hot");
    assert_cmd_contains(d, "turn dial", "sparkling diamond");
    assert_cmd_contains(d, "s", "Cave hallway");
    assert_cmd_contains(d, "e", "Cracked cave wall");
    assert_cmd_contains(d, "n", "Museum lobby");

    /* 4. Computer Exhibit: unlock ruby (458) and run RESET.EXE on IBM PC */
    assert_cmd_contains(d, "e", "Computer exhibit");
    assert_cmd_contains(d, "dial 458", "glittering ruby");
    assert_cmd_contains(d, "get ruby", "Taken.");
    assert_cmd_contains(d, "put floppy in pc", "drive A:");
    assert_cmd_contains(d, "type", "IBM Personal Computer DOS");
    g_assert_cmpstr(dunnet_prompt(d), ==, "A>");
    assert_cmd_contains(d, "reset", "Subway power grid online");
    assert_cmd_contains(d, "exit", "step away from the PC");
    g_assert_cmpstr(dunnet_prompt(d), ==, ">");

    /* 5. Score the 7 treasures in the Restroom */
    assert_cmd_contains(d, "w", "Museum lobby");
    assert_cmd_contains(d, "w", "Restroom");
    assert_cmd_contains(d, "put bracelet in urinal", "urinal");
    assert_cmd_contains(d, "put amethyst in urinal", "urinal");
    assert_cmd_contains(d, "put diamond in urinal", "urinal");
    assert_cmd_contains(d, "put silver in urinal", "urinal");
    assert_cmd_contains(d, "put egg in urinal", "urinal");
    assert_cmd_contains(d, "put coins in urinal", "urinal");
    assert_cmd_contains(d, "put ruby in urinal", "urinal");
    assert_cmd_contains(d, "flush", "Whoooosh!");
    g_assert_cmpint(dunnet_score(d), ==, 80);

    /* 6. Subway Train & Endgame */
    assert_cmd_contains(d, "e", "Museum lobby");
    assert_cmd_contains(d, "n", "Subway station");
    assert_cmd_contains(d, "n", "Endgame hallway");
    assert_cmd_contains(d, "answer robert", "Correct!");
    assert_cmd_contains(d, "answer worms", "Correct!");
    assert_cmd_contains(d, "answer 2", "Endgame Treasure Vault");
    g_assert_cmpint(dunnet_score(d), ==, 90);

    assert_cmd_contains(d, "n", "Endgame treasure room");
    assert_cmd_contains(d, "get mona", "Taken.");
    assert_cmd_contains(d, "put mona in pedestal", "Congratulations!");
    g_assert_cmpint(dunnet_score(d), ==, dunnet_max_score());
    g_assert_true(dunnet_is_over(d));

    dunnet_free(d);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dunnet/opening_and_dig_cpu", test_opening_and_dig_cpu);
    g_test_add_func("/dunnet/hazards_and_restart", test_hazards_and_restart);
    g_test_add_func("/dunnet/full_walkthrough_to_victory", test_full_walkthrough_to_victory);
    return g_test_run();
}

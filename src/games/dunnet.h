#ifndef DUNNET_H
#define DUNNET_H

#include <glib.h>

typedef struct DunnetState DunnetState;

/* Create a new Dunnet game state at the starting room ("Dead end"). */
DunnetState* dunnet_new(void);

/* Reset an existing Dunnet game state back to the beginning. */
void dunnet_reset(DunnetState* d);

/* Free a Dunnet game state. */
void dunnet_free(DunnetState* d);

/* Return the opening text printed when a game starts (without trailing prompt),
 * owned by caller (g_free). */
gchar* dunnet_opening_text(DunnetState* d);

/* Execute one line of user input and return the response text (without trailing
 * prompt), owned by caller (g_free). */
gchar* dunnet_command(DunnetState* d, const gchar* line);

/* Current prompt string (">", "$ ", "ftp> ", "login: ", "Password: ", "A>"),
 * static string. */
const gchar* dunnet_prompt(const DunnetState* d);

/* TRUE while the game is waiting for a password (input should not remain echoed). */
gboolean dunnet_is_password_prompt(const DunnetState* d);

/* TRUE when the player has died, quit or won (until "restart" is typed). */
gboolean dunnet_is_over(const DunnetState* d);

/* Current score and maximum possible score (100). */
gint dunnet_score(const DunnetState* d);
gint dunnet_max_score(void);

#endif /* DUNNET_H */

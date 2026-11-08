#ifndef VIM_KEYS_H
#define VIM_KEYS_H

/* Vim keys as Unicode code points. Text characters are themselves; special
 * keys and Ctrl combinations live in the private use area, so a key sequence
 * (macro register, dot repeat, :normal argument) is a plain UTF-8 string. */
#include <gtk/gtk.h>

#define VIM_KEY_ESC      0xE001
#define VIM_KEY_CR       0xE002 /* Return */
#define VIM_KEY_BS       0xE003
#define VIM_KEY_TAB      0xE004
#define VIM_KEY_DEL      0xE005
#define VIM_KEY_LEFT     0xE006
#define VIM_KEY_RIGHT    0xE007
#define VIM_KEY_UP       0xE008
#define VIM_KEY_DOWN     0xE009
#define VIM_KEY_HOME     0xE00A
#define VIM_KEY_END      0xE00B
#define VIM_KEY_PAGEUP   0xE00C
#define VIM_KEY_PAGEDOWN 0xE00D
#define VIM_KEY_INSERT   0xE00E
#define VIM_KEY_NL       '\n'   /* Ctrl-J: a literal newline in Insert mode */

/* Ctrl + c, with c a lowercase letter or one of @ [ \ ] ^ _ */
#define VIM_KEY_CTRL(c)    (0xE100 + (gunichar)(c))
#define VIM_KEY_IS_CTRL(k) ((k) >= 0xE100 && (k) < 0xE180)
#define VIM_KEY_IS_SPECIAL(k) ((k) >= 0xE000 && (k) < 0xE200)

/* Key of a GDK key press, 0 when Vim has no use for it (modifier alone,
 * Alt combinations, function keys, Ctrl+Shift combinations) */
gunichar vim_key_from_gdk(guint keyval, GdkModifierType state);

/* Canonical form: ASCII control characters become keys (ESC, CR, BS, Tab,
 * Ctrl-x), Ctrl-[ is ESC, Ctrl-M is CR, Ctrl-H is BS, Ctrl-I is Tab, Ctrl-J
 * is NL. Applied to every key fed to the engine. */
gunichar vim_key_normalize(gunichar k);

/* Append one key to a key string */
void vim_keys_append(GString* s, gunichar k);

/* "<Esc>", "<CR>", "<BS>", "<Tab>", "<C-r>", "<lt>"... to a key string */
gchar* vim_keys_from_notation(const gchar* notation);

/* Readable form of a key string ("^[" style for :registers and showcmd) */
gchar* vim_keys_to_display(const gchar* keys);

#endif /* VIM_KEYS_H */

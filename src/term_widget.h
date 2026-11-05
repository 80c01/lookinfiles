#ifndef TERM_WIDGET_H
#define TERM_WIDGET_H

/* Built-in terminal widget, for builds without VTE (Windows): the part of
 * the VTE API terminal.c uses, under VTE's names, so the terminal panel is
 * the same code with either. The screen is a vt_screen (xterm sequences),
 * the shell runs on a vt_pty (ConPTY on Windows). Mouse: drag to select;
 * wheel: scrollback (arrow keys on the alternate screen); middle click
 * pastes the selection. Shift+PgUp / Shift+PgDn scroll a page. */

#include <gtk/gtk.h>

#define VTE_TYPE_TERMINAL (vte_terminal_get_type())
G_DECLARE_FINAL_TYPE(VteTerminal, vte_terminal, VTE, TERMINAL, GtkWidget)

/* Version of the VTE API offered (termprops of 0.78) */
#define VTE_CHECK_VERSION(major, minor, micro) \
    (0 > (major) || (0 == (major) && 78 > (minor)) || (0 == (major) && 78 == (minor) && 0 >= (micro)))

typedef enum { VTE_PTY_DEFAULT = 0 } VtePtyFlags;
typedef enum { VTE_FORMAT_TEXT = 1 } VteFormat;
typedef enum { VTE_PROPERTY_ID_XTERM_TITLE = 1 } VtePropertyId;
#define VTE_TERMPROP_XTERM_TITLE "xterm.title"

typedef void (*VteTerminalSpawnAsyncCallback)(VteTerminal* terminal, GPid pid, GError* error,
                                              gpointer user_data);

/* Signals: "child-exited" (gint status), "termprop-changed" (detailed by
 * the property name, const char* name) */
GtkWidget* vte_terminal_new(void);

/* Runs argv now and calls callback (which owns error).
 * child_setup, timeout and cancellable are not supported (ignored). */
void vte_terminal_spawn_async(VteTerminal* terminal, VtePtyFlags pty_flags, const char* working_directory,
                              char** argv, char** envv, GSpawnFlags spawn_flags,
                              GSpawnChildSetupFunc child_setup, gpointer child_setup_data,
                              GDestroyNotify child_setup_data_destroy, int timeout,
                              GCancellable* cancellable, VteTerminalSpawnAsyncCallback callback,
                              gpointer user_data);
/* "child-exited" when pid ends */
void vte_terminal_watch_child(VteTerminal* terminal, GPid pid);

/* Text, background and palette (16 colours: the ANSI ones then the bright ones) */
void vte_terminal_set_colors(VteTerminal* terminal, const GdkRGBA* foreground, const GdkRGBA* background,
                             const GdkRGBA* palette, gsize palette_size);
void vte_terminal_set_color_cursor(VteTerminal* terminal, const GdkRGBA* cursor_background);
void vte_terminal_set_color_highlight(VteTerminal* terminal, const GdkRGBA* highlight_background);
void vte_terminal_set_font(VteTerminal* terminal, const PangoFontDescription* font_desc);

/* The selection to the clipboard; the clipboard to the shell */
void vte_terminal_copy_clipboard_format(VteTerminal* terminal, VteFormat format);
void vte_terminal_paste_clipboard(VteTerminal* terminal);

/* VTE_PROPERTY_ID_XTERM_TITLE: the title the shell set, NULL if none */
const char* vte_terminal_get_termprop_string_by_id(VteTerminal* terminal, int prop, gsize* size);

#endif /* TERM_WIDGET_H */

#ifndef VT_PTY_H
#define VT_PTY_H

/* A program run on a pseudo-terminal, for the built-in terminal
 * (term_widget): a Windows pseudo console (ConPTY, Windows 10 1809 and
 * later) or a POSIX pty. Its output reaches the callback on the main
 * context; what the user types is written to it. Pure GLib. */

#include <glib.h>

typedef struct VtPty VtPty;

typedef void (*VtPtyOutputFunc)(const gchar* data, gsize len, gpointer user_data);

/* Start argv (argv[0] looked up in PATH with search_path) in cwd (NULL: the
 * current directory) with envv (NULL: this environment) on a cols x rows
 * pseudo-terminal. *pid is the child, for g_child_watch_add(); the caller
 * closes it with g_spawn_close_pid(). NULL and error on failure. */
VtPty* vt_pty_spawn(gchar** argv, const gchar* cwd, gchar** envv, gboolean search_path,
                    gint cols, gint rows, VtPtyOutputFunc output, gpointer user_data,
                    GPid* pid, GError** error);
void vt_pty_write(VtPty* pty, const gchar* data, gsize len);
void vt_pty_resize(VtPty* pty, gint cols, gint rows);
/* No more output callbacks; the pseudo-terminal is closed, which ends the
 * child (hang-up / console closed) */
void vt_pty_free(VtPty* pty);

#endif /* VT_PTY_H */

#ifndef GIT_CODE_VIEW_H
#define GIT_CODE_VIEW_H

#include <gtk/gtk.h>

/* The Languages section of the Statistics view of the Git tab: the files of
 * HEAD counted by language (git_code_stats), a summary, a bar of the share
 * of each language's lines (palette colours, a tooltip on each part) and a
 * table: files, lines, non-blank lines, size and share. Read on a worker
 * thread while the section is on screen; read again on a reload only when
 * the tree of HEAD changed. */
typedef struct GitCodeView GitCodeView;

/* A new section; freed with its widget */
GitCodeView* git_code_view_new(void);

GtkWidget* git_code_view_widget(GitCodeView* v);

/* Count the files of the repository at root (NULL: none); reload: again
 * even when root is the one shown (commits were made) */
void git_code_view_set_root(GitCodeView* v, const gchar* root, gboolean reload);

/* Cancel the read running (at exit) */
void git_code_view_cancel(GitCodeView* v);

#endif /* GIT_CODE_VIEW_H */

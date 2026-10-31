#ifndef JUMP_LIST_H
#define JUMP_LIST_H

/* Back / forward history of code navigation jumps (docs/code-nav.md),
 * across files, like the navigation history of VS Code or Emacs' xref
 * marker stack. Locations are kept as line and byte column: they do not
 * follow later edits. Pure GLib, unit-tested. */
#include <glib.h>

typedef struct {
    gchar* path;
    gint line;    /* 0-based */
    gint column;  /* byte index in the line */
} JumpLocation;

typedef struct JumpList JumpList;

/* A history of at most max locations (the oldest are dropped) */
JumpList* jump_list_new(guint max);
void jump_list_free(JumpList* list);

/* Before a jump: remember where it starts. Locations after the current
 * one (left by going back) are dropped; a location on the same line as the
 * last one replaces it. */
void jump_list_push(JumpList* list, const gchar* path, gint line, gint column);

/* Go back from the current location (path, line, column): the location to
 * show, or NULL at the start of the history. The current location is
 * remembered so that forward can return to it. */
const JumpLocation* jump_list_back(JumpList* list, const gchar* path, gint line, gint column);
/* Go forward after going back: the location to show, or NULL */
const JumpLocation* jump_list_forward(JumpList* list);

guint jump_list_length(JumpList* list);
/* Index of the current location: length when not walking the history */
guint jump_list_position(JumpList* list);

/* Drop the locations in path (file deleted) */
void jump_list_forget_file(JumpList* list, const gchar* path);

#endif /* JUMP_LIST_H */

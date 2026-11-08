#ifndef EDITOR_INFO_H
#define EDITOR_INFO_H

/* What the bar of an editor tab tells about its file: git branch, relative
 * path, language, code navigation backend and diagnostics. Each mode
 * (editor_modes) shows it in its own style. */
#include "core/app_types.h"

typedef struct {
    gchar* branch;      /* git branch of the file's repository, NULL outside one */
    gchar* relpath;     /* path from the repository root, else the current folder, else ~ */
    gchar* language;    /* "C", "Python", "Org", "Text"... */
    gchar* language_id; /* lower case name, as Vim's filetype: "c", "python3", "org", "text" */
    gchar* backend;     /* "clangd", "clangd (starting)", "ctags"..., NULL for none */
    gboolean server;    /* backend is a language server */
    guint errors;       /* diagnostics of a ready language server */
    guint warnings;
} EditorInfo;

/* Fill info for tab (the branch and path are read once per file, until
 * editor_info_forget()); free with editor_info_clear() */
void editor_info_get(AppData* app, EditorTab* tab, EditorInfo* info);
void editor_info_clear(EditorInfo* info);

/* Read the branch and path of tab again on the next editor_info_get() (the
 * repository changed) */
void editor_info_forget(EditorTab* tab);

#endif /* EDITOR_INFO_H */

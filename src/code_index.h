#ifndef CODE_INDEX_H
#define CODE_INDEX_H

/* Built-in code index (TASK_CODE_NAV.md, phase 0): the project of a file,
 * its source files, and the definition lookup over them with the scanners
 * of code_scan. Nothing is cached: each lookup reads the files of the same
 * language family that contain the name, within CODE_INDEX_MAX_FILES and
 * CODE_INDEX_MAX_BYTES. Blocking (reads files, runs git): call it from a
 * worker thread. Pure GLib, unit-tested. */
#include <gio/gio.h>

#include "code_symbol.h"

#define CODE_INDEX_MAX_FILES 20000
#define CODE_INDEX_MAX_BYTES (96 * 1024 * 1024)
#define CODE_INDEX_MAX_FILE_BYTES (4 * 1024 * 1024)

/* Root of the project of path: its git repository, else its directory.
 * Caller frees. */
gchar* code_index_project_root(const gchar* path);

/* Absolute paths of the source files of family (code_lang_family()) under
 * root: `git ls-files` (tracked and untracked, ignored files excluded) in a
 * repository, else a directory walk without hidden and build directories.
 * At most CODE_INDEX_MAX_FILES; *truncated (may be NULL) tells when the
 * limit was hit. */
GPtrArray* code_index_project_files(const gchar* root, CodeLang family, GCancellable* cancellable,
                                    gboolean* truncated);

typedef struct {
    gchar* path;          /* absolute path */
    CodeSymbol* symbol;
} CodeMatch;

void code_match_free(CodeMatch* match);

typedef struct {
    const gchar* name;        /* identifier looked up */
    CodeLang lang;            /* language of the origin file */
    gboolean declaration;     /* TRUE: declarations first (prototypes...), else definitions */
    const gchar* origin_path; /* file of the cursor: scanned first, ranked first */
    GHashTable* texts;        /* path -> text of open buffers (used instead of the file), or NULL */
} CodeQuery;

/* Symbols named query->name in the origin file and in files (absolute
 * paths of the same family), best first: the kind wanted (definition or
 * declaration), then the origin file, its directory, then other files by
 * path and line. *truncated (may be NULL): the byte limit was hit. */
GPtrArray* code_index_find(const CodeQuery* query, GPtrArray* files, GCancellable* cancellable,
                           gboolean* truncated);

#endif /* CODE_INDEX_H */

#ifndef CODE_INDEX_H
#define CODE_INDEX_H

/* Built-in code index (docs/code-nav.md): the project of a file,
 * its source files, and the definition lookup over them with the scanners
 * of code_scan. Nothing is cached: each lookup reads the files of the same
 * language family that contain the name, within CODE_INDEX_MAX_FILES and
 * CODE_INDEX_MAX_BYTES. Blocking (reads files, runs git): call it from a
 * worker thread. Pure GLib, unit-tested. */
#include <gio/gio.h>

#include "code_symbol.h"
#include "code_tags.h"

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
/* Same, every file whatever its language (quick open, text search) */
GPtrArray* code_index_all_files(const gchar* root, GCancellable* cancellable, gboolean* truncated);

/* rel (relative to the root) is listed */
typedef gboolean (*CodeIndexKeepFunc)(const gchar* rel, gpointer data);

/* Every file for which keep (NULL: all) returns TRUE; with ignored, also the
 * files git ignores (outside git: hidden and build folders) */
GPtrArray* code_index_some_files(const gchar* root, gboolean ignored, CodeIndexKeepFunc keep, gpointer keep_data,
                                 GCancellable* cancellable, gboolean* truncated);

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
    CodeTagsIndex* tags;      /* ctags index of the project (updated first), or NULL: built-in scanners */
} CodeQuery;

/* Symbols named query->name in the origin file and in files (absolute
 * paths of the same family), best first: the kind wanted (definition or
 * declaration), then the origin file, its directory, then other files by
 * path and line. With query->tags, the files come from the ctags index and
 * only open buffers (query->texts) and files outside the list are scanned;
 * when ctags fails, everything is scanned. *truncated (may be NULL): the
 * byte limit was hit. */
GPtrArray* code_index_find(const CodeQuery* query, GPtrArray* files, GCancellable* cancellable,
                           gboolean* truncated);

/* --- References (whole-word occurrences) --- */

#define CODE_INDEX_MAX_REFERENCES 5000

typedef struct {
    gchar* path;
    gint line;      /* 0-based */
    gint column;    /* byte index of the name in the line */
    gint length;    /* bytes matched (0: the length of the name looked for) */
    gchar* text;    /* the line (tabs as spaces, no newline, valid UTF-8) */
} CodeReference;

void code_reference_free(CodeReference* ref);

/* Every whole-word occurrence of name in the origin file and files (same
 * family), as CodeReference*: the origin file first, then by path and line;
 * open buffers (texts) are read instead of their files. At most
 * CODE_INDEX_MAX_REFERENCES (*truncated then, or when the byte budget is
 * spent). */
GPtrArray* code_index_references(const gchar* name, CodeLang lang, const gchar* origin_path, GHashTable* texts,
                                 GPtrArray* files, GCancellable* cancellable, gboolean* truncated);

#endif /* CODE_INDEX_H */

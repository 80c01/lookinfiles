#ifndef GIT_CODE_STATS_H
#define GIT_CODE_STATS_H

/* What a repository holds: its files at HEAD counted by language (files,
 * lines, non-blank lines, bytes). Only the files git tracks are counted;
 * binary files (a NUL byte in the first 8000, as git tells), very large
 * ones, generated and vendored ones (lock files, minified files,
 * node_modules/, vendor/..., the linguist-generated and linguist-vendored
 * attributes) are left out. The language comes from the file name, then
 * its extension, then the #! line. Read through libgit2 (no git program);
 * no AppData and no widgets. */
#include <gio/gio.h>

/* Files larger than this are left out (counted in GitCodeStats.large) */
#define GIT_CODE_MAX_BYTES (8u * 1024u * 1024u)

typedef struct {
    gchar* name; /* the language, NULL: text in no language known */
    guint files;
    guint64 lines;
    guint64 nonblank;
    guint64 bytes;
} GitLangStats;

typedef struct {
    guint files; /* counted */
    guint64 lines;
    guint64 nonblank;
    guint64 bytes;
    guint binary;    /* files left out */
    guint generated; /* generated or vendored */
    guint large;     /* over GIT_CODE_MAX_BYTES */
    GPtrArray* languages; /* GitLangStats*, most lines first after git_code_stats_finish() (unknown last) */
    GHashTable* by_name;  /* name ("" for NULL) -> GitLangStats* (in languages) */
    gchar* tree;          /* id of the tree counted (git_code_stats_read()), or NULL */
} GitCodeStats;

GitCodeStats* git_code_stats_new(void);
void git_code_stats_free(GitCodeStats* stats);

/* The language of the file at path (below the top of the work tree) whose
 * content starts with head (len bytes, may be 0); NULL when none is known */
const gchar* git_code_language(const gchar* path, const gchar* head, gsize len);

/* TRUE when the name of path says the file is generated or vendored */
gboolean git_code_path_generated(const gchar* path);

/* TRUE when data looks binary: a NUL byte in its first 8000 bytes */
gboolean git_code_is_binary(const gchar* data, gsize len);

/* Lines of data (a last line without a newline counts), and those with
 * something else than white space */
void git_code_count_lines(const gchar* data, gsize len, guint64* lines, guint64* nonblank);

/* Count the file at path with its content */
void git_code_stats_add(GitCodeStats* stats, const gchar* path, const gchar* data, gsize len);

/* After the last git_code_stats_add(): the languages sorted */
void git_code_stats_finish(GitCodeStats* stats);

/* The files of the tree of HEAD in the repository at root, counted; an
 * empty GitCodeStats without commits; NULL when root is not a repository,
 * cancellable was cancelled, or the tree of HEAD is unless_tree (not NULL:
 * the id of the tree already counted; *unchanged is then TRUE). Worker
 * thread. */
GitCodeStats* git_code_stats_read(const gchar* root, const gchar* unless_tree, gboolean* unchanged,
                                  GCancellable* cancellable);

#endif /* GIT_CODE_STATS_H */

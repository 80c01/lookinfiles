#ifndef CODE_TAGS_H
#define CODE_TAGS_H

/* ctags backend of code navigation (docs/code-nav.md): finds
 * Universal Ctags (JSON output), reads its tags, and keeps an index per
 * project that re-tags only the files whose size or date changed, saved to a
 * cache file between sessions. Blocking (runs ctags, reads files): worker
 * threads only; an index may be used by several threads (it has a lock).
 * Pure GLib, unit-tested. */
#include <gio/gio.h>

#include "code_symbol.h"

/* Path of a Universal Ctags with JSON output in PATH ("ctags",
 * "universal-ctags", "ctags-universal"), or NULL. Checked once. */
const gchar* code_tags_program(void);

/* One line of `ctags --output-format=json` output: FALSE for other lines
 * (pseudo tags, kinds that are not symbols: headers, labels, locals...).
 * *path: the file of the tag (caller frees), *sym: the symbol (column of
 * the name found in the line). */
gboolean code_tags_parse_line(const gchar* line, gsize len, gchar** path, CodeSymbol** sym);

typedef struct CodeTagsIndex CodeTagsIndex;

/* An empty index running program; cache_path (may be NULL): file where the
 * tags are kept between sessions (loaded here when it exists) */
CodeTagsIndex* code_tags_index_new(const gchar* program, const gchar* cache_path);
void code_tags_index_free(CodeTagsIndex* index);

/* Bring the index up to date with files (absolute paths): new and changed
 * files are tagged, missing ones dropped, the cache file rewritten when
 * something changed. FALSE when ctags failed (the index is unchanged). */
gboolean code_tags_index_update(CodeTagsIndex* index, GPtrArray* files, GCancellable* cancellable);

/* Symbols named name: an array of CodeTagsHit, freed with the array */
typedef struct {
    gchar* path;
    CodeSymbol* symbol;
} CodeTagsHit;

GPtrArray* code_tags_index_lookup(CodeTagsIndex* index, const gchar* name);
/* Symbols whose name starts with prefix (case-insensitive), at most limit
 * (symbol pickers) */
GPtrArray* code_tags_index_search(CodeTagsIndex* index, const gchar* prefix, guint limit);
/* Number of files and symbols indexed */
void code_tags_index_stats(CodeTagsIndex* index, guint* files, guint* symbols);

#endif /* CODE_TAGS_H */

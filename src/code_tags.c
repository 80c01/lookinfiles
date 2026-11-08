/* ctags backend of code navigation (see code_tags.h) */
#include "code_tags.h"
#include "formats/json_view.h"
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_HEADER "LOOKIN-TAGS 1"

/* ------------------------------------------------------------------------- */
/* Program                                                                   */
/* ------------------------------------------------------------------------- */

static gchar* run_capture(const gchar* program, const gchar* arg) {
    gchar* argv[] = {(gchar*)program, (gchar*)arg, NULL};
    gchar* out = NULL;
    gint status = 0;
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL)) {
        g_free(out);
        return NULL;
    }
    return out;
}

const gchar* code_tags_program(void) {
    static gsize done = 0;
    static gchar* program = NULL;
    if (g_once_init_enter(&done)) {
        static const gchar* const NAMES[] = {"ctags", "universal-ctags", "ctags-universal"};
        for (gsize i = 0; i < G_N_ELEMENTS(NAMES) && program == NULL; i++) {
            gchar* path = g_find_program_in_path(NAMES[i]);
            if (path == NULL) continue;
            gchar* version = run_capture(path, "--version");
            gchar* features = version != NULL && strstr(version, "Universal Ctags") != NULL
                                  ? run_capture(path, "--list-features")
                                  : NULL;
            if (features != NULL && strstr(features, "json") != NULL) program = g_strdup(path);
            g_free(features);
            g_free(version);
            g_free(path);
        }
        g_once_init_leave(&done, 1);
    }
    return program;
}

/* ------------------------------------------------------------------------- */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------- */

/* Kind of a ctags kind name; FALSE for kinds that are not symbols */
static gboolean map_kind(const gchar* kind, CodeSymbolKind* out, gboolean* decl) {
    static const gchar* const SKIP[] = {
        "header", "label", "parameter", "local", "file", "filename", "import", "include", "using",
        "chapter", "section", "subsection", "subsubsection", "heading1", "heading2", "heading3",
        "anchor", "unknown", "argument", "macroparam", "condition", "key",
    };
    static const struct {
        const gchar* name;
        CodeSymbolKind kind;
        gboolean decl;
    } MAP[] = {
        {"function", CODE_SYM_FUNCTION, FALSE}, {"func", CODE_SYM_FUNCTION, FALSE},
        {"subroutine", CODE_SYM_FUNCTION, FALSE}, {"procedure", CODE_SYM_FUNCTION, FALSE},
        {"prototype", CODE_SYM_FUNCTION, TRUE}, {"subroutineDeclaration", CODE_SYM_FUNCTION, TRUE},
        {"method", CODE_SYM_METHOD, FALSE}, {"singletonMethod", CODE_SYM_METHOD, FALSE},
        {"accessor", CODE_SYM_METHOD, FALSE}, {"methodSpec", CODE_SYM_METHOD, TRUE},
        {"macro", CODE_SYM_MACRO, FALSE}, {"define", CODE_SYM_MACRO, FALSE},
        {"typedef", CODE_SYM_TYPE, FALSE}, {"struct", CODE_SYM_TYPE, FALSE}, {"union", CODE_SYM_TYPE, FALSE},
        {"enum", CODE_SYM_TYPE, FALSE}, {"type", CODE_SYM_TYPE, FALSE}, {"alias", CODE_SYM_TYPE, FALSE},
        {"typealias", CODE_SYM_TYPE, FALSE}, {"trait", CODE_SYM_TYPE, FALSE}, {"talias", CODE_SYM_TYPE, FALSE},
        {"class", CODE_SYM_CLASS, FALSE}, {"interface", CODE_SYM_CLASS, FALSE}, {"object", CODE_SYM_CLASS, FALSE},
        {"protocol", CODE_SYM_CLASS, FALSE}, {"record", CODE_SYM_CLASS, FALSE},
        {"enumerator", CODE_SYM_CONSTANT, FALSE}, {"enumConstant", CODE_SYM_CONSTANT, FALSE},
        {"constant", CODE_SYM_CONSTANT, FALSE}, {"const", CODE_SYM_CONSTANT, FALSE},
        {"member", CODE_SYM_FIELD, FALSE}, {"field", CODE_SYM_FIELD, FALSE}, {"property", CODE_SYM_FIELD, FALSE},
        {"attribute", CODE_SYM_FIELD, FALSE},
        {"variable", CODE_SYM_VARIABLE, FALSE}, {"var", CODE_SYM_VARIABLE, FALSE},
        {"externvar", CODE_SYM_VARIABLE, TRUE},
        {"namespace", CODE_SYM_MODULE, FALSE}, {"module", CODE_SYM_MODULE, FALSE},
        {"package", CODE_SYM_MODULE, FALSE},
        {"target", CODE_SYM_TARGET, FALSE},
    };
    *decl = FALSE;
    if (kind == NULL) return FALSE;
    for (gsize i = 0; i < G_N_ELEMENTS(SKIP); i++) {
        if (strcmp(kind, SKIP[i]) == 0) return FALSE;
    }
    for (gsize i = 0; i < G_N_ELEMENTS(MAP); i++) {
        if (strcmp(kind, MAP[i].name) == 0) {
            *out = MAP[i].kind;
            *decl = MAP[i].decl;
            return TRUE;
        }
    }
    *out = CODE_SYM_VARIABLE;
    return TRUE;
}

/* The source line from a ctags pattern "/^line$/" */
static gchar* pattern_text(const gchar* pattern) {
    if (pattern == NULL) return g_strdup("");
    gsize len = strlen(pattern);
    const gchar* s = pattern;
    if (len >= 2 && s[0] == '/' && s[len - 1] == '/') {
        s++;
        len -= 2;
    }
    if (len > 0 && s[0] == '^') {
        s++;
        len--;
    }
    if (len > 0 && s[len - 1] == '$') len--;
    GString* out = g_string_sized_new(len);
    for (gsize i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len && (s[i + 1] == '/' || s[i + 1] == '\\' || s[i + 1] == '$' ||
                                            s[i + 1] == '^')) {
            i++;
        }
        g_string_append_c(out, s[i]);
    }
    return g_string_free(out, FALSE);
}

static gchar* member_string(const JsonValue* obj, const gchar* key) {
    const JsonValue* v = json_value_get(obj, key);
    return v != NULL ? json_value_dup_string(v) : NULL;
}

/* Byte index of name as a whole word in text, 0 when not found */
static gint name_column(const gchar* text, const gchar* name) {
    gsize n = strlen(name);
    for (const gchar* p = text; (p = strstr(p, name)) != NULL; p++) {
        gboolean before = p == text || !(g_ascii_isalnum(p[-1]) || p[-1] == '_' || p[-1] == '$');
        gboolean after = !(g_ascii_isalnum(p[n]) || p[n] == '_');
        if (before && after) return (gint)(p - text);
    }
    return 0;
}

gboolean code_tags_parse_line(const gchar* line, gsize len, gchar** path, CodeSymbol** sym) {
    *path = NULL;
    *sym = NULL;
    if (line == NULL || len == 0 || line[0] != '{') return FALSE;
    JsonValue* obj = json_value_parse(line, len, NULL);
    if (obj == NULL) return FALSE;
    gboolean ok = FALSE;
    gchar* type = member_string(obj, "_type");
    gchar* kind = member_string(obj, "kind");
    gchar* name = member_string(obj, "name");
    gchar* file = member_string(obj, "path");
    const JsonValue* line_v = json_value_get(obj, "line");
    CodeSymbolKind k = CODE_SYM_VARIABLE;
    gboolean decl = FALSE;
    if (g_strcmp0(type, "tag") == 0 && name != NULL && name[0] != '\0' && file != NULL && line_v != NULL &&
        map_kind(kind, &k, &decl) && strncmp(name, "__anon", 6) != 0) {
        gchar* pattern = member_string(obj, "pattern");
        gchar* text = pattern_text(pattern);
        gint lineno = (gint)json_value_get_number(line_v) - 1;
        *sym = code_symbol_new(name, strlen(name), k, decl, MAX(lineno, 0), name_column(text, name), text,
                               strlen(text));
        gchar* signature = member_string(obj, "signature");
        gchar* typeref = member_string(obj, "typeref");
        /* typeref: "typename:int *", or "struct:Name" ("struct:__anon12" when unnamed) */
        gchar* type_name = NULL;
        if (typeref != NULL) {
            const gchar* colon = strchr(typeref, ':');
            if (g_str_has_prefix(typeref, "typename:")) type_name = g_strdup(typeref + 9);
            else if (colon != NULL && g_str_has_prefix(colon + 1, "__anon")) type_name = g_strndup(typeref, colon - typeref);
            else if (colon != NULL) type_name = g_strdup_printf("%.*s %s", (gint)(colon - typeref), typeref, colon + 1);
            else type_name = g_strdup(typeref);
        }
        if (signature != NULL) {
            (*sym)->signature = type_name != NULL ? g_strdup_printf("%s %s%s", type_name, name, signature)
                                                  : g_strdup_printf("%s%s", name, signature);
        } else if (type_name != NULL) {
            (*sym)->signature = g_strdup_printf("%s %s", type_name, name);
        }
        g_free(type_name);
        g_free(typeref);
        g_free(signature);
        g_free(text);
        g_free(pattern);
        *path = file;
        file = NULL;
        ok = TRUE;
    }
    g_free(file);
    g_free(name);
    g_free(kind);
    g_free(type);
    json_value_free(obj);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Index                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint64 mtime;
    gint64 size;
    GPtrArray* symbols;  /* CodeSymbol* */
} TagFile;

typedef struct {
    const gchar* path;   /* key of files */
    CodeSymbol* symbol;  /* owned by the TagFile */
} TagRef;

struct CodeTagsIndex {
    GMutex lock;
    gchar* program;
    gchar* cache_path;
    GHashTable* files;   /* path -> TagFile* */
    GHashTable* names;   /* name -> GArray of TagRef, rebuilt when dirty */
    gboolean names_dirty;
};

static void tag_file_free(gpointer p) {
    TagFile* f = p;
    g_ptr_array_unref(f->symbols);
    g_free(f);
}

static TagFile* tag_file_new(gint64 mtime, gint64 size) {
    TagFile* f = g_new0(TagFile, 1);
    f->mtime = mtime;
    f->size = size;
    f->symbols = g_ptr_array_new_with_free_func((GDestroyNotify)code_symbol_free);
    return f;
}

static void names_value_free(gpointer p) {
    g_array_free(p, TRUE);
}

/* Fields of the cache file are tab-separated: no tab or newline inside */
static gchar* cache_field(const gchar* s) {
    gchar* c = g_strdup(s != NULL ? s : "");
    for (gchar* p = c; *p != '\0'; p++) {
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    }
    return c;
}

static void load_cache(CodeTagsIndex* index) {
    gchar* data = NULL;
    gsize len = 0;
    if (index->cache_path == NULL || !g_file_get_contents(index->cache_path, &data, &len, NULL)) return;
    gchar** lines = g_strsplit(data, "\n", -1);
    g_free(data);
    if (lines[0] == NULL || strcmp(lines[0], CACHE_HEADER) != 0) {
        g_strfreev(lines);
        return;
    }
    TagFile* current = NULL;
    for (gint i = 1; lines[i] != NULL; i++) {
        gchar** f = g_strsplit(lines[i], "\t", 8);
        guint n = g_strv_length(f);
        if (n == 4 && strcmp(f[0], "F") == 0) {
            current = tag_file_new(g_ascii_strtoll(f[1], NULL, 10), g_ascii_strtoll(f[2], NULL, 10));
            g_hash_table_replace(index->files, g_strdup(f[3]), current);
        } else if (n == 8 && strcmp(f[0], "S") == 0 && current != NULL) {
            CodeSymbol* sym = code_symbol_new(f[5], strlen(f[5]), (CodeSymbolKind)atoi(f[1]), atoi(f[2]) != 0,
                                              atoi(f[3]), atoi(f[4]), f[6], strlen(f[6]));
            if (f[7][0] != '\0') sym->signature = g_strdup(f[7]);
            g_ptr_array_add(current->symbols, sym);
        }
        g_strfreev(f);
    }
    g_strfreev(lines);
    index->names_dirty = TRUE;
}

static void save_cache(CodeTagsIndex* index) {
    if (index->cache_path == NULL) return;
    GString* out = g_string_new(CACHE_HEADER "\n");
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, index->files);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        TagFile* f = value;
        gchar* path = cache_field(key);
        g_string_append_printf(out, "F\t%" G_GINT64_FORMAT "\t%" G_GINT64_FORMAT "\t%s\n", f->mtime, f->size, path);
        g_free(path);
        for (guint i = 0; i < f->symbols->len; i++) {
            CodeSymbol* s = g_ptr_array_index(f->symbols, i);
            gchar* name = cache_field(s->name);
            gchar* text = cache_field(s->text);
            gchar* sig = cache_field(s->signature);
            g_string_append_printf(out, "S\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", (gint)s->kind, s->declaration ? 1 : 0,
                                   s->line, s->column, name, text, sig);
            g_free(sig);
            g_free(text);
            g_free(name);
        }
    }
    gchar* dir = g_path_get_dirname(index->cache_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_file_set_contents(index->cache_path, out->str, (gssize)out->len, NULL);
    g_string_free(out, TRUE);
}

CodeTagsIndex* code_tags_index_new(const gchar* program, const gchar* cache_path) {
    CodeTagsIndex* index = g_new0(CodeTagsIndex, 1);
    g_mutex_init(&index->lock);
    index->program = g_strdup(program);
    index->cache_path = g_strdup(cache_path);
    index->files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tag_file_free);
    index->names = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, names_value_free);
    load_cache(index);
    return index;
}

void code_tags_index_free(CodeTagsIndex* index) {
    if (index == NULL) return;
    g_hash_table_destroy(index->names);
    g_hash_table_destroy(index->files);
    g_free(index->program);
    g_free(index->cache_path);
    g_mutex_clear(&index->lock);
    g_free(index);
}

/* Run ctags on paths; the tags by file (path -> TagFile*), NULL on failure */
static GHashTable* run_ctags(CodeTagsIndex* index, GPtrArray* paths, GHashTable* stats,
                             GCancellable* cancellable) {
    GSubprocessLauncher* launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    GError* err = NULL;
    GSubprocess* proc = g_subprocess_launcher_spawn(
        launcher, &err, index->program, "--output-format=json", "--fields=+nKSset", "--pattern-length-limit=0",
        "--kinds-C=+px", "--kinds-C++=+px", "--sort=no", "-f", "-", "-L", "-", NULL);
    g_object_unref(launcher);
    if (proc == NULL) {
        g_clear_error(&err);
        return NULL;
    }
    GString* list = g_string_new(NULL);
    for (guint i = 0; i < paths->len; i++) {
        g_string_append(list, g_ptr_array_index(paths, i));
        g_string_append_c(list, '\n');
    }
    GBytes* input = g_string_free_to_bytes(list);
    GBytes* output = NULL;
    gboolean ok = g_subprocess_communicate(proc, input, cancellable, &output, NULL, &err) &&
                  g_subprocess_get_if_exited(proc) && g_subprocess_get_exit_status(proc) == 0;
    g_clear_error(&err);
    g_bytes_unref(input);
    g_object_unref(proc);
    if (!ok || output == NULL) {
        if (output != NULL) g_bytes_unref(output);
        return NULL;
    }
    GHashTable* result = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tag_file_free);
    for (guint i = 0; i < paths->len; i++) {
        const gchar* p = g_ptr_array_index(paths, i);
        const gint64* st = g_hash_table_lookup(stats, p);
        g_hash_table_insert(result, g_strdup(p), tag_file_new(st != NULL ? st[0] : 0, st != NULL ? st[1] : 0));
    }
    gsize len = 0;
    const gchar* data = g_bytes_get_data(output, &len);
    gsize start = 0;
    for (gsize i = 0; i <= len; i++) {
        if (i < len && data[i] != '\n') continue;
        gchar* path = NULL;
        CodeSymbol* sym = NULL;
        if (i > start && code_tags_parse_line(data + start, i - start, &path, &sym)) {
            TagFile* f = g_hash_table_lookup(result, path);
            if (f != NULL) g_ptr_array_add(f->symbols, sym);
            else code_symbol_free(sym);
            g_free(path);
        }
        start = i + 1;
    }
    g_bytes_unref(output);
    return result;
}

gboolean code_tags_index_update(CodeTagsIndex* index, GPtrArray* files, GCancellable* cancellable) {
    if (index == NULL || index->program == NULL || files == NULL) return FALSE;
    /* Stat outside the lock */
    GHashTable* wanted = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
    for (guint i = 0; i < files->len; i++) {
        const gchar* p = g_ptr_array_index(files, i);
        GStatBuf st;
        if (g_stat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        gint64* v = g_new(gint64, 2);
        v[0] = (gint64)st.st_mtime;
        v[1] = (gint64)st.st_size;
        g_hash_table_insert(wanted, (gpointer)p, v);
    }
    g_mutex_lock(&index->lock);
    gboolean changed = FALSE;
    /* Files gone from the project */
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, index->files);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        if (!g_hash_table_contains(wanted, key)) {
            g_hash_table_iter_remove(&it);
            changed = TRUE;
        }
    }
    GPtrArray* stale = g_ptr_array_new();
    g_hash_table_iter_init(&it, wanted);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        const gint64* st = value;
        TagFile* f = g_hash_table_lookup(index->files, key);
        if (f == NULL || f->mtime != st[0] || f->size != st[1]) g_ptr_array_add(stale, key);
    }
    gboolean ok = TRUE;
    if (stale->len > 0) {
        GHashTable* fresh = run_ctags(index, stale, wanted, cancellable);
        if (fresh == NULL) {
            ok = FALSE;
        } else {
            g_hash_table_iter_init(&it, fresh);
            while (g_hash_table_iter_next(&it, &key, &value)) {
                g_hash_table_iter_steal(&it);
                g_hash_table_replace(index->files, key, value);
            }
            g_hash_table_destroy(fresh);
            changed = TRUE;
        }
    }
    if (changed) {
        index->names_dirty = TRUE;
        save_cache(index);
    }
    g_mutex_unlock(&index->lock);
    g_ptr_array_free(stale, TRUE);
    g_hash_table_destroy(wanted);
    return ok;
}

static void rebuild_names(CodeTagsIndex* index) {
    if (!index->names_dirty) return;
    g_hash_table_remove_all(index->names);
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, index->files);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        TagFile* f = value;
        for (guint i = 0; i < f->symbols->len; i++) {
            CodeSymbol* s = g_ptr_array_index(f->symbols, i);
            GArray* refs = g_hash_table_lookup(index->names, s->name);
            if (refs == NULL) {
                refs = g_array_new(FALSE, FALSE, sizeof(TagRef));
                g_hash_table_insert(index->names, s->name, refs);
            }
            TagRef r = {key, s};
            g_array_append_val(refs, r);
        }
    }
    index->names_dirty = FALSE;
}

static void hit_free(gpointer p) {
    CodeTagsHit* h = p;
    g_free(h->path);
    code_symbol_free(h->symbol);
    g_free(h);
}

static void add_hit(GPtrArray* out, const TagRef* r) {
    CodeTagsHit* h = g_new0(CodeTagsHit, 1);
    h->path = g_strdup(r->path);
    h->symbol = code_symbol_copy(r->symbol);
    g_ptr_array_add(out, h);
}

GPtrArray* code_tags_index_lookup(CodeTagsIndex* index, const gchar* name) {
    GPtrArray* out = g_ptr_array_new_with_free_func(hit_free);
    if (index == NULL || name == NULL) return out;
    g_mutex_lock(&index->lock);
    rebuild_names(index);
    GArray* refs = g_hash_table_lookup(index->names, name);
    for (guint i = 0; refs != NULL && i < refs->len; i++) add_hit(out, &g_array_index(refs, TagRef, i));
    g_mutex_unlock(&index->lock);
    return out;
}

GPtrArray* code_tags_index_search(CodeTagsIndex* index, const gchar* prefix, guint limit) {
    GPtrArray* out = g_ptr_array_new_with_free_func(hit_free);
    if (index == NULL || prefix == NULL) return out;
    gchar* low = g_ascii_strdown(prefix, -1);
    gsize n = strlen(low);
    g_mutex_lock(&index->lock);
    rebuild_names(index);
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, index->names);
    while (g_hash_table_iter_next(&it, &key, &value) && out->len < limit) {
        if (g_ascii_strncasecmp(key, low, n) != 0) continue;
        GArray* refs = value;
        for (guint i = 0; i < refs->len && out->len < limit; i++) add_hit(out, &g_array_index(refs, TagRef, i));
    }
    g_mutex_unlock(&index->lock);
    g_free(low);
    return out;
}

void code_tags_index_stats(CodeTagsIndex* index, guint* files, guint* symbols) {
    guint nf = 0, ns = 0;
    if (index != NULL) {
        g_mutex_lock(&index->lock);
        nf = g_hash_table_size(index->files);
        GHashTableIter it;
        gpointer key, value;
        g_hash_table_iter_init(&it, index->files);
        while (g_hash_table_iter_next(&it, &key, &value)) ns += ((TagFile*)value)->symbols->len;
        g_mutex_unlock(&index->lock);
    }
    if (files != NULL) *files = nf;
    if (symbols != NULL) *symbols = ns;
}

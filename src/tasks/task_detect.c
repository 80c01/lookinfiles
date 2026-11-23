/* The build and test tasks of a project (see task_detect.h) */
#include "tasks/task_detect.h"
#include <string.h>

/* Make targets offered at most */
#define MAX_MAKE_TARGETS 40
/* Build files read at most */
#define MAX_FILE_BYTES (1024 * 1024)

/* The files that make a folder a project's */
static const gchar* const MARKERS[] = {
    "Makefile", "makefile", "GNUmakefile", "Cargo.toml", "package.json", "go.mod", "CMakeLists.txt",
    "meson.build", "build.ninja", "pyproject.toml", "setup.py", "pytest.ini",
};

TaskDef* task_def_new(const gchar* label, const gchar* const* argv, const gchar* dir) {
    TaskDef* def = g_new0(TaskDef, 1);
    def->label = g_strdup(label);
    def->argv = g_strdupv((gchar**)argv);
    def->dir = g_strdup(dir);
    return def;
}

TaskDef* task_def_copy(const TaskDef* def) {
    return def != NULL ? task_def_new(def->label, (const gchar* const*)def->argv, def->dir) : NULL;
}

void task_def_free(TaskDef* def) {
    if (def == NULL) return;
    g_free(def->label);
    g_strfreev(def->argv);
    g_free(def->dir);
    g_free(def);
}

gboolean task_def_equal(const TaskDef* a, const TaskDef* b) {
    if (a == NULL || b == NULL) return a == b;
    return g_strcmp0(a->dir, b->dir) == 0 && g_strv_equal((const gchar* const*)a->argv, (const gchar* const*)b->argv);
}

static gboolean has_file(const gchar* dir, const gchar* name) {
    gchar* path = g_build_filename(dir, name, NULL);
    gboolean found = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return found;
}

static gchar* read_file(const gchar* dir, const gchar* name) {
    gchar* path = g_build_filename(dir, name, NULL);
    gchar* text = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &text, &len, NULL)) text = NULL;
    else if (len > MAX_FILE_BYTES) text[MAX_FILE_BYTES] = '\0';
    g_free(path);
    return text;
}

gchar* task_detect_project_dir(const gchar* start) {
    if (start == NULL) return NULL;
    gchar* dir = g_canonicalize_filename(start, NULL);
    const gchar* home = g_get_home_dir();
    while (dir != NULL) {
        for (guint i = 0; i < G_N_ELEMENTS(MARKERS); i++) {
            if (has_file(dir, MARKERS[i])) return dir;
        }
        gchar* parent = g_path_get_dirname(dir);
        gboolean top = g_strcmp0(parent, dir) == 0 || g_strcmp0(dir, home) == 0;
        g_free(dir);
        dir = NULL;
        if (!top) dir = parent;
        else g_free(parent);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Makefile                                                                  */
/* ------------------------------------------------------------------------- */

/* A target a person runs: a plain name, not a file nor a pattern */
static gboolean plain_target(const gchar* t) {
    if (t[0] == '\0' || t[0] == '.' || t[0] == '-') return FALSE;
    for (const gchar* c = t; *c != '\0'; c++) {
        if (!g_ascii_isalnum(*c) && *c != '_' && *c != '-') return FALSE;
    }
    return TRUE;
}

static void add_unique(GPtrArray* list, const gchar* name) {
    for (guint i = 0; i < list->len; i++) {
        if (g_strcmp0(g_ptr_array_index(list, i), name) == 0) return;
    }
    if (list->len < MAX_MAKE_TARGETS) g_ptr_array_add(list, g_strdup(name));
}

GPtrArray* task_detect_make_targets(const gchar* makefile) {
    GPtrArray* rules = g_ptr_array_new_with_free_func(g_free);
    GPtrArray* phony = g_ptr_array_new_with_free_func(g_free);
    gchar** lines = g_strsplit(makefile != NULL ? makefile : "", "\n", -1);
    for (gchar** l = lines; *l != NULL; l++) {
        const gchar* line = *l;
        if (line[0] == '\t' || line[0] == ' ' || line[0] == '#') continue;
        const gchar* colon = strchr(line, ':');
        if (colon == NULL || colon[1] == '=' || colon[1] == ':') continue;
        gchar* head = g_strndup(line, (gsize)(colon - line));
        if (strchr(head, '=') != NULL || strchr(head, '$') != NULL) {
            g_free(head);
            continue;
        }
        gboolean is_phony = g_strcmp0(g_strstrip(head), ".PHONY") == 0;
        gchar* rest = is_phony ? g_strdup(colon + 1) : NULL;
        if (rest != NULL) {
            gchar* hash = strchr(rest, '#');
            if (hash != NULL) *hash = '\0';
        }
        gchar** names = g_strsplit_set(is_phony ? rest : head, " \t", -1);
        for (gchar** n = names; *n != NULL; n++) {
            if (plain_target(*n)) add_unique(is_phony ? phony : rules, *n);
        }
        g_strfreev(names);
        g_free(rest);
        g_free(head);
    }
    g_strfreev(lines);
    if (phony->len == 0) {
        g_ptr_array_unref(phony);
        return rules;
    }
    /* The .PHONY targets that have a rule, in the order of the rules */
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < rules->len; i++) {
        const gchar* r = g_ptr_array_index(rules, i);
        for (guint j = 0; j < phony->len; j++) {
            if (g_strcmp0(g_ptr_array_index(phony, j), r) == 0) add_unique(out, r);
        }
    }
    g_ptr_array_unref(rules);
    g_ptr_array_unref(phony);
    return out;
}

/* ------------------------------------------------------------------------- */
/* package.json                                                              */
/* ------------------------------------------------------------------------- */

static const gchar* skip_space(const gchar* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/* The JSON string at *s (after its opening quote is *s), *s moved past it;
 * NULL when malformed */
static gchar* read_string(const gchar** s) {
    const gchar* c = *s;
    if (*c != '"') return NULL;
    c++;
    GString* out = g_string_new(NULL);
    while (*c != '\0' && *c != '"') {
        if (*c == '\\' && c[1] != '\0') {
            c++;
            switch (*c) {
            case 'n': g_string_append_c(out, '\n'); break;
            case 't': g_string_append_c(out, '\t'); break;
            case 'u': g_string_append_c(out, '?'); c += MIN(strlen(c) - 1, 4); break;
            default: g_string_append_c(out, *c); break;
            }
            c++;
            continue;
        }
        g_string_append_c(out, *c++);
    }
    if (*c != '"') {
        g_string_free(out, TRUE);
        return NULL;
    }
    *s = c + 1;
    return g_string_free(out, FALSE);
}

GPtrArray* task_detect_npm_scripts(const gchar* package_json) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    const gchar* s = package_json != NULL ? strstr(package_json, "\"scripts\"") : NULL;
    if (s == NULL) return out;
    s = skip_space(s + strlen("\"scripts\""));
    if (*s != ':') return out;
    s = skip_space(s + 1);
    if (*s != '{') return out;
    s = skip_space(s + 1);
    while (*s == '"') {
        gchar* key = read_string(&s);
        if (key == NULL) break;
        s = skip_space(s);
        gchar* value = *s == ':' ? (s = skip_space(s + 1), read_string(&s)) : NULL;
        if (value == NULL) {
            g_free(key);
            break;
        }
        g_ptr_array_add(out, key);
        g_free(value);
        s = skip_space(s);
        if (*s != ',') break;
        s = skip_space(s + 1);
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------- */

static void add_task(GPtrArray* tasks, const gchar* dir, const gchar* label, const gchar* first, ...) {
    GPtrArray* argv = g_ptr_array_new();
    va_list ap;
    va_start(ap, first);
    for (const gchar* a = first; a != NULL; a = va_arg(ap, const gchar*)) g_ptr_array_add(argv, (gpointer)a);
    va_end(ap);
    g_ptr_array_add(argv, NULL);
    g_ptr_array_add(tasks, task_def_new(label, (const gchar* const*)argv->pdata, dir));
    g_ptr_array_unref(argv);
}

static void add_make(GPtrArray* tasks, const gchar* dir) {
    const gchar* file = has_file(dir, "GNUmakefile") ? "GNUmakefile"
                        : has_file(dir, "makefile") ? "makefile"
                        : has_file(dir, "Makefile") ? "Makefile"
                                                    : NULL;
    if (file == NULL) return;
    add_task(tasks, dir, "make", "make", NULL);
    gchar* text = read_file(dir, file);
    GPtrArray* targets = task_detect_make_targets(text);
    for (guint i = 0; i < targets->len; i++) {
        const gchar* t = g_ptr_array_index(targets, i);
        gchar* label = g_strconcat("make ", t, NULL);
        add_task(tasks, dir, label, "make", t, NULL);
        g_free(label);
    }
    g_ptr_array_unref(targets);
    g_free(text);
}

/* The folder of the Cargo workspace of the crate in dir (dir itself when it
 * is no member), not above the home folder */
static gchar* cargo_root(const gchar* dir) {
    const gchar* home = g_get_home_dir();
    gchar* cur = g_strdup(dir);
    while (TRUE) {
        gchar* text = read_file(cur, "Cargo.toml");
        gboolean workspace = text != NULL && strstr(text, "[workspace]") != NULL;
        g_free(text);
        if (workspace) return cur;
        gchar* parent = g_path_get_dirname(cur);
        gboolean top = g_strcmp0(parent, cur) == 0 || g_strcmp0(cur, home) == 0;
        g_free(cur);
        if (top) {
            g_free(parent);
            return g_strdup(dir);
        }
        cur = parent;
    }
}

static void add_cargo(GPtrArray* tasks, const gchar* dir) {
    if (!has_file(dir, "Cargo.toml")) return;
    gchar* root = cargo_root(dir);
    static const gchar* const verbs[] = {"build", "check", "test", "clippy", "run"};
    for (guint i = 0; i < G_N_ELEMENTS(verbs); i++) {
        gchar* label = g_strconcat("cargo ", verbs[i], NULL);
        add_task(tasks, root, label, "cargo", verbs[i], NULL);
        g_free(label);
    }
    g_free(root);
}

static void add_npm(GPtrArray* tasks, const gchar* dir) {
    if (!has_file(dir, "package.json")) return;
    const gchar* runner = has_file(dir, "pnpm-lock.yaml")                        ? "pnpm"
                          : has_file(dir, "yarn.lock")                           ? "yarn"
                          : has_file(dir, "bun.lockb") || has_file(dir, "bun.lock") ? "bun"
                                                                                  : "npm";
    gchar* text = read_file(dir, "package.json");
    GPtrArray* scripts = task_detect_npm_scripts(text);
    /* The build script first: the default */
    for (gint pass = 0; pass < 2; pass++) {
        for (guint i = 0; i < scripts->len; i++) {
            const gchar* s = g_ptr_array_index(scripts, i);
            if ((g_strcmp0(s, "build") == 0) != (pass == 0)) continue;
            gchar* label = g_strdup_printf("%s run %s", runner, s);
            add_task(tasks, dir, label, runner, "run", s, NULL);
            g_free(label);
        }
    }
    g_ptr_array_unref(scripts);
    g_free(text);
}

static void add_go(GPtrArray* tasks, const gchar* dir) {
    if (!has_file(dir, "go.mod")) return;
    add_task(tasks, dir, "go build ./...", "go", "build", "./...", NULL);
    add_task(tasks, dir, "go test ./...", "go", "test", "./...", NULL);
    add_task(tasks, dir, "go vet ./...", "go", "vet", "./...", NULL);
}

/* The first of the build folders names under dir holding file, or NULL */
static const gchar* build_dir_with(const gchar* dir, const gchar* const* names, const gchar* file) {
    for (const gchar* const* n = names; *n != NULL; n++) {
        gchar* sub = g_build_filename(dir, *n, NULL);
        gboolean found = has_file(sub, file);
        g_free(sub);
        if (found) return *n;
    }
    return NULL;
}

static void add_cmake_meson_ninja(GPtrArray* tasks, const gchar* dir) {
    if (has_file(dir, "CMakeLists.txt")) {
        static const gchar* const names[] = {"build", "cmake-build-debug", "cmake-build-release", "out/build", NULL};
        const gchar* b = build_dir_with(dir, names, "CMakeCache.txt");
        gchar* label = b != NULL ? g_strdup_printf("cmake --build %s", b) : g_strdup("cmake -B build");
        if (b != NULL) add_task(tasks, dir, label, "cmake", "--build", b, NULL);
        else add_task(tasks, dir, label, "cmake", "-B", "build", NULL);
        g_free(label);
    }
    if (has_file(dir, "meson.build")) {
        static const gchar* const names[] = {"build", "builddir", "_build", NULL};
        const gchar* b = build_dir_with(dir, names, "build.ninja");
        if (b != NULL) {
            gchar* label = g_strdup_printf("ninja -C %s", b);
            add_task(tasks, dir, label, "ninja", "-C", b, NULL);
            g_free(label);
            label = g_strdup_printf("meson test -C %s", b);
            add_task(tasks, dir, label, "meson", "test", "-C", b, NULL);
            g_free(label);
        } else {
            add_task(tasks, dir, "meson setup build", "meson", "setup", "build", NULL);
        }
    } else if (has_file(dir, "build.ninja")) {
        add_task(tasks, dir, "ninja", "ninja", NULL);
    }
}

static void add_python(GPtrArray* tasks, const gchar* dir) {
    if (!has_file(dir, "pyproject.toml") && !has_file(dir, "setup.py") && !has_file(dir, "pytest.ini")) return;
#ifdef G_OS_WIN32
    add_task(tasks, dir, "pytest", "python", "-m", "pytest", NULL);
#else
    add_task(tasks, dir, "pytest", "python3", "-m", "pytest", NULL);
#endif
}

GPtrArray* task_detect(const gchar* dir) {
    GPtrArray* tasks = g_ptr_array_new_with_free_func((GDestroyNotify)task_def_free);
    if (dir == NULL) return tasks;
    add_make(tasks, dir);
    add_cargo(tasks, dir);
    add_cmake_meson_ninja(tasks, dir);
    add_npm(tasks, dir);
    add_go(tasks, dir);
    add_python(tasks, dir);
    return tasks;
}

/* The LaTeX tasks of a .tex file (see task_latex.h) */
#include "tasks/task_latex.h"
#include "core/vfs.h"
#include "latex/latex_root.h"
#include <string.h>

/* Candidate main documents read at most, and their size */
#define ROOT_CANDIDATES 64
#define ROOT_MAX_BYTES (1024 * 1024)

static gchar* read_text(const gchar* path) {
    gchar* text = NULL;
    gsize len = 0;
    if (!vfs_file_get_contents(path, ROOT_MAX_BYTES, &text, &len, NULL)) return NULL;
    return text;
}

/* The .tex files of dir, sorted */
static GPtrArray* tex_files(const gchar* dir) {
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    if (vfs_is_remote(dir)) {
        GPtrArray* entries = vfs_list(dir, NULL, NULL);
        for (guint i = 0; entries != NULL && i < entries->len; i++) {
            const VfsEntry* e = g_ptr_array_index(entries, i);
            if (!e->is_dir && g_str_has_suffix(e->name, ".tex")) g_ptr_array_add(names, g_strdup(e->name));
        }
        if (entries != NULL) g_ptr_array_unref(entries);
    } else {
        GDir* d = g_dir_open(dir, 0, NULL);
        const gchar* e;
        while (d != NULL && (e = g_dir_read_name(d)) != NULL) {
            if (g_str_has_suffix(e, ".tex")) g_ptr_array_add(names, g_strdup(e));
        }
        if (d != NULL) g_dir_close(d);
    }
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    return names;
}

/* The main documents of dir (prefix: how dir is written from the saved
 * file's directory, "" or "../"), excluding skip; stem: the saved file seen
 * from dir */
static void add_candidates(const gchar* dir, const gchar* prefix, const gchar* skip, const gchar* stem,
                           GPtrArray* names, GPtrArray* texts, GPtrArray* stems) {
    GPtrArray* entries = tex_files(dir);
    for (guint i = 0; i < entries->len && names->len < ROOT_CANDIDATES; i++) {
        gchar* path = g_build_filename(dir, g_ptr_array_index(entries, i), NULL);
        gchar* text = g_strcmp0(path, skip) != 0 ? read_text(path) : NULL;
        if (text != NULL && latex_root_is_main(text)) {
            g_ptr_array_add(names, g_strconcat(prefix, g_ptr_array_index(entries, i), NULL));
            g_ptr_array_add(texts, text);
            g_ptr_array_add(stems, g_strdup(stem));
        } else {
            g_free(text);
        }
        g_free(path);
    }
    g_ptr_array_unref(entries);
}

gchar* task_latex_root(const gchar* path) {
    if (path == NULL || !g_str_has_suffix(path, ".tex")) return NULL;
    gchar* own = read_text(path);
    if (own == NULL) return NULL;
    gchar* dir = g_path_get_dirname(path);
    gchar* root = NULL;
    gchar* magic = latex_root_magic(own);
    if (magic != NULL) {
        root = g_canonicalize_filename(magic, dir);
        if (!vfs_file_test(root, G_FILE_TEST_IS_REGULAR) && !g_str_has_suffix(root, ".tex")) {
            gchar* with_ext = g_strconcat(root, ".tex", NULL);
            g_free(root);
            root = with_ext;
        }
        g_free(magic);
        if (!vfs_file_test(root, G_FILE_TEST_IS_REGULAR)) g_clear_pointer(&root, g_free);
    } else if (latex_root_is_main(own)) {
        root = g_strdup(path);
    } else {
        /* A chapter: the document of its folder or of the one above that
         * includes it */
        gchar* base = g_path_get_basename(path);
        base[strlen(base) - strlen(".tex")] = '\0';
        gchar* sub = g_path_get_basename(dir);
        gchar* stem_up = g_build_filename(sub, base, NULL);
        gchar* parent = g_path_get_dirname(dir);
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        GPtrArray* texts = g_ptr_array_new_with_free_func(g_free);
        GPtrArray* stems = g_ptr_array_new_with_free_func(g_free);
        add_candidates(dir, "", path, base, names, texts, stems);
        gchar* mount = vfs_mount_of(dir);
        /* Not above the folder a remote machine is seen at */
        if (names->len == 0 && g_strcmp0(parent, dir) != 0 && g_strcmp0(dir, mount) != 0) {
            add_candidates(parent, "../", path, stem_up, names, texts, stems);
        }
        g_free(mount);
        gint i = latex_root_choose((const gchar* const*)names->pdata, (const gchar* const*)texts->pdata,
                                   (const gchar* const*)stems->pdata, names->len);
        if (i >= 0) root = g_canonicalize_filename(g_ptr_array_index(names, i), dir);
        g_ptr_array_unref(stems);
        g_ptr_array_unref(texts);
        g_ptr_array_unref(names);
        g_free(parent);
        g_free(stem_up);
        g_free(sub);
        g_free(base);
    }
    g_free(dir);
    g_free(own);
    return root;
}

gchar* task_latex_output(const gchar* root, const gchar* ext) {
    gsize n = strlen(root);
    if (g_str_has_suffix(root, ".tex")) n -= strlen(".tex");
    gchar* stem = g_strndup(root, n);
    gchar* out = g_strconcat(stem, ext, NULL);
    g_free(stem);
    return out;
}

/* Add the task argv (NULL-terminated, the name of root last) */
static void add_task(GPtrArray* tasks, const gchar* dir, const gchar* name, GPtrArray* argv, const gchar* root) {
    g_ptr_array_add(argv, (gpointer)name);
    g_ptr_array_add(argv, NULL);
    gchar* program = g_path_get_basename(g_ptr_array_index(argv, 0));
    gchar* label = g_strdup_printf("%s %s", program, name);
    if (root == NULL) {
        /* latexmk -C main.tex */
        g_free(label);
        label = g_strdup_printf("%s %s %s", program, (const gchar*)g_ptr_array_index(argv, 1), name);
    }
    TaskDef* def = task_def_new(label, (const gchar* const*)argv->pdata, dir);
    def->tex_root = g_strdup(root);
    g_ptr_array_add(tasks, def);
    g_free(label);
    g_free(program);
}

GPtrArray* task_latex_tasks(const gchar* root) {
    GPtrArray* tasks = g_ptr_array_new_with_free_func((GDestroyNotify)task_def_free);
    if (root == NULL) return tasks;
    gchar* dir = g_path_get_dirname(root);
    gchar* name = g_path_get_basename(root);
    gchar* text = read_text(root);
    LatexEngine engine = latex_root_engine(text != NULL ? text : "");
    g_free(text);
    /* On a remote machine latexmk is run there (TeX Live has it) */
    gboolean remote = vfs_is_remote(root);
    gchar* latexmk = remote ? g_strdup("latexmk") : g_find_program_in_path("latexmk");
    gchar* tectonic = latexmk == NULL ? g_find_program_in_path("tectonic") : NULL;
    gchar* tex = latexmk == NULL && tectonic == NULL ? g_find_program_in_path(latex_root_engine_name(engine)) : NULL;
    GPtrArray* argv = g_ptr_array_new();
    if (latexmk != NULL) {
        gchar* base = g_path_get_basename(latexmk);
        g_ptr_array_add(argv, base);
        /* A latexmkrc of the project chooses the engine itself */
        gchar* rc = g_build_filename(dir, "latexmkrc", NULL);
        gchar* dot_rc = g_build_filename(dir, ".latexmkrc", NULL);
        if (!vfs_file_test(rc, G_FILE_TEST_EXISTS) && !vfs_file_test(dot_rc, G_FILE_TEST_EXISTS)) {
            g_ptr_array_add(argv, engine == LATEX_ENGINE_XELATEX    ? "-pdfxe"
                                  : engine == LATEX_ENGINE_LUALATEX ? "-pdflua"
                                                                    : "-pdf");
        }
        g_free(dot_rc);
        g_free(rc);
        g_ptr_array_add(argv, "-interaction=nonstopmode");
        g_ptr_array_add(argv, "-file-line-error");
        g_ptr_array_add(argv, "-synctex=1");
        add_task(tasks, dir, name, argv, root);
        g_ptr_array_set_size(argv, 0);
        g_ptr_array_add(argv, base);
        g_ptr_array_add(argv, "-C");
        add_task(tasks, dir, name, argv, NULL);
        g_free(base);
    } else if (tectonic != NULL) {
        g_ptr_array_add(argv, "tectonic");
        g_ptr_array_add(argv, "--keep-logs");
        g_ptr_array_add(argv, "--synctex");
        add_task(tasks, dir, name, argv, root);
    } else if (tex != NULL) {
        g_ptr_array_add(argv, (gpointer)latex_root_engine_name(engine));
        g_ptr_array_add(argv, "-interaction=nonstopmode");
        g_ptr_array_add(argv, "-file-line-error");
        g_ptr_array_add(argv, "-synctex=1");
        add_task(tasks, dir, name, argv, root);
    }
    g_ptr_array_unref(argv);
    g_free(tex);
    g_free(tectonic);
    g_free(latexmk);
    g_free(name);
    g_free(dir);
    return tasks;
}

/* Preview benchmark and coverage report (no display needed).
 *
 *   preview_tool bench DIR...     time the structured preview of every file
 *                                 (the viewer's worker-thread part) and fail
 *                                 when one exceeds its budget (docs/previews.md rule 1)
 *   preview_tool coverage DIR...  how many files end at each fidelity level,
 *                                 and which extensions still end at the hex
 *                                 dump (L0)
 *
 * Built by `make bench-preview` / `make preview-coverage`. */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "file_types.h"
#include "preview_formats.h"

/* Same text limit as the viewer reads for files up to PREVIEW_THRESHOLD */
#define TEXT_LIMIT (512 * 1024)

/* Budgets in milliseconds (docs/previews.md): text formats, binary / rendered formats */
#define BUDGET_TEXT_MS 50.0
#define BUDGET_BINARY_MS 150.0

/* Files of the directories, recursively (hidden entries skipped) */
static void collect(const gchar* path, GPtrArray* files, guint max) {
    if (files->len >= max) {
        return;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_ptr_array_add(files, g_strdup(path));
        }
        return;
    }
    GDir* dir = g_dir_open(path, 0, NULL);
    if (dir == NULL) {
        return;
    }
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] != '.') {
            g_ptr_array_add(names, g_strdup(name));
        }
    }
    g_dir_close(dir);
    for (guint i = 0; i < names->len; i++) {
        gchar* child = g_build_filename(path, g_ptr_array_index(names, i), NULL);
        if (!g_file_test(child, G_FILE_TEST_IS_SYMLINK)) {
            collect(child, files, max);
        }
        g_free(child);
    }
    g_ptr_array_free(names, TRUE);
}

static gint compare_double(gconstpointer a, gconstpointer b) {
    gdouble x = *(const gdouble*)a;
    gdouble y = *(const gdouble*)b;
    return (x > y) - (x < y);
}

static int run_bench(GPtrArray* files) {
    GArray* times[FILE_KIND_LAST + 1];
    for (gint k = 0; k <= FILE_KIND_LAST; k++) {
        times[k] = g_array_new(FALSE, FALSE, sizeof(gdouble));
    }
    int status = 0;
    printf("%-14s %9s  %s\n", "kind", "ms", "file");
    for (guint i = 0; i < files->len; i++) {
        const gchar* path = g_ptr_array_index(files, i);
        gchar* name = g_path_get_basename(path);
        FileKind kind = file_kind_of_path(path, name);
        if (!preview_format_has_builder(kind)) {
            g_free(name);
            continue;
        }
        GError* error = NULL;
        gint64 t0 = g_get_monotonic_time();
        PreviewDoc* doc = preview_format_build_file(path, name, TEXT_LIMIT, NULL, &kind, &error);
        gdouble ms = (gdouble)(g_get_monotonic_time() - t0) / 1000.0;
        gdouble budget = file_kind_is_text(kind) ? BUDGET_TEXT_MS : BUDGET_BINARY_MS;
        g_array_append_val(times[kind], ms);
        printf("%-14s %9.1f  %s%s%s%s\n", file_kind_label(kind), ms, path,
               doc == NULL ? "  (failed: " : "", doc == NULL ? error->message : "",
               doc == NULL ? ")" : "");
        if (ms > budget) {
            printf("  OVER BUDGET: %.1f ms > %.0f ms\n", ms, budget);
            status = 1;
        }
        g_clear_error(&error);
        preview_doc_free(doc);
        g_free(name);
    }
    printf("\n%-14s %6s %9s %9s\n", "kind", "files", "p95 ms", "max ms");
    for (gint k = 0; k <= FILE_KIND_LAST; k++) {
        GArray* t = times[k];
        if (t->len > 0) {
            g_array_sort(t, compare_double);
            guint p95 = (guint)((t->len - 1) * 0.95);
            printf("%-14s %6u %9.1f %9.1f\n", file_kind_label((FileKind)k), t->len,
                   g_array_index(t, gdouble, p95), g_array_index(t, gdouble, t->len - 1));
        }
        g_array_free(t, TRUE);
    }
    printf("\nbench-preview: %s\n", status == 0 ? "OK" : "FAIL (over budget)");
    return status;
}

/* Fidelity level a kind reaches today (see docs/previews.md, "Fidelity ladder") */
static gint kind_level(FileKind kind, gboolean text) {
    if (kind == FILE_KIND_BINARY) {
        return 0;
    }
    if (text || file_kind_is_media(kind) || preview_format_has_builder(kind)) {
        return 1; /* at least a summary or the text itself */
    }
    return 0; /* known binary format without a builder: hex dump */
}

static int run_coverage(GPtrArray* files) {
    guint per_kind[FILE_KIND_LAST + 1] = { 0 };
    guint l0 = 0;
    GHashTable* l0_ext = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < files->len; i++) {
        const gchar* path = g_ptr_array_index(files, i);
        gchar* name = g_path_get_basename(path);
        guchar head[FILE_SNIFF_BYTES];
        gsize len = 0;
        FILE* fp = g_fopen(path, "rb");
        if (fp != NULL) {
            len = fread(head, 1, sizeof(head), fp);
            fclose(fp);
        }
        FileKind kind = file_kind_detect(name, head, len);
        per_kind[kind]++;
        if (kind_level(kind, file_content_is_text(head, len)) == 0) {
            l0++;
            const gchar* ext = file_extension(name);
            gchar* key = g_ascii_strdown(ext[0] != '\0' ? ext : "(none)", -1);
            guint n = GPOINTER_TO_UINT(g_hash_table_lookup(l0_ext, key));
            g_hash_table_insert(l0_ext, key, GUINT_TO_POINTER(n + 1));
        }
        g_free(name);
    }
    printf("%-14s %8s\n", "kind", "files");
    for (gint k = 0; k <= FILE_KIND_LAST; k++) {
        if (per_kind[k] > 0) {
            const gchar* label = file_kind_label((FileKind)k);
            printf("%-14s %8u%s\n", label != NULL ? label : "Text", per_kind[k],
                   kind_level((FileKind)k, file_kind_is_text((FileKind)k)) == 0 ? "  (no builder yet)" : "");
        }
    }
    printf("\nL0 (hex dump): %u of %u files (%.2f %%)\n", l0, files->len,
           files->len > 0 ? 100.0 * l0 / files->len : 0.0);
    if (l0 > 0) {
        printf("Most common extensions at L0:\n");
        GList* keys = g_hash_table_get_keys(l0_ext);
        for (guint shown = 0; shown < 15 && keys != NULL; shown++) {
            GList* best = keys;
            for (GList* l = keys; l != NULL; l = l->next) {
                if (GPOINTER_TO_UINT(g_hash_table_lookup(l0_ext, l->data)) >
                    GPOINTER_TO_UINT(g_hash_table_lookup(l0_ext, best->data))) {
                    best = l;
                }
            }
            printf("  %-12s %6u\n", (const gchar*)best->data,
                   GPOINTER_TO_UINT(g_hash_table_lookup(l0_ext, best->data)));
            keys = g_list_delete_link(keys, best);
        }
        g_list_free(keys);
    }
    g_hash_table_destroy(l0_ext);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || (strcmp(argv[1], "bench") != 0 && strcmp(argv[1], "coverage") != 0)) {
        fprintf(stderr, "usage: %s bench|coverage DIR...\n", argv[0]);
        return 2;
    }
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    for (int i = 2; i < argc; i++) {
        collect(argv[i], files, 200000);
    }
    int status = strcmp(argv[1], "bench") == 0 ? run_bench(files) : run_coverage(files);
    g_ptr_array_free(files, TRUE);
    return status;
}

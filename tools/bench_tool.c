/* Explorer benchmark (make bench-scan, docs/performance.md; no display needed).
 *
 *   bench_tool scan DIR [N]   time the listing of DIR (made with N entries,
 *                             default 100000, when it does not hold them)
 *                             in each sort order, then the rows the explorer
 *                             makes of it and the memory they take; fails
 *                             when a result exceeds its budget.
 *
 *   bench_tool slides         time the preview of a presentation full of
 *                             photos (slide_render): 20 slides, each with a
 *                             3000 x 2000 photo over the same 1920 x 1080
 *                             background picture, like a .ppt of a few MB.
 *
 * Budgets (environment, to tighten or loosen): BENCH_SCAN_MS (listing sorted
 * by name), BENCH_ROWS_MS (rows of the listing), BENCH_ROW_BYTES (heap per
 * row, listing included), BENCH_SLIDES_MS (preview of the presentation). */
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "core/profile.h"
#include "explorer/dir_scan.h"
#include "explorer/explorer_row.h"
#include "office/slide_render.h"
#include "preview/preview_doc.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#define DEFAULT_ENTRIES 100000
#define RUNS 3

/* Default budgets for 100 000 entries */
#define BUDGET_SCAN_MS 300.0
#define BUDGET_ROWS_MS 100.0
#define BUDGET_ROW_BYTES 700.0
#define BUDGET_SLIDES_MS 700.0

static const gchar* const exts[] = {".c", ".h", ".txt", ".md", ".png", ".json", "", ".tar.gz"};

/* Bytes allocated on the heap (glibc), else the resident memory */
static gint64 heap_bytes(void) {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    struct mallinfo2 mi = mallinfo2();
    return (gint64)mi.uordblks + (gint64)mi.hblkhd;
#else
    gint64 rss = -1, peak = -1;
    profile_memory(&rss, &peak);
    return rss * 1024;
#endif
}

static gdouble budget(const gchar* var, gdouble fallback) {
    const gchar* v = g_getenv(var);
    return (v != NULL && v[0] != '\0') ? g_ascii_strtod(v, NULL) : fallback;
}

/* Entries of dir, without reading them all twice: the count of names */
static guint count_entries(const gchar* dir) {
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d == NULL) {
        return 0;
    }
    guint n = 0;
    while (g_dir_read_name(d) != NULL) {
        n++;
    }
    g_dir_close(d);
    return n;
}

/* dir with n entries: one folder in 50, files of mixed extensions and sizes */
static gboolean make_corpus(const gchar* dir, guint n) {
    if (count_entries(dir) == n) {
        return TRUE;
    }
    printf("bench: making %u entries in %s\n", n, dir);
    g_mkdir_with_parents(dir, 0755);
    for (guint i = 0; i < n; i++) {
        gchar* path;
        if (i % 50 == 0) {
            path = g_strdup_printf("%s/Folder %06u", dir, i);
            g_mkdir(path, 0755);
        } else {
            path = g_strdup_printf("%s/file_%06u%s", dir, (i * 7919u) % n, exts[i % G_N_ELEMENTS(exts)]);
            FILE* f = fopen(path, "w");
            if (f == NULL) {
                g_free(path);
                return FALSE;
            }
            for (guint k = 0; k < i % 7; k++) {
                fputc('x', f);
            }
            fclose(f);
        }
        g_free(path);
    }
    return count_entries(dir) >= n;
}

static gint compare_double(gconstpointer a, gconstpointer b) {
    gdouble x = *(const gdouble*)a, y = *(const gdouble*)b;
    return (x > y) - (x < y);
}

/* Median of RUNS listings of dir in sort order, in ms */
static gdouble time_scan(const gchar* dir, DirScanSort sort, guint* entries) {
    gdouble t[RUNS];
    for (guint r = 0; r < RUNS; r++) {
        gint64 start = g_get_monotonic_time();
        DirScanNode* node = dir_scan_read(dir, FALSE, sort, NULL, NULL);
        t[r] = (g_get_monotonic_time() - start) / 1000.0;
        *entries = (node != NULL && node->entries != NULL) ? node->entries->len : 0;
        dir_scan_node_free(node);
    }
    qsort(t, RUNS, sizeof(gdouble), compare_double);
    return t[RUNS / 2];
}

/* The rows file_list.c makes of a listing (make_entry_row(), without git) */
static void make_rows(ExplorerRow* root, const DirScanNode* node) {
    GPtrArray* rows = g_ptr_array_new_full(node->entries->len, g_object_unref);
    for (guint i = 0; i < node->entries->len; i++) {
        const DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
        gchar* full = g_build_filename(node->dirname, e->name, NULL);
        ExplorerRow* row = explorer_row_new_child(root, e->is_dir, e->is_dir ? "dir" : "file",
                                                  e->name, e->name, full);
        if (e->is_dir) {
            explorer_row_set_glyph(row, "folder");
            explorer_row_set_loaded(row, FALSE);
            explorer_row_append(row, "-", "", "...", "");
        } else {
            explorer_row_set_icon_name(row, e->name);
        }
        explorer_row_set_badge(row, "");
        g_ptr_array_add(rows, row);
        g_free(full);
    }
    explorer_row_insert_rows(root, G_MAXUINT, rows);
    g_ptr_array_unref(rows);
}

static int bench_scan(const gchar* dir, guint n) {
    if (!make_corpus(dir, n)) {
        fprintf(stderr, "bench: cannot make %s\n", dir);
        return 1;
    }
    static const struct {
        DirScanSort sort;
        const gchar* name;
    } sorts[] = {
        {DIR_SCAN_SORT_NAME, "name"},
        {DIR_SCAN_SORT_DATE, "date"},
        {DIR_SCAN_SORT_SIZE, "size"},
        {DIR_SCAN_SORT_EXT, "extension"},
    };
    guint entries = 0;
    gdouble scan_name_ms = 0;
    for (guint s = 0; s < G_N_ELEMENTS(sorts); s++) {
        gdouble t = time_scan(dir, sorts[s].sort, &entries);
        if (s == 0) {
            scan_name_ms = t;
        }
        printf("  scan, by %-9s %8.1f ms  (%u entries, median of %d)\n", sorts[s].name, t, entries, RUNS);
    }

    gint64 heap0 = heap_bytes();
    DirScanNode* node = dir_scan_read(dir, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    gint64 heap_listing = heap_bytes();
    ExplorerRow* root = explorer_row_new_root();
    gint64 start = g_get_monotonic_time();
    make_rows(root, node);
    gdouble rows_ms = (g_get_monotonic_time() - start) / 1000.0;
    gint64 heap_rows = heap_bytes();
    guint count = node->entries->len;
    gdouble listing_bytes = count > 0 ? (gdouble)(heap_listing - heap0) / count : 0;
    gdouble row_bytes = count > 0 ? (gdouble)(heap_rows - heap_listing) / count : 0;
    printf("  rows               %8.1f ms  (%u rows)\n", rows_ms, count);
    printf("  memory per entry   %8.0f B    (listing %.0f B + row %.0f B)\n",
           listing_bytes + row_bytes, listing_bytes, row_bytes);
    start = g_get_monotonic_time();
    g_object_unref(root);
    dir_scan_node_free(node);
    printf("  free               %8.1f ms\n", (g_get_monotonic_time() - start) / 1000.0);

    /* Budgets scale with the number of entries */
    gdouble scale = (gdouble)count / DEFAULT_ENTRIES;
    gdouble max_scan = budget("BENCH_SCAN_MS", BUDGET_SCAN_MS) * scale;
    gdouble max_rows = budget("BENCH_ROWS_MS", BUDGET_ROWS_MS) * scale;
    gdouble max_bytes = budget("BENCH_ROW_BYTES", BUDGET_ROW_BYTES);
    int status = 0;
    if (scan_name_ms > max_scan) {
        printf("bench: FAIL - scan %.1f ms > budget %.1f ms\n", scan_name_ms, max_scan);
        status = 1;
    }
    if (rows_ms > max_rows) {
        printf("bench: FAIL - rows %.1f ms > budget %.1f ms\n", rows_ms, max_rows);
        status = 1;
    }
    if (listing_bytes + row_bytes > max_bytes) {
        printf("bench: FAIL - %.0f B per entry > budget %.0f B\n", listing_bytes + row_bytes, max_bytes);
        status = 1;
    }
    if (status == 0) {
        printf("bench-scan: OK (budgets: scan %.0f ms, rows %.0f ms, %.0f B per entry)\n",
               max_scan, max_rows, max_bytes);
    }
    return status;
}

/* A JPEG of w x h with a gradient and noise (seed varies it), as a photo */
static GBytes* photo_jpeg(gint w, gint h, guint seed) {
    GdkPixbuf* pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    guchar* px = gdk_pixbuf_get_pixels(pix);
    gint stride = gdk_pixbuf_get_rowstride(pix);
    GRand* rnd = g_rand_new_with_seed(seed);
    for (gint y = 0; y < h; y++) {
        for (gint x = 0; x < w; x++) {
            guchar* p = px + (gsize)y * stride + x * 3;
            gint n = g_rand_int_range(rnd, -24, 24);
            p[0] = (guchar)CLAMP(x * 255 / w + n, 0, 255);
            p[1] = (guchar)CLAMP(y * 255 / h + n, 0, 255);
            p[2] = (guchar)CLAMP((gint)(seed * 37 % 256) + n, 0, 255);
        }
    }
    g_rand_free(rnd);
    gchar* buf = NULL;
    gsize len = 0;
    gdk_pixbuf_save_to_buffer(pix, &buf, &len, "jpeg", NULL, "quality", "85", NULL);
    g_object_unref(pix);
    return g_bytes_new_take(buf, len);
}

static gdouble since_ms(gint64 start) {
    return (g_get_monotonic_time() - start) / 1000.0;
}

static int bench_slides(void) {
    const guint n = 20;
    GBytes* background = photo_jpeg(1920, 1080, 1);
    SlideDeck* deck = slide_deck_new(720, 405);
    gsize bytes = g_bytes_get_size(background);
    for (guint i = 0; i < n; i++) {
        Slide* slide = slide_new();
        slide->background_picture = g_bytes_ref(background);
        SlideShape* shape = slide_shape_new();
        shape->x = 60;
        shape->y = 60;
        shape->w = 600;
        shape->h = 400 * 600 / 600.0 * 0.66;
        shape->picture = photo_jpeg(3000, 2000, i + 2);
        bytes += g_bytes_get_size(shape->picture);
        g_ptr_array_add(slide->shapes, shape);
        g_ptr_array_add(deck->slides, slide);
    }
    printf("  presentation       %u slides, %.1f MB of pictures\n", n, bytes / 1048576.0);

    /* One picture decoded alone, for scale */
    gint64 start = g_get_monotonic_time();
    const Slide* first = g_ptr_array_index(deck->slides, 0);
    const SlideShape* shape = g_ptr_array_index(first->shapes, 0);
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write_bytes(loader, shape->picture, NULL);
    gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);
    printf("  one photo decoded  %8.1f ms  (3000 x 2000 JPEG)\n", since_ms(start));

    /* BENCH_SLIDES_DUMP=<file>: the first slide as drawn, to compare renders */
    const gchar* dump = g_getenv("BENCH_SLIDES_DUMP");
    if (dump != NULL && dump[0] != '\0') {
        GBytes* png = slide_render_png(deck, first, 960);
        if (png != NULL) {
            g_file_set_contents(dump, g_bytes_get_data(png, NULL), (gssize)g_bytes_get_size(png), NULL);
            g_bytes_unref(png);
        }
    }

    start = g_get_monotonic_time();
    PreviewDoc* doc = slide_deck_preview(deck, "Bench", "", n, 1.0);
    gdouble total = since_ms(start);
    printf("  preview            %8.1f ms  (%.1f ms per slide, %u pictures)\n", total, total / n, doc->images->len);
    preview_doc_free(doc);
    slide_deck_free(deck);
    g_bytes_unref(background);

    gdouble max_ms = budget("BENCH_SLIDES_MS", BUDGET_SLIDES_MS);
    if (total > max_ms) {
        printf("bench: FAIL - presentation preview %.1f ms > budget %.1f ms\n", total, max_ms);
        return 1;
    }
    printf("bench-slides: OK (budget %.0f ms)\n", max_ms);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "scan") == 0) {
        guint n = (argc >= 4) ? (guint)g_ascii_strtoull(argv[3], NULL, 10) : DEFAULT_ENTRIES;
        return bench_scan(argv[2], n > 0 ? n : DEFAULT_ENTRIES);
    }
    if (argc >= 2 && strcmp(argv[1], "slides") == 0) {
        return bench_slides();
    }
    fprintf(stderr, "usage: %s scan DIR [ENTRIES] | slides\n", argv[0]);
    return 2;
}

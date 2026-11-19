/* Statistics of a disk usage scan grouped for display (see disk_stats.h) */
#include "core/disk_stats.h"
#include "core/file_types.h"
#include "core/i18n.h"

/* A kind is a family of file_types.h, in the same order */
G_STATIC_ASSERT((gint)DISK_KIND_DOCUMENTS == (gint)FILE_FAMILY_DOCUMENTS);
G_STATIC_ASSERT((gint)DISK_KIND_PROGRAMS == (gint)FILE_FAMILY_PROGRAMS);
G_STATIC_ASSERT((gint)DISK_KIND_OTHER == (gint)FILE_FAMILY_OTHER);
G_STATIC_ASSERT((gint)DISK_KIND_COUNT == (gint)FILE_FAMILY_COUNT);

DiskKind disk_kind_of_extension(const gchar* ext) {
    if (ext == NULL || ext[0] == '\0') return DISK_KIND_OTHER;
    gchar* name = g_strconcat("x.", ext, NULL);
    FileKind kind = file_kind_from_name(name);
    g_free(name);
    return (DiskKind)file_family_of_kind(kind);
}

const gchar* disk_kind_name(DiskKind kind) {
    return file_family_name((FileFamily)kind);
}

void disk_stats_kinds(const DiskStats* stats, DiskTally out[DISK_KIND_COUNT]) {
    for (gint k = 0; k < DISK_KIND_COUNT; k++) out[k] = (DiskTally){ 0, 0 };
    if (stats == NULL || stats->extensions == NULL) return;
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, stats->extensions);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        const DiskTally* t = value;
        DiskTally* k = &out[disk_kind_of_extension(key)];
        k->size += t->size;
        k->count += t->count;
    }
}

static gint compare_extensions(gconstpointer a, gconstpointer b) {
    const DiskExtension* x = a;
    const DiskExtension* y = b;
    if (x->tally.size != y->tally.size) return x->tally.size > y->tally.size ? -1 : 1;
    return g_strcmp0(x->ext, y->ext);
}

GArray* disk_stats_top_extensions(const DiskStats* stats, guint n) {
    GArray* out = g_array_new(FALSE, FALSE, sizeof(DiskExtension));
    if (stats == NULL || stats->extensions == NULL) return out;
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, stats->extensions);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        DiskExtension e = { key, *(const DiskTally*)value };
        g_array_append_val(out, e);
    }
    g_array_sort(out, compare_extensions);
    if (out->len > n) g_array_set_size(out, n);
    return out;
}

const gchar* disk_age_name(DiskAge age) {
    switch (age) {
    case DISK_AGE_DAY: return _("Less than a day");
    case DISK_AGE_WEEK: return _("Less than a week");
    case DISK_AGE_MONTH: return _("Less than a month");
    case DISK_AGE_YEAR: return _("Less than a year");
    case DISK_AGE_3_YEARS: return _("1 to 3 years");
    case DISK_AGE_OLDER:
    case DISK_AGE_COUNT: break;
    }
    return _("Over 3 years");
}

const gchar* disk_size_name(DiskSize size) {
    switch (size) {
    /* TRANSLATORS: a group of files by size: those of 0 bytes */
    case DISK_SIZE_EMPTY: return _("Empty");
    /* TRANSLATORS: groups of files by size; k, M, G are powers of 1024 */
    case DISK_SIZE_TINY: return _("Under 1k");
    case DISK_SIZE_1K: return _("1k to 10k");
    case DISK_SIZE_10K: return _("10k to 100k");
    case DISK_SIZE_100K: return _("100k to 1M");
    case DISK_SIZE_1M: return _("1M to 10M");
    case DISK_SIZE_10M: return _("10M to 100M");
    case DISK_SIZE_100M: return _("100M to 1G");
    case DISK_SIZE_1G: return _("1G to 10G");
    case DISK_SIZE_HUGE:
    case DISK_SIZE_COUNT: break;
    }
    return _("10G and more");
}

const gchar* disk_size_label(DiskSize size) {
    /* The units are those typed in the size criteria: not translated */
    static const gchar* const labels[DISK_SIZE_COUNT] = { "0",   "<1k", "1k",   "10k", "100k",
                                                          "1M",  "10M", "100M", "1G",  "10G+" };
    return (guint)size < DISK_SIZE_COUNT ? labels[size] : labels[DISK_SIZE_HUGE];
}

gdouble disk_percent(guint64 part, guint64 whole) {
    return whole > 0 ? 100.0 * (gdouble)part / (gdouble)whole : 0.0;
}

gchar* disk_format_percent(guint64 part, guint64 whole) {
    gdouble p = disk_percent(part, whole);
    if (part > 0 && p < 0.1) return g_strdup("< 0.1%");
    return g_strdup_printf("%.1f%%", p);
}

gchar* disk_format_files(guint64 n) {
    gchar* num = g_strdup_printf("%" G_GUINT64_FORMAT, n);
    /* TRANSLATORS: %s is a number of files */
    gchar* text = g_strdup_printf(ngettext("%s file", "%s files", (gulong)MIN(n, G_MAXULONG)), num);
    g_free(num);
    return text;
}

gchar* disk_format_group(const gchar* name, const DiskTally* t, guint64 all_count, guint64 all_size) {
    gchar* files = disk_format_files(t->count);
    gchar* files_pct = disk_format_percent(t->count, all_count);
    gchar* size = g_format_size(t->size);
    gchar* size_pct = disk_format_percent(t->size, all_size);
    gchar* tip = g_strdup_printf("%s\n%s (%s)\n%s (%s)", name, files, files_pct, size, size_pct);
    g_free(size_pct);
    g_free(size);
    g_free(files_pct);
    g_free(files);
    return tip;
}

gchar* disk_format_tally(const DiskTally* t) {
    gchar* size = g_format_size(t->size);
    gchar* count = disk_format_files(t->count);
    /* TRANSLATORS: a size and a number of files: "2.1 GB (124 files)" */
    gchar* text = g_strdup_printf(_("%s (%s)"), size, count);
    g_free(size);
    g_free(count);
    return text;
}

/* Statistics of a disk usage scan grouped for display (see disk_stats.h) */
#include "core/disk_stats.h"
#include "core/file_types.h"
#include "core/i18n.h"

DiskKind disk_kind_of_extension(const gchar* ext) {
    if (ext == NULL || ext[0] == '\0') return DISK_KIND_OTHER;
    gchar* name = g_strconcat("x.", ext, NULL);
    FileKind kind = file_kind_from_name(name);
    g_free(name);
    switch (kind) {
    case FILE_KIND_MARKDOWN:
    case FILE_KIND_HTML:
    case FILE_KIND_ORG:
    case FILE_KIND_PDF:
    case FILE_KIND_DOCUMENT:
    case FILE_KIND_SPREADSHEET:
    case FILE_KIND_PRESENTATION:
    case FILE_KIND_EBOOK:
    case FILE_KIND_NOTEBOOK:
    case FILE_KIND_RTF:
    case FILE_KIND_OLE:
    case FILE_KIND_MAIL:
        return DISK_KIND_DOCUMENTS;
    case FILE_KIND_IMAGE:
        return DISK_KIND_IMAGES;
    case FILE_KIND_VIDEO:
        return DISK_KIND_VIDEOS;
    case FILE_KIND_AUDIO:
        return DISK_KIND_AUDIO;
    case FILE_KIND_ARCHIVE:
        return DISK_KIND_ARCHIVES;
    case FILE_KIND_EXECUTABLE:
    case FILE_KIND_CLASS:
    case FILE_KIND_WASM:
    case FILE_KIND_PYC:
        return DISK_KIND_PROGRAMS;
    case FILE_KIND_TEXT:
    case FILE_KIND_CSV:
    case FILE_KIND_JSON:
    case FILE_KIND_XML:
    case FILE_KIND_TOML:
    case FILE_KIND_YAML:
    case FILE_KIND_DIFF:
    case FILE_KIND_LOG:
    case FILE_KIND_DESKTOP:
    case FILE_KIND_MANPAGE:
    case FILE_KIND_TRANSLATION:
    case FILE_KIND_CALENDAR:
    case FILE_KIND_CONTACT:
    case FILE_KIND_CERTIFICATE:
        return DISK_KIND_CODE;
    default:
        return DISK_KIND_OTHER;
    }
}

const gchar* disk_kind_name(DiskKind kind) {
    switch (kind) {
    case DISK_KIND_DOCUMENTS: return _("Documents");
    case DISK_KIND_IMAGES: return _("Images");
    case DISK_KIND_VIDEOS: return _("Videos");
    case DISK_KIND_AUDIO: return _("Audio");
    case DISK_KIND_ARCHIVES: return _("Archives");
    case DISK_KIND_CODE: return _("Text and code");
    case DISK_KIND_PROGRAMS: return _("Programs");
    case DISK_KIND_OTHER:
    case DISK_KIND_COUNT: break;
    }
    return _("Other");
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

gchar* disk_format_tally(const DiskTally* t) {
    gchar* size = g_format_size(t->size);
    gchar* count = disk_format_files(t->count);
    /* TRANSLATORS: a size and a number of files: "2.1 GB (124 files)" */
    gchar* text = g_strdup_printf(_("%s (%s)"), size, count);
    g_free(size);
    g_free(count);
    return text;
}

/* The tree of a remote machine's folder for a disk usage scan (see disk_remote.h) */
#include "core/disk_remote.h"

#include <string.h>
#include <sys/stat.h>

struct DiskRemoteListing {
    GHashTable* dirs; /* folder rel -> (name -> GStatBuf*) */
    GStatBuf root;
    gboolean has_root;
};

gchar* disk_remote_command(gboolean one_filesystem) {
    return g_strdup_printf("find \"$PWD\" -maxdepth 0 -printf '%%y %%s %%b %%T@ %%D %%i %%n .\\0' 2>/dev/null && "
                           "find \"$PWD\" -mindepth 1%s -printf '%%y %%s %%b %%T@ %%D %%i %%n %%P\\0' 2>/dev/null; exit 0",
                           one_filesystem ? " -xdev" : "");
}

DiskRemoteListing* disk_remote_listing_new(void) {
    DiskRemoteListing* l = g_new0(DiskRemoteListing, 1);
    l->dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
    return l;
}

void disk_remote_listing_free(DiskRemoteListing* l) {
    if (l == NULL) return;
    g_hash_table_unref(l->dirs);
    g_free(l);
}

static mode_t mode_of(gchar type) {
    switch (type) {
    case 'f': return S_IFREG;
    case 'd': return S_IFDIR;
#ifdef S_IFLNK
    case 'l': return S_IFLNK;
#else
    case 'l': return S_IFREG; /* no symlinks in GStatBuf (Windows) */
#endif
    case 'p': return S_IFIFO;
#ifdef S_IFSOCK
    case 's': return S_IFSOCK;
#endif
    case 'b': return S_IFBLK;
    default: return S_IFCHR;
    }
}

gboolean disk_remote_listing_add(DiskRemoteListing* l, const gchar* record, const gchar** rel_out) {
    if (record == NULL || record[0] == '\0' || record[1] != ' ') return FALSE;
    GStatBuf st;
    memset(&st, 0, sizeof st);
    st.st_mode = mode_of(record[0]);
    gchar* p = (gchar*)record + 2;
    guint64 v[6];
    for (guint i = 0; i < G_N_ELEMENTS(v); i++) {
        gchar* end = NULL;
        v[i] = g_ascii_strtoull(p, &end, 10);
        if (end == NULL || end == p) return FALSE;
        if (i == 2 && *end == '.') { /* the date's fraction */
            while (*end != '\0' && *end != ' ') end++;
        }
        if (*end != ' ') return FALSE;
        p = end + 1;
    }
    if (*p == '\0') return FALSE;
    st.st_size = (goffset)v[0];
#ifdef G_OS_UNIX
    st.st_blocks = (blkcnt_t)v[1];
#endif
    st.st_mtime = (time_t)v[2];
    st.st_dev = (dev_t)v[3];
    st.st_ino = (ino_t)v[4];
#ifdef G_OS_UNIX
    st.st_nlink = (nlink_t)v[5];
#else
    st.st_nlink = (short)MIN(v[5], (guint64)G_MAXSHORT);
#endif
    if (rel_out != NULL) *rel_out = p;
    if (strcmp(p, ".") == 0) {
        l->root = st;
        l->has_root = TRUE;
        return TRUE;
    }
    const gchar* slash = strrchr(p, '/');
    gchar* dir = slash != NULL ? g_strndup(p, (gsize)(slash - p)) : g_strdup("");
    const gchar* name = slash != NULL ? slash + 1 : p;
    GHashTable* entries = g_hash_table_lookup(l->dirs, dir);
    if (entries == NULL) {
        entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_hash_table_insert(l->dirs, dir, entries);
    } else {
        g_free(dir);
    }
    g_hash_table_replace(entries, g_strdup(name), g_memdup2(&st, sizeof st));
    return TRUE;
}

const GStatBuf* disk_remote_listing_root(const DiskRemoteListing* l) {
    return l != NULL && l->has_root ? &l->root : NULL;
}

GHashTable* disk_remote_listing_dir(const DiskRemoteListing* l, const gchar* rel) {
    return l != NULL ? g_hash_table_lookup(l->dirs, rel != NULL ? rel : "") : NULL;
}

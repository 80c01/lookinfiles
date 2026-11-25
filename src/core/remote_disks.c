#include "core/remote_disks.h"
#include "core/i18n.h"

#include <stdlib.h>
#include <string.h>

void remote_disk_free(gpointer data) {
    RemoteDisk* d = data;
    if (d == NULL) {
        return;
    }
    g_free(d->target);
    g_free(d->name);
    g_free(d->source);
    g_free(d->fstype);
    g_free(d);
}

static const gchar* const NETWORK_FS[] = {"nfs", "nfs4", "cifs", "smb3", "smbfs", "afpfs", "fuse.sshfs",
                                          "sshfs", "fuse.rclone", "9p", "ceph", "glusterfs", "davfs", NULL};
static const gchar* const POOL_FS[] = {"zfs", "btrfs", "apfs", "hfs", NULL};

static gboolean in_list(const gchar* s, const gchar* const* list) {
    for (gsize i = 0; s != NULL && list[i] != NULL; i++) {
        if (strcmp(s, list[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* path is dir or below it */
static gboolean under(const gchar* path, const gchar* dir) {
    gsize n = strlen(dir);
    return strncmp(path, dir, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static gboolean network_source(const gchar* source) {
    return source != NULL && (g_str_has_prefix(source, "//") || (source[0] != '/' && strstr(source, ":/") != NULL));
}

gboolean remote_disks_keep(const gchar* target, const gchar* source, const gchar* fstype) {
    if (target == NULL || target[0] != '/') {
        return FALSE;
    }
    if (strcmp(target, "/") != 0) {
        static const gchar* const hidden[] = {"/proc", "/sys", "/dev", "/snap", "/boot", "/var/lib/docker",
                                              "/var/lib/containers", "/var/snap", "/System", "/private", NULL};
        for (gsize i = 0; hidden[i] != NULL; i++) {
            if (under(target, hidden[i])) {
                return FALSE;
            }
        }
        if (under(target, "/run") && !under(target, "/run/media")) {
            return FALSE;
        }
    }
    gboolean device = source != NULL && g_str_has_prefix(source, "/dev/") && !g_str_has_prefix(source, "/dev/loop");
    if (fstype == NULL || fstype[0] == '\0') { /* df: the source only */
        return device || network_source(source);
    }
    if (in_list(fstype, NETWORK_FS)) {
        return TRUE;
    }
    return device || in_list(fstype, POOL_FS);
}

DiskVolumeKind remote_disk_kind(const gchar* target, const gchar* source, const gchar* fstype) {
    if (g_strcmp0(target, "/") == 0) {
        return DISK_VOLUME_SYSTEM;
    }
    if (in_list(fstype, NETWORK_FS) || network_source(source)) {
        return DISK_VOLUME_NETWORK;
    }
    if (g_strcmp0(fstype, "iso9660") == 0 || g_strcmp0(fstype, "udf") == 0) {
        return DISK_VOLUME_OPTICAL;
    }
    if (target != NULL && (under(target, "/media") || under(target, "/run/media") || under(target, "/Volumes"))) {
        return DISK_VOLUME_REMOVABLE;
    }
    return DISK_VOLUME_FIXED;
}

/* A value of findmnt -P with its \xHH escapes decoded; *p moves past the
 * closing quote */
static gchar* read_value(const gchar** p) {
    GString* out = g_string_new(NULL);
    const gchar* s = *p;
    while (*s != '\0' && *s != '"') {
        if (s[0] == '\\' && s[1] == 'x' && g_ascii_isxdigit(s[2]) && g_ascii_isxdigit(s[3])) {
            g_string_append_c(out, (gchar)(g_ascii_xdigit_value(s[2]) * 16 + g_ascii_xdigit_value(s[3])));
            s += 4;
        } else if (s[0] == '\\' && s[1] != '\0') {
            g_string_append_c(out, s[1]);
            s += 2;
        } else {
            g_string_append_c(out, *s++);
        }
    }
    if (*s == '"') {
        s++;
    }
    *p = s;
    return g_string_free(out, FALSE);
}

/* KEY="value" pairs of a findmnt -P line */
static GHashTable* read_pairs(const gchar* line) {
    GHashTable* pairs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const gchar* p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        const gchar* eq = strstr(p, "=\"");
        if (eq == NULL) {
            break;
        }
        gchar* key = g_strndup(p, eq - p);
        p = eq + 2;
        g_hash_table_replace(pairs, key, read_value(&p));
    }
    return pairs;
}

static guint64 to_u64(const gchar* s) {
    return (s != NULL && g_ascii_isdigit(s[0])) ? g_ascii_strtoull(s, NULL, 10) : 0;
}

static void add_disk(GPtrArray* out, const gchar* target, const gchar* source, const gchar* fstype,
                     guint64 size, guint64 avail) {
    if (!remote_disks_keep(target, source, fstype)) {
        return;
    }
    for (guint i = 0; i < out->len; i++) {
        if (strcmp(((RemoteDisk*)g_ptr_array_index(out, i))->target, target) == 0) {
            return; /* the same folder mounted over: the first one */
        }
    }
    RemoteDisk* d = g_new0(RemoteDisk, 1);
    d->target = g_strdup(target);
    d->name = (strcmp(target, "/") == 0) ? g_strdup(_("File System")) : g_path_get_basename(target);
    d->source = g_strdup(source != NULL ? source : "");
    d->fstype = g_strdup(fstype != NULL ? fstype : "");
    d->size = size;
    d->avail = avail;
    d->kind = remote_disk_kind(target, source, fstype);
    g_ptr_array_add(out, d);
}

/* A df -P line: Filesystem, 1024-blocks, Used, Available, Capacity, Mounted on
 * (the rest of the line, spaces included) */
static void add_df_line(GPtrArray* out, const gchar* line) {
    const gchar* p = line;
    gchar* fields[5] = {NULL};
    for (gint i = 0; i < 5; i++) {
        while (*p == ' ' || *p == '\t') p++;
        const gchar* start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        fields[i] = g_strndup(start, p - start);
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '/' && g_ascii_isdigit(fields[1][0])) {
        add_disk(out, p, fields[0], NULL, to_u64(fields[1]) * 1024, to_u64(fields[3]) * 1024);
    }
    for (gint i = 0; i < 5; i++) {
        g_free(fields[i]);
    }
}

static gint compare_disks(gconstpointer pa, gconstpointer pb) {
    const RemoteDisk* a = *(const RemoteDisk* const*)pa;
    const RemoteDisk* b = *(const RemoteDisk* const*)pb;
    DiskVolume va = {.name = (gchar*)a->name, .path = (gchar*)a->target, .kind = a->kind};
    DiskVolume vb = {.name = (gchar*)b->name, .path = (gchar*)b->target, .kind = b->kind};
    return disk_volume_compare(&va, &vb);
}

GPtrArray* remote_disks_parse(const gchar* output) {
    GPtrArray* out = g_ptr_array_new_with_free_func(remote_disk_free);
    if (output == NULL) {
        return out;
    }
    gchar** lines = g_strsplit(output, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strchomp(lines[i]);
        if (g_str_has_prefix(line, "TARGET=\"")) {
            GHashTable* pairs = read_pairs(line);
            add_disk(out, g_hash_table_lookup(pairs, "TARGET"), g_hash_table_lookup(pairs, "SOURCE"),
                     g_hash_table_lookup(pairs, "FSTYPE"), to_u64(g_hash_table_lookup(pairs, "SIZE")),
                     to_u64(g_hash_table_lookup(pairs, "AVAIL")));
            g_hash_table_unref(pairs);
        } else if (line[0] != '\0' && !g_str_has_prefix(line, "Filesystem")) {
            add_df_line(out, line);
        }
    }
    g_strfreev(lines);
    g_ptr_array_sort(out, compare_disks);
    return out;
}

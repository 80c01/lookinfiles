/* SFTP v3 packet encoding (see sftp_proto.h). */
#include "core/sftp_proto.h"
#include "core/i18n.h"

#include <string.h>

gboolean sftp_attrs_is_dir(const SftpAttrs* a) {
    return (a->flags & SFTP_ATTR_PERMISSIONS) && (a->permissions & SFTP_S_IFMT) == SFTP_S_IFDIR;
}

gboolean sftp_attrs_is_reg(const SftpAttrs* a) {
    return (a->flags & SFTP_ATTR_PERMISSIONS) && (a->permissions & SFTP_S_IFMT) == SFTP_S_IFREG;
}

gboolean sftp_attrs_is_link(const SftpAttrs* a) {
    return (a->flags & SFTP_ATTR_PERMISSIONS) && (a->permissions & SFTP_S_IFMT) == SFTP_S_IFLNK;
}

GByteArray* sftp_packet_begin(guint8 type) {
    GByteArray* b = g_byte_array_sized_new(64);
    guint8 head[5] = { 0, 0, 0, 0, type };
    g_byte_array_append(b, head, sizeof head);
    return b;
}

void sftp_put_u32(GByteArray* b, guint32 v) {
    guint8 x[4] = { (guint8)(v >> 24), (guint8)(v >> 16), (guint8)(v >> 8), (guint8)v };
    g_byte_array_append(b, x, sizeof x);
}

void sftp_put_u64(GByteArray* b, guint64 v) {
    sftp_put_u32(b, (guint32)(v >> 32));
    sftp_put_u32(b, (guint32)v);
}

void sftp_put_data(GByteArray* b, const void* data, gsize len) {
    sftp_put_u32(b, (guint32)len);
    if (len > 0) g_byte_array_append(b, data, (guint)len);
}

void sftp_put_str(GByteArray* b, const gchar* s) {
    sftp_put_data(b, s != NULL ? s : "", s != NULL ? strlen(s) : 0);
}

void sftp_put_attrs(GByteArray* b, const SftpAttrs* a) {
    guint32 flags = a != NULL ? (a->flags & ~SFTP_ATTR_EXTENDED) : 0;
    sftp_put_u32(b, flags);
    if (flags & SFTP_ATTR_SIZE) sftp_put_u64(b, a->size);
    if (flags & SFTP_ATTR_UIDGID) {
        sftp_put_u32(b, a->uid);
        sftp_put_u32(b, a->gid);
    }
    if (flags & SFTP_ATTR_PERMISSIONS) sftp_put_u32(b, a->permissions);
    if (flags & SFTP_ATTR_ACMODTIME) {
        sftp_put_u32(b, a->atime);
        sftp_put_u32(b, a->mtime);
    }
}

GBytes* sftp_packet_finish(GByteArray* b) {
    guint32 len = b->len - 4;
    b->data[0] = (guint8)(len >> 24);
    b->data[1] = (guint8)(len >> 16);
    b->data[2] = (guint8)(len >> 8);
    b->data[3] = (guint8)len;
    return g_byte_array_free_to_bytes(b);
}

void sftp_reader_init(SftpReader* r, const void* data, gsize len) {
    r->p = data;
    r->left = data != NULL ? len : 0;
    r->ok = TRUE;
}

static gboolean take(SftpReader* r, gsize n, const guint8** out) {
    if (!r->ok || r->left < n) {
        r->ok = FALSE;
        return FALSE;
    }
    *out = r->p;
    r->p += n;
    r->left -= n;
    return TRUE;
}

gboolean sftp_get_u8(SftpReader* r, guint8* v) {
    const guint8* x = NULL;
    if (!take(r, 1, &x)) return FALSE;
    *v = x[0];
    return TRUE;
}

gboolean sftp_get_u32(SftpReader* r, guint32* v) {
    const guint8* x = NULL;
    if (!take(r, 4, &x)) return FALSE;
    *v = ((guint32)x[0] << 24) | ((guint32)x[1] << 16) | ((guint32)x[2] << 8) | x[3];
    return TRUE;
}

gboolean sftp_get_u64(SftpReader* r, guint64* v) {
    guint32 hi = 0, lo = 0;
    if (!sftp_get_u32(r, &hi) || !sftp_get_u32(r, &lo)) return FALSE;
    *v = ((guint64)hi << 32) | lo;
    return TRUE;
}

gboolean sftp_get_data(SftpReader* r, const guint8** data, guint32* len) {
    guint32 n = 0;
    if (!sftp_get_u32(r, &n) || !take(r, n, data)) return FALSE;
    *len = n;
    return TRUE;
}

gchar* sftp_get_str(SftpReader* r) {
    const guint8* data = NULL;
    guint32 len = 0;
    if (!sftp_get_data(r, &data, &len)) return NULL;
    return g_utf8_make_valid((const gchar*)data, (gssize)len);
}

gboolean sftp_get_attrs(SftpReader* r, SftpAttrs* a) {
    memset(a, 0, sizeof *a);
    if (!sftp_get_u32(r, &a->flags)) return FALSE;
    if (a->flags & SFTP_ATTR_SIZE) sftp_get_u64(r, &a->size);
    if (a->flags & SFTP_ATTR_UIDGID) {
        sftp_get_u32(r, &a->uid);
        sftp_get_u32(r, &a->gid);
    }
    if (a->flags & SFTP_ATTR_PERMISSIONS) sftp_get_u32(r, &a->permissions);
    if (a->flags & SFTP_ATTR_ACMODTIME) {
        sftp_get_u32(r, &a->atime);
        sftp_get_u32(r, &a->mtime);
    }
    if (a->flags & SFTP_ATTR_EXTENDED) { /* name, value pairs: skipped */
        guint32 count = 0;
        sftp_get_u32(r, &count);
        for (guint32 i = 0; i < count && r->ok; i++) {
            const guint8* d = NULL;
            guint32 n = 0;
            sftp_get_data(r, &d, &n);
            sftp_get_data(r, &d, &n);
        }
        a->flags &= ~SFTP_ATTR_EXTENDED;
    }
    return r->ok;
}

GError* sftp_status_error(guint32 code, const gchar* message) {
    gint gcode = G_IO_ERROR_FAILED;
    const gchar* fallback = _("the remote operation failed");
    switch (code) {
    case SFTP_NO_SUCH_FILE:
        gcode = G_IO_ERROR_NOT_FOUND;
        fallback = _("no such file or folder");
        break;
    case SFTP_PERMISSION_DENIED:
        gcode = G_IO_ERROR_PERMISSION_DENIED;
        fallback = _("permission denied");
        break;
    case SFTP_OP_UNSUPPORTED:
        gcode = G_IO_ERROR_NOT_SUPPORTED;
        fallback = _("the remote machine does not support this operation");
        break;
    case SFTP_NO_CONNECTION:
    case SFTP_CONNECTION_LOST:
        gcode = G_IO_ERROR_CONNECTION_CLOSED;
        fallback = _("the connection to the remote machine was lost");
        break;
    case SFTP_EOF:
        fallback = _("end of file");
        break;
    default:
        break;
    }
    /* The server's text (English) only says more for a plain failure */
    gboolean own = gcode != G_IO_ERROR_FAILED || code == SFTP_EOF || message == NULL || message[0] == '\0';
    return g_error_new_literal(G_IO_ERROR, gcode, own ? fallback : message);
}

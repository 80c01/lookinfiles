#ifndef SFTP_PROTO_H
#define SFTP_PROTO_H

/* The SFTP protocol, version 3 (draft-ietf-secsh-filexfer-02, what OpenSSH
 * speaks): packet types, attributes, and the encoding of packets
 * ("uint32 length, byte type, uint32 id, ...", big-endian; strings are a
 * uint32 length then the bytes). Pure, no I/O (unit-tested); the client is
 * sftp_client.h. */
#include <gio/gio.h>

enum {
    SFTP_INIT = 1,
    SFTP_VERSION = 2,
    SFTP_OPEN = 3,
    SFTP_CLOSE = 4,
    SFTP_READ = 5,
    SFTP_WRITE = 6,
    SFTP_LSTAT = 7,
    SFTP_FSTAT = 8,
    SFTP_SETSTAT = 9,
    SFTP_FSETSTAT = 10,
    SFTP_OPENDIR = 11,
    SFTP_READDIR = 12,
    SFTP_REMOVE = 13,
    SFTP_MKDIR = 14,
    SFTP_RMDIR = 15,
    SFTP_REALPATH = 16,
    SFTP_STAT = 17,
    SFTP_RENAME = 18,
    SFTP_READLINK = 19,
    SFTP_SYMLINK = 20,
    SFTP_STATUS = 101,
    SFTP_HANDLE = 102,
    SFTP_DATA = 103,
    SFTP_NAME = 104,
    SFTP_ATTRS = 105,
    SFTP_EXTENDED = 200,
    SFTP_EXTENDED_REPLY = 201,
};

/* Status codes */
enum {
    SFTP_OK = 0,
    SFTP_EOF = 1,
    SFTP_NO_SUCH_FILE = 2,
    SFTP_PERMISSION_DENIED = 3,
    SFTP_FAILURE = 4,
    SFTP_BAD_MESSAGE = 5,
    SFTP_NO_CONNECTION = 6,
    SFTP_CONNECTION_LOST = 7,
    SFTP_OP_UNSUPPORTED = 8,
};

/* Open flags */
enum {
    SFTP_OPEN_READ = 0x01,
    SFTP_OPEN_WRITE = 0x02,
    SFTP_OPEN_APPEND = 0x04,
    SFTP_OPEN_CREAT = 0x08,
    SFTP_OPEN_TRUNC = 0x10,
    SFTP_OPEN_EXCL = 0x20,
};

/* Attribute flags: which fields of SftpAttrs are set */
enum {
    SFTP_ATTR_SIZE = 0x01,
    SFTP_ATTR_UIDGID = 0x02,
    SFTP_ATTR_PERMISSIONS = 0x04,
    SFTP_ATTR_ACMODTIME = 0x08,
    SFTP_ATTR_EXTENDED = 0x80000000u,
};

/* File types in the permissions (as stat's st_mode) */
#define SFTP_S_IFMT 0170000u
#define SFTP_S_IFDIR 0040000u
#define SFTP_S_IFREG 0100000u
#define SFTP_S_IFLNK 0120000u

typedef struct {
    guint32 flags; /* SFTP_ATTR_* */
    guint64 size;
    guint32 uid, gid;
    guint32 permissions; /* type and mode bits */
    guint32 atime, mtime;
} SftpAttrs;

/* Whether the attributes say a directory, a regular file, a symbolic link
 * (FALSE when the permissions are missing) */
gboolean sftp_attrs_is_dir(const SftpAttrs* a);
gboolean sftp_attrs_is_reg(const SftpAttrs* a);
gboolean sftp_attrs_is_link(const SftpAttrs* a);

/* Writing a packet: begin, put the fields, finish (sets the length; the
 * array is then the whole packet) */
GByteArray* sftp_packet_begin(guint8 type);
void sftp_put_u32(GByteArray* b, guint32 v);
void sftp_put_u64(GByteArray* b, guint64 v);
void sftp_put_data(GByteArray* b, const void* data, gsize len); /* a string */
void sftp_put_str(GByteArray* b, const gchar* s);
void sftp_put_attrs(GByteArray* b, const SftpAttrs* a); /* NULL: none */
GBytes* sftp_packet_finish(GByteArray* b);

/* Reading a packet body (after the length): every get fails (FALSE, ok
 * cleared) past the end, and later ones too */
typedef struct {
    const guint8* p;
    gsize left;
    gboolean ok;
} SftpReader;

void sftp_reader_init(SftpReader* r, const void* data, gsize len);
gboolean sftp_get_u8(SftpReader* r, guint8* v);
gboolean sftp_get_u32(SftpReader* r, guint32* v);
gboolean sftp_get_u64(SftpReader* r, guint64* v);
/* A string, pointing into the packet (not terminated) */
gboolean sftp_get_data(SftpReader* r, const guint8** data, guint32* len);
/* A string as a new UTF-8 text (invalid bytes replaced), NULL on failure */
gchar* sftp_get_str(SftpReader* r);
gboolean sftp_get_attrs(SftpReader* r, SftpAttrs* a);

/* The GError of a status reply that is not SFTP_OK: G_IO_ERROR_NOT_FOUND,
 * PERMISSION_DENIED, NOT_SUPPORTED, CONNECTION_CLOSED (with a translated
 * text), else FAILED with the server's message (when it gave one) */
GError* sftp_status_error(guint32 code, const gchar* message);

#endif /* SFTP_PROTO_H */

/* TNEF winmail.dat (see mail_tnef.h) */
#include "mail_tnef.h"
#include "i18n.h"
#include "mail_msg.h"
#include <gio/gio.h>
#include <string.h>

/* Attribute levels and ids (low 16 bits of the attribute) */
#define LVL_MESSAGE 1
#define LVL_ATTACHMENT 2

#define ATT_FROM 0x8000
#define ATT_SUBJECT 0x8004
#define ATT_DATE_SENT 0x8005
#define ATT_MESSAGE_CLASS 0x8008
#define ATT_BODY 0x800C
#define ATT_ATTACH_DATA 0x800F
#define ATT_ATTACH_TITLE 0x8010
#define ATT_ATTACH_RENDDATA 0x9002
#define ATT_MAPI_PROPS 0x9003
#define ATT_ATTACHMENT 0x9005
#define ATT_OEM_CODEPAGE 0x9007

/* MAPI property types */
#define PT_SHORT 0x0002
#define PT_LONG 0x0003
#define PT_FLOAT 0x0004
#define PT_DOUBLE 0x0005
#define PT_CURRENCY 0x0006
#define PT_APPTIME 0x0007
#define PT_ERROR 0x000A
#define PT_BOOLEAN 0x000B
#define PT_OBJECT 0x000D
#define PT_I8 0x0014
#define PT_STRING8 0x001E
#define PT_UNICODE 0x001F
#define PT_SYSTIME 0x0040
#define PT_CLSID 0x0048
#define PT_BINARY 0x0102
#define MV_FLAG 0x1000

#define MAX_PROPS 10000

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

gboolean mail_tnef_looks_like(const guint8* data, gsize len) {
    return data != NULL && len >= 6 && u32(data) == MAIL_TNEF_SIGNATURE;
}

/* ------------------------------------------------------------------------- */
/* MAPI property blocks                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    guint16 id;
    guint16 type;            /* without MV_FLAG */
    const guint8* value;     /* first value (variable types: its bytes) */
    gsize len;
} Prop;

static gsize fixed_size(guint16 type) {
    switch (type) {
    case PT_SHORT:
    case PT_LONG:
    case PT_FLOAT:
    case PT_ERROR:
    case PT_BOOLEAN:
    case 0x0001: /* PT_NULL */
        return 4;
    case PT_DOUBLE:
    case PT_CURRENCY:
    case PT_APPTIME:
    case PT_I8:
    case PT_SYSTIME:
        return 8;
    case PT_CLSID:
        return 16;
    default:
        return 0;
    }
}

static gboolean is_variable(guint16 type) {
    return type == PT_STRING8 || type == PT_UNICODE || type == PT_BINARY || type == PT_OBJECT;
}

/* The properties of a block; stops at the first damaged one */
static GArray* parse_props(const guint8* d, gsize n) {
    GArray* props = g_array_new(FALSE, TRUE, sizeof(Prop));
    if (n < 4) return props;
    guint32 count = u32(d);
    gsize p = 4;
    for (guint32 i = 0; i < count && i < MAX_PROPS; i++) {
        if (p + 4 > n) break;
        guint16 type = u16(d + p), id = u16(d + p + 2);
        p += 4;
        if (id >= 0x8000) {
            /* Named property: GUID, then a number or a name */
            if (p + 20 > n) break;
            guint32 kind = u32(d + p + 16);
            p += 20;
            if (kind == 0) {
                p += 4;
            } else {
                if (p + 4 > n) break;
                guint32 name_len = u32(d + p);
                p += 4;
                if (name_len > n - MIN(p, n)) break;
                p += ((gsize)name_len + 3) & ~(gsize)3;
            }
            if (p > n) break;
        }
        gboolean multi = (type & MV_FLAG) != 0;
        guint16 base = type & ~MV_FLAG;
        gboolean variable = is_variable(base);
        gsize fixed = fixed_size(base);
        if (!variable && fixed == 0) break; /* unknown type: the rest cannot be found */
        guint32 values = 1;
        if (multi || variable) {
            if (p + 4 > n) break;
            values = u32(d + p);
            p += 4;
        }
        Prop prop = {.id = id, .type = base};
        gboolean ok = TRUE;
        for (guint32 v = 0; v < values && ok; v++) {
            if (variable) {
                if (p + 4 > n) {
                    ok = FALSE;
                    break;
                }
                guint32 vlen = u32(d + p);
                p += 4;
                if (vlen > n - p) {
                    ok = FALSE;
                    break;
                }
                if (v == 0) {
                    prop.value = d + p;
                    prop.len = vlen;
                }
                p += ((gsize)vlen + 3) & ~(gsize)3;
                if (p > n) p = n;
            } else {
                if (fixed > n - p) {
                    ok = FALSE;
                    break;
                }
                if (v == 0) {
                    prop.value = d + p;
                    prop.len = fixed;
                }
                p += fixed;
            }
        }
        if (!ok) break;
        if (prop.value != NULL && !multi) g_array_append_val(props, prop);
    }
    return props;
}

static const Prop* find_prop(GArray* props, guint16 id) {
    for (guint i = 0; props != NULL && i < props->len; i++) {
        const Prop* p = &g_array_index(props, Prop, i);
        if (p->id == id) return p;
    }
    return NULL;
}

/* An 8-bit string in the message code page */
static gchar* string8(const guint8* d, gsize n, const gchar* charset) {
    while (n > 0 && d[n - 1] == '\0') n--;
    return mail_to_utf8((const gchar*)d, n, charset);
}

static gchar* utf16(const guint8* d, gsize n) {
    glong units = (glong)(n / 2);
    gunichar2* w = g_new(gunichar2, units + 1);
    for (glong i = 0; i < units; i++) w[i] = u16(d + 2 * i);
    while (units > 0 && w[units - 1] == 0) units--;
    gchar* s = g_utf16_to_utf8(w, units, NULL, NULL, NULL);
    g_free(w);
    return s;
}

/* A string property (Unicode or 8-bit); NULL when absent or blank */
static gchar* prop_string(GArray* props, guint16 id, const gchar* charset) {
    const Prop* p = find_prop(props, id);
    if (p == NULL) return NULL;
    gchar* s = p->type == PT_UNICODE   ? utf16(p->value, p->len)
               : p->type == PT_STRING8 ? string8(p->value, p->len, charset)
                                       : NULL;
    if (s != NULL && g_strstrip(s)[0] == '\0') g_clear_pointer(&s, g_free);
    return s;
}

static gint64 filetime(const guint8* d) {
    guint64 ft = (guint64)u32(d) | ((guint64)u32(d + 4) << 32);
    return ft != 0 ? (gint64)(ft / 10000000ULL) - 11644473600LL : -1;
}

/* ------------------------------------------------------------------------- */
/* Attributes                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* title;         /* attAttachTitle (8.3 name) */
    gchar* long_name;     /* PR_ATTACH_LONG_FILENAME */
    gchar* mime;          /* PR_ATTACH_MIME_TAG */
    const guint8* data;
    gsize size;
    gboolean has_data;
} Attachment;

static void attachment_clear(gpointer p) {
    Attachment* a = p;
    g_free(a->title);
    g_free(a->long_name);
    g_free(a->mime);
}

/* attDateSent: year, month, day, hour, minute, second, weekday */
static gint64 dtr_time(const guint8* d, gsize n) {
    if (n < 12) return -1;
    GDateTime* dt = g_date_time_new_utc(u16(d), u16(d + 2), u16(d + 4), u16(d + 6), u16(d + 8), u16(d + 10));
    if (dt == NULL) return -1;
    gint64 t = g_date_time_to_unix(dt);
    g_date_time_unref(dt);
    return t;
}

/* attFrom: a TRP (id, sizes) then the display name and the address */
static gchar* trp_sender(const guint8* d, gsize n, const gchar* charset) {
    if (n < 8) return NULL;
    gsize name_len = u16(d + 4), addr_len = u16(d + 6);
    if (8 + name_len > n) return NULL;
    gchar* name = string8(d + 8, name_len, charset);
    gsize a = 8 + ((name_len + 3) & ~(gsize)3);
    gchar* addr = a + addr_len <= n && addr_len > 0 ? string8(d + a, addr_len, charset) : NULL;
    gchar* out;
    if (addr != NULL && g_str_has_prefix(addr, "SMTP:")) memmove(addr, addr + 5, strlen(addr + 5) + 1);
    if (addr != NULL && strchr(addr, '@') != NULL && name[0] != '\0' && g_strcmp0(name, addr) != 0) {
        out = g_strdup_printf("%s <%s>", name, addr);
    } else {
        out = g_strdup(name[0] != '\0' ? name : addr);
    }
    g_free(name);
    g_free(addr);
    return out;
}

static void set_field(gchar** field, gchar* value) {
    if (value == NULL) return;
    g_free(*field);
    *field = value;
}

MailMessage* mail_tnef_read(const guint8* data, gsize len, gsize max_part, GError** error) {
    if (!mail_tnef_looks_like(data, len)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a TNEF stream"));
        return NULL;
    }
    gchar* charset = g_strdup("windows-1252");
    gchar *subject = NULL, *from = NULL, *klass = NULL, *body = NULL;
    gint64 date = -1;
    GArray* message_props = NULL;
    GArray* attachments = g_array_new(FALSE, TRUE, sizeof(Attachment));
    g_array_set_clear_func(attachments, attachment_clear);
    gsize p = 6;
    while (p + 9 <= len) {
        guint8 level = data[p];
        guint32 attr = u32(data + p + 1);
        guint32 size = u32(data + p + 5);
        p += 9;
        if (size > len - p) break; /* truncated */
        const guint8* d = data + p;
        p += size;
        p += MIN((gsize)2, len - p); /* checksum */
        guint16 id = (guint16)(attr & 0xFFFF);
        if (level == LVL_MESSAGE) {
            switch (id) {
            case ATT_OEM_CODEPAGE:
                if (size >= 4 && u32(d) != 0) {
                    g_free(charset);
                    charset = u32(d) == 65001 ? g_strdup("utf-8") : g_strdup_printf("cp%u", u32(d));
                }
                break;
            case ATT_SUBJECT:
                set_field(&subject, string8(d, size, charset));
                break;
            case ATT_FROM:
                set_field(&from, trp_sender(d, size, charset));
                break;
            case ATT_DATE_SENT:
                date = dtr_time(d, size);
                break;
            case ATT_MESSAGE_CLASS:
                set_field(&klass, string8(d, size, charset));
                break;
            case ATT_BODY:
                set_field(&body, string8(d, size, charset));
                break;
            case ATT_MAPI_PROPS:
                if (message_props != NULL) g_array_unref(message_props);
                message_props = parse_props(d, size);
                break;
            default:
                break;
            }
        } else if (level == LVL_ATTACHMENT) {
            if (id == ATT_ATTACH_RENDDATA || attachments->len == 0) {
                Attachment a = {0};
                g_array_append_val(attachments, a);
            }
            Attachment* a = &g_array_index(attachments, Attachment, attachments->len - 1);
            if (id == ATT_ATTACH_TITLE) {
                set_field(&a->title, string8(d, size, charset));
            } else if (id == ATT_ATTACH_DATA) {
                a->data = d;
                a->size = size;
                a->has_data = TRUE;
            } else if (id == ATT_ATTACHMENT) {
                GArray* props = parse_props(d, size);
                set_field(&a->long_name, prop_string(props, 0x3707, charset));
                if (a->long_name == NULL) set_field(&a->long_name, prop_string(props, 0x3001, charset));
                set_field(&a->mime, prop_string(props, 0x370E, charset));
                const Prop* bin = find_prop(props, 0x3701);
                if (!a->has_data && bin != NULL && bin->type == PT_BINARY) {
                    a->data = bin->value;
                    a->size = bin->len;
                    a->has_data = TRUE;
                }
                g_array_unref(props);
            }
        }
    }

    MailMessage* msg = mail_msg_message_new(NULL);
    /* The MAPI properties are more complete than the attributes */
    gchar* mapi_subject = prop_string(message_props, 0x0037, charset);
    set_field(&msg->subject, mapi_subject != NULL ? mapi_subject : g_steal_pointer(&subject));
    gchar* sender = prop_string(message_props, 0x0C1A, charset);
    gchar* email = prop_string(message_props, 0x5D01, charset);
    if (email == NULL) email = prop_string(message_props, 0x0C1F, charset);
    if (email != NULL && strchr(email, '@') == NULL) g_clear_pointer(&email, g_free);
    if (sender != NULL || email != NULL) {
        set_field(&msg->from, sender != NULL && email != NULL ? g_strdup_printf("%s <%s>", sender, email)
                                                             : g_strdup(sender != NULL ? sender : email));
    } else {
        set_field(&msg->from, g_steal_pointer(&from));
    }
    g_free(sender);
    g_free(email);
    set_field(&msg->to, prop_string(message_props, 0x0E04, charset));
    set_field(&msg->cc, prop_string(message_props, 0x0E03, charset));
    const Prop* t = find_prop(message_props, 0x0039);
    if (t != NULL && t->type == PT_SYSTIME) date = filetime(t->value);
    msg->date = date;
    gchar* mapi_class = prop_string(message_props, 0x001A, charset);
    if (mapi_class != NULL) set_field(&klass, mapi_class);
    msg->encrypted = klass != NULL && g_str_has_prefix(klass, "IPM.Note.SMIME") && strstr(klass, "Signed") == NULL;

    /* Body: plain text, else HTML, else compressed RTF */
    gchar* text = prop_string(message_props, 0x1000, charset);
    if (text == NULL) text = g_steal_pointer(&body);
    const Prop* html = find_prop(message_props, 0x1013);
    const Prop* rtf = find_prop(message_props, 0x1009);
    if (text != NULL) {
        g_ptr_array_add(msg->root->children, mail_msg_part_new("text/plain", "utf-8", g_bytes_new_take(text, strlen(text))));
    } else if (html != NULL && html->type == PT_BINARY) {
        g_ptr_array_add(msg->root->children, mail_msg_part_new("text/html", NULL, g_bytes_new(html->value, html->len)));
    } else if (html != NULL && html->type == PT_UNICODE) {
        gchar* h = utf16(html->value, html->len);
        if (h != NULL) g_ptr_array_add(msg->root->children, mail_msg_part_new("text/html", "utf-8", g_bytes_new_take(h, strlen(h))));
    } else if (rtf != NULL && rtf->type == PT_BINARY) {
        MailPart* part = mail_msg_rtf_part(rtf->value, rtf->len);
        if (part != NULL) g_ptr_array_add(msg->root->children, part);
    }

    for (guint i = 0; i < attachments->len; i++) {
        Attachment* a = &g_array_index(attachments, Attachment, i);
        const gchar* name = a->long_name != NULL ? a->long_name : a->title;
        GBytes* bytes = a->has_data && a->size <= max_part ? g_bytes_new(a->data, a->size) : NULL;
        MailPart* part = mail_msg_part_new(a->mime, NULL, bytes);
        if (part->content_type == NULL) part->content_type = mail_msg_guess_type(name != NULL ? name : "file");
        part->filename = g_strdup(name != NULL ? name : _("(attachment)"));
        part->attachment = TRUE;
        part->size = a->size;
        g_ptr_array_add(msg->root->children, part);
    }

    g_free(subject);
    g_free(from);
    g_free(klass);
    g_free(body);
    g_free(charset);
    if (message_props != NULL) g_array_unref(message_props);
    g_array_unref(attachments);
    return msg;
}

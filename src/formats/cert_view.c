/* X.509 certificates and keys for the preview (see cert_view.h). */
#include "formats/cert_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

/* --- DER reading --- */

typedef struct {
    const guchar* p;
    gsize len;
} Der;

/* Next element: tag, content (inner) and the rest after it. FALSE if damaged. */
static gboolean der_next(Der* d, guint* tag, Der* inner) {
    if (d->len < 2) {
        return FALSE;
    }
    guint t = d->p[0];
    gsize pos = 1;
    gsize n = d->p[pos++];
    if (n & 0x80) {
        guint bytes = n & 0x7F;
        if (bytes == 0 || bytes > 4 || pos + bytes > d->len) {
            return FALSE;
        }
        n = 0;
        for (guint i = 0; i < bytes; i++) {
            n = (n << 8) | d->p[pos++];
        }
    }
    if (n > d->len - pos) {
        return FALSE;
    }
    *tag = t;
    inner->p = d->p + pos;
    inner->len = n;
    d->p += pos + n;
    d->len -= pos + n;
    return TRUE;
}

/* Next element, which must have this tag */
static gboolean der_expect(Der* d, guint tag, Der* inner) {
    Der copy = *d;
    guint t;
    if (!der_next(&copy, &t, inner) || t != tag) {
        return FALSE;
    }
    *d = copy;
    return TRUE;
}

#define TAG_BOOLEAN 0x01
#define TAG_INTEGER 0x02
#define TAG_BIT_STRING 0x03
#define TAG_OCTET_STRING 0x04
#define TAG_OID 0x06
#define TAG_SEQUENCE 0x30
#define TAG_SET 0x31
#define TAG_UTC_TIME 0x17
#define TAG_GENERALIZED_TIME 0x18

static gchar* oid_string(Der oid) {
    GString* s = g_string_new(NULL);
    guint64 value = 0;
    gboolean first = TRUE;
    for (gsize i = 0; i < oid.len; i++) {
        value = (value << 7) | (oid.p[i] & 0x7F);
        if (!(oid.p[i] & 0x80)) {
            if (first) {
                guint64 a = value < 40 ? 0 : value < 80 ? 1 : 2;
                g_string_append_printf(s, "%" G_GUINT64_FORMAT ".%" G_GUINT64_FORMAT, a, value - a * 40);
                first = FALSE;
            } else {
                g_string_append_printf(s, ".%" G_GUINT64_FORMAT, value);
            }
            value = 0;
        }
    }
    return g_string_free(s, FALSE);
}

static gchar* hex_colon(const guchar* p, gsize len) {
    GString* s = g_string_new(NULL);
    for (gsize i = 0; i < len; i++) {
        g_string_append_printf(s, "%s%02X", i > 0 ? ":" : "", p[i]);
    }
    return g_string_free(s, FALSE);
}

/* Directory string of a name attribute as UTF-8 */
static gchar* der_string(guint tag, Der v) {
    if (tag == 0x1E) { /* BMPString: UTF-16 big-endian */
        GString* s = g_string_new(NULL);
        for (gsize i = 0; i + 1 < v.len; i += 2) {
            g_string_append_unichar(s, (gunichar)((v.p[i] << 8) | v.p[i + 1]));
        }
        return g_string_free(s, FALSE);
    }
    if (tag == 0x14) { /* T61String: treated as Latin-1 */
        gchar* s = g_convert((const gchar*)v.p, (gssize)v.len, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
        if (s != NULL) {
            return s;
        }
    }
    return g_utf8_make_valid((const gchar*)v.p, (gssize)v.len);
}

static const struct {
    const gchar* oid;
    const gchar* name;
} NAME_ATTRS[] = {
    { "2.5.4.3", "CN" }, { "2.5.4.6", "C" }, { "2.5.4.7", "L" }, { "2.5.4.8", "ST" },
    { "2.5.4.10", "O" }, { "2.5.4.11", "OU" }, { "2.5.4.5", "serialNumber" },
    { "2.5.4.9", "street" }, { "2.5.4.17", "postalCode" }, { "2.5.4.97", "organizationIdentifier" },
    { "1.2.840.113549.1.9.1", "emailAddress" }, { "0.9.2342.19200300.100.1.25", "DC" },
    { "0.9.2342.19200300.100.1.1", "UID" }, { "2.5.4.4", "SN" }, { "2.5.4.42", "GN" },
    { "2.5.4.12", "title" }, { "1.3.6.1.4.1.311.60.2.1.3", "jurisdictionC" },
    { "2.5.4.15", "businessCategory" },
};

/* "CN=x, O=y" in the order of the certificate (most specific first is the
 * usual display: the order is reversed, as OpenSSL -nameopt RFC2253 does) */
static gchar* parse_name(Der name, gchar** cn) {
    GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
    Der set;
    guint tag;
    while (der_next(&name, &tag, &set)) {
        Der atv;
        while (der_expect(&set, TAG_SEQUENCE, &atv)) {
            Der oid, value;
            guint vtag;
            if (!der_expect(&atv, TAG_OID, &oid) || !der_next(&atv, &vtag, &value)) {
                continue;
            }
            gchar* id = oid_string(oid);
            gchar* text = der_string(vtag, value);
            const gchar* label = id;
            for (gsize i = 0; i < G_N_ELEMENTS(NAME_ATTRS); i++) {
                if (strcmp(NAME_ATTRS[i].oid, id) == 0) {
                    label = NAME_ATTRS[i].name;
                }
            }
            if (cn != NULL && *cn == NULL && strcmp(id, "2.5.4.3") == 0) {
                *cn = g_strdup(text);
            }
            g_ptr_array_insert(parts, 0, g_strdup_printf("%s=%s", label, text));
            g_free(text);
            g_free(id);
        }
    }
    g_ptr_array_add(parts, NULL);
    gchar* out = g_strjoinv(", ", (gchar**)parts->pdata);
    g_ptr_array_free(parts, TRUE);
    return out;
}

static gint64 parse_time(guint tag, Der t) {
    gchar buf[32];
    if (t.len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, t.p, t.len);
    buf[t.len] = '\0';
    gint year, mon, day, hour, min, sec = 0;
    if (tag == TAG_UTC_TIME) {
        if (sscanf(buf, "%2d%2d%2d%2d%2d%2d", &year, &mon, &day, &hour, &min, &sec) < 5) {
            return -1;
        }
        year += (year < 50) ? 2000 : 1900;
    } else if (tag == TAG_GENERALIZED_TIME) {
        if (sscanf(buf, "%4d%2d%2d%2d%2d%2d", &year, &mon, &day, &hour, &min, &sec) < 5) {
            return -1;
        }
    } else {
        return -1;
    }
    GDateTime* dt = g_date_time_new_utc(year, mon, day, hour, min, sec);
    if (dt == NULL) {
        return -1;
    }
    gint64 unix_time = g_date_time_to_unix(dt);
    g_date_time_unref(dt);
    return unix_time;
}

static const struct {
    const gchar* oid;
    const gchar* name;
} ALGORITHMS[] = {
    { "1.2.840.113549.1.1.1", "RSA" },
    { "1.2.840.113549.1.1.4", "MD5 with RSA" },
    { "1.2.840.113549.1.1.5", "SHA-1 with RSA" },
    { "1.2.840.113549.1.1.10", "RSASSA-PSS" },
    { "1.2.840.113549.1.1.11", "SHA-256 with RSA" },
    { "1.2.840.113549.1.1.12", "SHA-384 with RSA" },
    { "1.2.840.113549.1.1.13", "SHA-512 with RSA" },
    { "1.2.840.10045.2.1", "EC" },
    { "1.2.840.10045.4.1", "ECDSA with SHA-1" },
    { "1.2.840.10045.4.3.2", "ECDSA with SHA-256" },
    { "1.2.840.10045.4.3.3", "ECDSA with SHA-384" },
    { "1.2.840.10045.4.3.4", "ECDSA with SHA-512" },
    { "1.2.840.10040.4.1", "DSA" },
    { "1.2.840.10040.4.3", "DSA with SHA-1" },
    { "2.16.840.1.101.3.4.3.2", "DSA with SHA-256" },
    { "1.3.101.110", "X25519" },
    { "1.3.101.111", "X448" },
    { "1.3.101.112", "Ed25519" },
    { "1.3.101.113", "Ed448" },
    { "1.2.643.7.1.1.1.1", "GOST R 34.10-2012 256" },
};

static const struct {
    const gchar* oid;
    const gchar* name;
} CURVES[] = {
    { "1.2.840.10045.3.1.7", "P-256" },
    { "1.3.132.0.34", "P-384" },
    { "1.3.132.0.35", "P-521" },
    { "1.3.132.0.10", "secp256k1" },
    { "1.3.36.3.3.2.8.1.1.7", "brainpoolP256r1" },
};

static gchar* algorithm_name(Der alg_id) {
    Der oid;
    if (!der_expect(&alg_id, TAG_OID, &oid)) {
        return g_strdup("unknown");
    }
    gchar* id = oid_string(oid);
    for (gsize i = 0; i < G_N_ELEMENTS(ALGORITHMS); i++) {
        if (strcmp(ALGORITHMS[i].oid, id) == 0) {
            g_free(id);
            return g_strdup(ALGORITHMS[i].name);
        }
    }
    return id;
}

/* Bits of a big-endian unsigned INTEGER */
static guint integer_bits(Der n) {
    while (n.len > 0 && n.p[0] == 0) {
        n.p++;
        n.len--;
    }
    if (n.len == 0) {
        return 0;
    }
    guint bits = (guint)(n.len - 1) * 8;
    for (guchar top = n.p[0]; top != 0; top >>= 1) {
        bits++;
    }
    return bits;
}

/* "RSA 2048 bits", "EC P-256"... of a SubjectPublicKeyInfo */
static gchar* describe_key(Der spki) {
    Der alg, key;
    if (!der_expect(&spki, TAG_SEQUENCE, &alg) || !der_expect(&spki, TAG_BIT_STRING, &key)) {
        return g_strdup("unknown");
    }
    Der alg_copy = alg;
    gchar* name = algorithm_name(alg_copy);
    Der oid;
    der_expect(&alg, TAG_OID, &oid);
    if (strcmp(name, "RSA") == 0 && key.len > 1) {
        Der bits = { key.p + 1, key.len - 1 }; /* skip the unused-bits byte */
        Der seq, modulus;
        if (der_expect(&bits, TAG_SEQUENCE, &seq) && der_expect(&seq, TAG_INTEGER, &modulus)) {
            gchar* out = g_strdup_printf(_("RSA %u bits"), integer_bits(modulus));
            g_free(name);
            return out;
        }
    } else if (strcmp(name, "EC") == 0) {
        Der curve;
        if (der_expect(&alg, TAG_OID, &curve)) {
            gchar* id = oid_string(curve);
            const gchar* curve_name = id;
            for (gsize i = 0; i < G_N_ELEMENTS(CURVES); i++) {
                if (strcmp(CURVES[i].oid, id) == 0) {
                    curve_name = CURVES[i].name;
                }
            }
            gchar* out = g_strdup_printf("EC %s", curve_name);
            g_free(id);
            g_free(name);
            return out;
        }
    }
    return name;
}

static void parse_alt_names(X509Info* info, Der value) {
    Der seq;
    if (!der_expect(&value, TAG_SEQUENCE, &seq)) {
        return;
    }
    Der item;
    guint tag;
    while (der_next(&seq, &tag, &item)) {
        switch (tag) {
        case 0x82:
            g_ptr_array_add(info->alt_names, g_strdup_printf("DNS:%.*s", (gint)item.len, item.p));
            break;
        case 0x81:
            g_ptr_array_add(info->alt_names, g_strdup_printf("email:%.*s", (gint)item.len, item.p));
            break;
        case 0x86:
            g_ptr_array_add(info->alt_names, g_strdup_printf("URI:%.*s", (gint)item.len, item.p));
            break;
        case 0x87:
            if (item.len == 4 || item.len == 16) {
                GInetAddress* addr = g_inet_address_new_from_bytes(
                    item.p, item.len == 4 ? G_SOCKET_FAMILY_IPV4 : G_SOCKET_FAMILY_IPV6);
                gchar* text = g_inet_address_to_string(addr);
                g_ptr_array_add(info->alt_names, g_strdup_printf("IP:%s", text));
                g_free(text);
                g_object_unref(addr);
            }
            break;
        default:
            break;
        }
    }
}

static const gchar* const KEY_USAGES[] = {
    N_("Digital Signature"), N_("Non Repudiation"), N_("Key Encipherment"), N_("Data Encipherment"),
    N_("Key Agreement"), N_("Certificate Sign"), N_("CRL Sign"), N_("Encipher Only"), N_("Decipher Only"),
};

static gchar* parse_key_usage(Der value) {
    Der bits;
    if (!der_expect(&value, TAG_BIT_STRING, &bits) || bits.len < 2) {
        return NULL;
    }
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(KEY_USAGES); i++) {
        gsize byte = 1 + i / 8;
        if (byte < bits.len && (bits.p[byte] & (0x80 >> (i % 8)))) {
            g_string_append_printf(s, "%s%s", s->len > 0 ? ", " : "", _(KEY_USAGES[i]));
        }
    }
    return g_string_free(s, s->len == 0);
}

static const struct {
    const gchar* oid;
    const gchar* name;
} EXT_KEY_USAGES[] = {
    { "1.3.6.1.5.5.7.3.1", N_("TLS Web Server Authentication") },
    { "1.3.6.1.5.5.7.3.2", N_("TLS Web Client Authentication") },
    { "1.3.6.1.5.5.7.3.3", N_("Code Signing") },
    { "1.3.6.1.5.5.7.3.4", N_("E-mail Protection") },
    { "1.3.6.1.5.5.7.3.8", N_("Time Stamping") },
    { "1.3.6.1.5.5.7.3.9", N_("OCSP Signing") },
    { "2.5.29.37.0", N_("Any") },
};

static gchar* parse_ext_key_usage(Der value) {
    Der seq, oid;
    if (!der_expect(&value, TAG_SEQUENCE, &seq)) {
        return NULL;
    }
    GString* s = g_string_new(NULL);
    while (der_expect(&seq, TAG_OID, &oid)) {
        gchar* id = oid_string(oid);
        const gchar* name = id;
        for (gsize i = 0; i < G_N_ELEMENTS(EXT_KEY_USAGES); i++) {
            if (strcmp(EXT_KEY_USAGES[i].oid, id) == 0) {
                name = _(EXT_KEY_USAGES[i].name);
            }
        }
        g_string_append_printf(s, "%s%s", s->len > 0 ? ", " : "", name);
        g_free(id);
    }
    return g_string_free(s, s->len == 0);
}

static void parse_extensions(X509Info* info, Der exts) {
    Der seq, ext;
    if (!der_expect(&exts, TAG_SEQUENCE, &seq)) {
        return;
    }
    while (der_expect(&seq, TAG_SEQUENCE, &ext)) {
        Der oid, value, critical;
        if (!der_expect(&ext, TAG_OID, &oid)) {
            continue;
        }
        der_expect(&ext, TAG_BOOLEAN, &critical);
        if (!der_expect(&ext, TAG_OCTET_STRING, &value)) {
            continue;
        }
        gchar* id = oid_string(oid);
        if (strcmp(id, "2.5.29.17") == 0) {
            parse_alt_names(info, value);
        } else if (strcmp(id, "2.5.29.19") == 0) {
            Der bc, ca;
            info->is_ca = 0;
            if (der_expect(&value, TAG_SEQUENCE, &bc) && der_expect(&bc, TAG_BOOLEAN, &ca) &&
                ca.len == 1 && ca.p[0] != 0) {
                info->is_ca = 1;
            }
        } else if (strcmp(id, "2.5.29.15") == 0) {
            info->key_usage = parse_key_usage(value);
        } else if (strcmp(id, "2.5.29.37") == 0) {
            info->ext_key_usage = parse_ext_key_usage(value);
        }
        g_free(id);
    }
}

static gchar* fingerprint(GChecksumType type, const guchar* der, gsize len) {
    GChecksum* sum = g_checksum_new(type);
    g_checksum_update(sum, der, (gssize)len);
    guint8 digest[64];
    gsize n = sizeof(digest);
    g_checksum_get_digest(sum, digest, &n);
    g_checksum_free(sum);
    return hex_colon(digest, n);
}

static gboolean invalid(GError** error) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a valid X.509 certificate"));
    return FALSE;
}

X509Info* x509_parse(const guchar* der, gsize len, GError** error) {
    Der all = { der, len };
    Der cert, tbs, sig_alg;
    if (!der_expect(&all, TAG_SEQUENCE, &cert) || !der_expect(&cert, TAG_SEQUENCE, &tbs) ||
        !der_expect(&cert, TAG_SEQUENCE, &sig_alg)) {
        invalid(error);
        return NULL;
    }
    X509Info* info = g_new0(X509Info, 1);
    info->alt_names = g_ptr_array_new_with_free_func(g_free);
    info->is_ca = -1;
    info->version = 1;
    info->not_before = info->not_after = -1;

    Der version, serial, tbs_sig, issuer, validity, subject, spki;
    if (der_expect(&tbs, 0xA0, &version)) {
        Der v;
        if (der_expect(&version, TAG_INTEGER, &v) && v.len == 1) {
            info->version = v.p[0] + 1u;
        }
    }
    if (!der_expect(&tbs, TAG_INTEGER, &serial) || !der_expect(&tbs, TAG_SEQUENCE, &tbs_sig) ||
        !der_expect(&tbs, TAG_SEQUENCE, &issuer) || !der_expect(&tbs, TAG_SEQUENCE, &validity) ||
        !der_expect(&tbs, TAG_SEQUENCE, &subject) || !der_expect(&tbs, TAG_SEQUENCE, &spki)) {
        x509_info_free(info);
        invalid(error);
        return NULL;
    }
    info->serial = hex_colon(serial.p, serial.len);
    info->signature = algorithm_name(sig_alg);
    info->issuer = parse_name(issuer, NULL);
    info->subject = parse_name(subject, &info->subject_cn);
    Der t;
    guint tag;
    if (der_next(&validity, &tag, &t)) {
        info->not_before = parse_time(tag, t);
    }
    if (der_next(&validity, &tag, &t)) {
        info->not_after = parse_time(tag, t);
    }
    info->key = describe_key(spki);
    Der ext;
    while (der_next(&tbs, &tag, &ext)) {
        if (tag == 0xA3) {
            parse_extensions(info, ext);
        }
    }
    gsize cert_len = (gsize)(all.p - der);
    info->sha256 = fingerprint(G_CHECKSUM_SHA256, der, cert_len);
    info->sha1 = fingerprint(G_CHECKSUM_SHA1, der, cert_len);
    return info;
}

void x509_info_free(X509Info* info) {
    if (info == NULL) {
        return;
    }
    g_free(info->subject);
    g_free(info->subject_cn);
    g_free(info->issuer);
    g_free(info->serial);
    g_free(info->key);
    g_free(info->signature);
    g_ptr_array_free(info->alt_names, TRUE);
    g_free(info->key_usage);
    g_free(info->ext_key_usage);
    g_free(info->sha256);
    g_free(info->sha1);
    g_free(info);
}

/* --- PEM --- */

typedef struct {
    gchar* label;      /* "CERTIFICATE", "PRIVATE KEY"... */
    guchar* der;
    gsize len;
} PemBlock;

static void pem_block_clear(gpointer data) {
    PemBlock* b = data;
    g_free(b->label);
    g_free(b->der);
}

static GArray* pem_blocks(const gchar* text, gsize len) {
    GArray* blocks = g_array_new(FALSE, TRUE, sizeof(PemBlock));
    g_array_set_clear_func(blocks, pem_block_clear);
    const gchar* end = text + len;
    const gchar* p = text;
    while (p < end) {
        const gchar* begin = g_strstr_len(p, end - p, "-----BEGIN ");
        if (begin == NULL) {
            break;
        }
        const gchar* label_start = begin + 11;
        const gchar* label_end = g_strstr_len(label_start, end - label_start, "-----");
        if (label_end == NULL) {
            break;
        }
        gchar* label = g_strndup(label_start, (gsize)(label_end - label_start));
        gchar* end_marker = g_strdup_printf("-----END %s-----", label);
        const gchar* body = label_end + 5;
        const gchar* stop = g_strstr_len(body, end - body, end_marker);
        g_free(end_marker);
        if (stop == NULL) {
            g_free(label);
            break;
        }
        /* Base64 body, skipping headers ("Proc-Type: ...") and blank lines */
        GString* b64 = g_string_new(NULL);
        gchar* chunk = g_strndup(body, (gsize)(stop - body));
        gchar** lines = g_strsplit(chunk, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            g_strstrip(lines[i]);
            if (strchr(lines[i], ':') == NULL) {
                g_string_append(b64, lines[i]);
            }
        }
        g_strfreev(lines);
        g_free(chunk);
        PemBlock block = { label, NULL, 0 };
        block.der = g_base64_decode(b64->str, &block.len);
        g_string_free(b64, TRUE);
        g_array_append_val(blocks, block);
        p = stop + 1;
    }
    return blocks;
}

/* --- Display --- */

static gchar* format_date(gint64 t) {
    if (t < 0) {
        return g_strdup("unknown");
    }
    GDateTime* dt = g_date_time_new_from_unix_utc(t);
    gchar* s = g_date_time_format(dt, "%Y-%m-%d %H:%M UTC");
    g_date_time_unref(dt);
    return s;
}

static void append_validity(PreviewDoc* doc, const X509Info* info, gint64 now) {
    gchar* from = format_date(info->not_before);
    gchar* until = format_date(info->not_after);
    preview_doc_field(doc, _("Valid from"), from);
    preview_doc_field(doc, _("Valid until"), until);
    g_free(from);
    g_free(until);
    if (info->not_after < 0) {
        return;
    }
    gint64 days = (info->not_after - now) / 86400;
    gchar* status;
    PreviewStyle style;
    if (info->not_before > now) {
        status = g_strdup(_("not valid yet"));
        style = PREVIEW_STYLE_WARNING;
    } else if (info->not_after < now) {
        gint64 ago = (now - info->not_after) / 86400;
        status = ago == 0 ? g_strdup(_("expired today"))
                          : g_strdup_printf(ngettext("expired %d day ago", "expired %d days ago", (gulong)ago), (gint)ago);
        style = PREVIEW_STYLE_ERROR;
    } else {
        status = g_strdup_printf(ngettext("valid, expires in %d day", "valid, expires in %d days", (gulong)days), (gint)days);
        style = days < 30 ? PREVIEW_STYLE_WARNING : PREVIEW_STYLE_SUCCESS;
    }
    preview_doc_field_styled(doc, _("Status"), status, style);
    g_free(status);
}

static void append_certificate(PreviewDoc* doc, const X509Info* info, gint64 now) {
    preview_doc_field_styled(doc, _("Subject"), info->subject, PREVIEW_STYLE_BOLD);
    gboolean self_signed = g_strcmp0(info->subject, info->issuer) == 0;
    gchar* issuer = self_signed ? g_strdup_printf(_("%s (self-signed)"), info->issuer) : g_strdup(info->issuer);
    preview_doc_field(doc, _("Issuer"), issuer);
    g_free(issuer);
    append_validity(doc, info, now);
    if (info->alt_names->len > 0) {
        GString* names = g_string_new(NULL);
        for (guint i = 0; i < info->alt_names->len && i < 50; i++) {
            g_string_append_printf(names, "%s%s", i > 0 ? "\n" : "", (gchar*)g_ptr_array_index(info->alt_names, i));
        }
        if (info->alt_names->len > 50) {
            g_string_append_printf(names, _("\n… %u more"), info->alt_names->len - 50);
        }
        preview_doc_field(doc, _("Alt names"), names->str);
        g_string_free(names, TRUE);
    }
    preview_doc_field(doc, _("Public key"), info->key);
    preview_doc_field(doc, _("Signature"), info->signature);
    if (info->is_ca >= 0) {
        preview_doc_field(doc, _("CA"), info->is_ca ? _("yes") : _("no"));
    }
    preview_doc_field(doc, _("Key usage"), info->key_usage);
    preview_doc_field(doc, _("Extended use"), info->ext_key_usage);
    preview_doc_field(doc, _("Serial"), info->serial);
    gchar* version = g_strdup_printf("%u", info->version);
    preview_doc_field(doc, _("Version"), version);
    g_free(version);
    preview_doc_field(doc, _("SHA-256"), info->sha256);
    preview_doc_field(doc, _("SHA-1"), info->sha1);
}

/* Name of a private key block, from its PEM label */
static gchar* key_kind(const gchar* label) {
    if (strcmp(label, "RSA PRIVATE KEY") == 0) return g_strdup("RSA private key (PKCS #1)");
    if (strcmp(label, "EC PRIVATE KEY") == 0) return g_strdup("EC private key");
    if (strcmp(label, "DSA PRIVATE KEY") == 0) return g_strdup("DSA private key");
    if (strcmp(label, "OPENSSH PRIVATE KEY") == 0) return g_strdup("OpenSSH private key");
    if (strcmp(label, "ENCRYPTED PRIVATE KEY") == 0) return g_strdup("Encrypted private key (PKCS #8)");
    if (strcmp(label, "PRIVATE KEY") == 0) return g_strdup("Private key (PKCS #8)");
    return NULL;
}

/* PKCS #8 PrivateKeyInfo: SEQUENCE { version, AlgorithmIdentifier, key } */
static gchar* pkcs8_algorithm(const guchar* der, gsize len) {
    Der all = { der, len };
    Der seq, version, alg;
    if (der_expect(&all, TAG_SEQUENCE, &seq) && der_expect(&seq, TAG_INTEGER, &version) &&
        der_expect(&seq, TAG_SEQUENCE, &alg)) {
        return algorithm_name(alg);
    }
    return NULL;
}

/* PKCS #10 request: SEQUENCE { info SEQUENCE { version, subject, spki, ... }, ... } */
static void append_request(PreviewDoc* doc, const guchar* der, gsize len) {
    Der all = { der, len };
    Der req, info, version, subject, spki;
    if (der_expect(&all, TAG_SEQUENCE, &req) && der_expect(&req, TAG_SEQUENCE, &info) &&
        der_expect(&info, TAG_INTEGER, &version) && der_expect(&info, TAG_SEQUENCE, &subject) &&
        der_expect(&info, TAG_SEQUENCE, &spki)) {
        gchar* name = parse_name(subject, NULL);
        gchar* key = describe_key(spki);
        preview_doc_field_styled(doc, _("Subject"), name, PREVIEW_STYLE_BOLD);
        preview_doc_field(doc, _("Public key"), key);
        g_free(key);
        g_free(name);
    }
}

gboolean cert_view_append(PreviewDoc* doc, const guchar* data, gsize len, gint64 now,
                          GError** error) {
    GArray* blocks;
    if (len > 1 && data[0] == 0x30 && data[1] >= 0x81 && data[1] <= 0x84) {
        /* DER certificate (a SEQUENCE longer than 127 bytes) */
        blocks = g_array_new(FALSE, TRUE, sizeof(PemBlock));
        g_array_set_clear_func(blocks, pem_block_clear);
        PemBlock b = { g_strdup("CERTIFICATE"), g_memdup2(data, len), len };
        g_array_append_val(blocks, b);
    } else {
        blocks = pem_blocks((const gchar*)data, len);
    }
    if (blocks->len == 0) {
        g_array_free(blocks, TRUE);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("no certificate or key found"));
        return FALSE;
    }

    guint n_certs = 0;
    for (guint i = 0; i < blocks->len; i++) {
        const gchar* label = g_array_index(blocks, PemBlock, i).label;
        n_certs += (strcmp(label, "CERTIFICATE") == 0 || strcmp(label, "TRUSTED CERTIFICATE") == 0);
    }
    if (n_certs > 1) {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM,
                            n_certs > CERT_VIEW_MAX_DETAILED ? _("%u certificates (the first ones in detail)\n")
                                                             : _("%u certificates\n"),
                            n_certs);
    }

    guint cert_index = 0;
    gboolean any = FALSE;
    for (guint i = 0; i < blocks->len; i++) {
        const PemBlock* b = &g_array_index(blocks, PemBlock, i);
        gboolean is_cert = strcmp(b->label, "CERTIFICATE") == 0 || strcmp(b->label, "TRUSTED CERTIFICATE") == 0;
        if (is_cert) {
            cert_index++;
            g_autoptr(X509Info) info = x509_parse(b->der, b->len, NULL);
            if (cert_index > CERT_VIEW_MAX_DETAILED) {
                if (info != NULL) {
                    gchar* until = format_date(info->not_after);
                    gchar* line = g_strdup_printf(_("%1$u. %2$s — until %3$s"), cert_index,
                                                  info->subject_cn != NULL ? info->subject_cn : info->subject, until);
                    if (cert_index == CERT_VIEW_MAX_DETAILED + 1) {
                        preview_doc_section(doc, _("Other certificates"));
                    }
                    preview_doc_item(doc, line);
                    g_free(line);
                    g_free(until);
                }
                any |= info != NULL;
                continue;
            }
            gchar* title = n_certs > 1 ? g_strdup_printf(_("Certificate %1$u of %2$u"), cert_index, n_certs)
                                       : g_strdup(_("Certificate"));
            preview_doc_section(doc, title);
            g_free(title);
            if (info == NULL) {
                preview_doc_note(doc, _("Damaged certificate: it cannot be decoded."));
                continue;
            }
            append_certificate(doc, info, now);
            any = TRUE;
            continue;
        }
        gchar* key = key_kind(b->label);
        if (key != NULL) {
            preview_doc_section(doc, key);
            gchar* alg = strcmp(b->label, "PRIVATE KEY") == 0 ? pkcs8_algorithm(b->der, b->len) : NULL;
            preview_doc_field(doc, _("Algorithm"), alg);
            preview_doc_note(doc, _("The key itself is not shown."));
            g_free(alg);
            g_free(key);
            any = TRUE;
            continue;
        }
        if (strcmp(b->label, "PUBLIC KEY") == 0) {
            preview_doc_section(doc, _("Public key"));
            Der all = { b->der, b->len };
            Der spki;
            if (der_expect(&all, TAG_SEQUENCE, &spki)) {
                gchar* desc = describe_key(spki);
                preview_doc_field(doc, _("Algorithm"), desc);
                g_free(desc);
            }
            any = TRUE;
            continue;
        }
        if (strcmp(b->label, "CERTIFICATE REQUEST") == 0 || strcmp(b->label, "NEW CERTIFICATE REQUEST") == 0) {
            preview_doc_section(doc, _("Certificate request"));
            append_request(doc, b->der, b->len);
            any = TRUE;
            continue;
        }
        preview_doc_section(doc, b->label);
        any = TRUE;
    }
    g_array_free(blocks, TRUE);
    if (!any) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a valid X.509 certificate"));
        return FALSE;
    }
    return TRUE;
}

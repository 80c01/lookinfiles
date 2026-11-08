#ifndef CERT_VIEW_H
#define CERT_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* Certificates and keys for the preview: PEM files (any number of blocks:
 * chains, CA bundles, keys, certificate requests) and DER certificates.
 * X.509 is decoded here (DER / ASN.1), with no TLS library: subject, issuer,
 * validity (coloured: valid, expiring soon, expired), serial number, key type
 * and size, signature algorithm, alternative names, CA flag, key usages and
 * SHA-256 / SHA-1 fingerprints. Private keys are only named, their content is
 * never shown. Pure GLib, unit-tested. */

typedef struct {
    gchar* subject;          /* "CN=example.org, O=Example, C=US" */
    gchar* subject_cn;       /* common name, or NULL */
    gchar* issuer;
    gchar* serial;           /* hex, colon separated */
    gint64 not_before;       /* Unix time */
    gint64 not_after;
    gchar* key;              /* "RSA 2048 bits", "EC P-256", "Ed25519" */
    gchar* signature;        /* "SHA-256 with RSA" */
    GPtrArray* alt_names;    /* "DNS:example.org", "IP:192.0.2.1", "email:a@b" */
    gint is_ca;              /* basicConstraints: 1 CA, 0 not, -1 absent */
    gchar* key_usage;        /* "Digital Signature, Key Encipherment" or NULL */
    gchar* ext_key_usage;    /* "TLS Web Server Authentication" or NULL */
    gchar* sha256;           /* fingerprints, hex colon separated */
    gchar* sha1;
    guint version;           /* 1..3 */
} X509Info;

/* Decode a DER certificate; NULL with error set if it is not one */
X509Info* x509_parse(const guchar* der, gsize len, GError** error);
void x509_info_free(X509Info* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(X509Info, x509_info_free)

/* Append the description of a PEM text or DER file (data, len) to doc,
 * validity computed at time now (Unix time). FALSE (doc unchanged) with error
 * set if there is no certificate or key in it. */
gboolean cert_view_append(PreviewDoc* doc, const guchar* data, gsize len, gint64 now,
                          GError** error);

/* Most certificates described in detail (a CA bundle has 140) */
#define CERT_VIEW_MAX_DETAILED 20

#endif /* CERT_VIEW_H */

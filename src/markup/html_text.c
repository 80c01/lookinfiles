/* HTML text helpers (see html_text.h) */
#include "markup/html_text.h"
#include <ctype.h>
#include <string.h>

/* Named entities (HTML names are case-sensitive: &Delta; is not &delta;) */
static const struct {
    const gchar* name;
    const gchar* text;
} ENTITIES[] = {
    /* XML / Basic */
    {"amp", "&"},
    {"lt", "<"},
    {"gt", ">"},
    {"quot", "\""},
    {"apos", "'"},
    /* Spaces */
    {"nbsp", " "},
    {"ensp", " "},
    {"emsp", "  "},
    {"thinsp", " "},
    /* Punctuation & Typography */
    {"mdash", "—"},
    {"ndash", "–"},
    {"hellip", "…"},
    {"bull", "•"},
    {"middot", "·"},
    {"copy", "©"},
    {"reg", "®"},
    {"trade", "™"},
    {"euro", "€"},
    {"pound", "£"},
    {"yen", "¥"},
    {"cent", "¢"},
    {"deg", "°"},
    {"plusmn", "±"},
    {"times", "×"},
    {"divide", "÷"},
    {"minus", "−"},
    {"lsquo", "‘"},
    {"rsquo", "’"},
    {"ldquo", "“"},
    {"rdquo", "”"},
    {"laquo", "«"},
    {"raquo", "»"},
    {"check", "✓"},
    {"cross", "✗"},
    {"star", "★"},
    {"heart", "♥"},
    {"sect", "§"},
    {"para", "¶"},
    {"dagger", "†"},
    {"Dagger", "‡"},
    {"permil", "‰"},
    {"lowast", "∗"},
    /* Math & Logic */
    {"ge", "≥"},
    {"le", "≤"},
    {"ne", "≠"},
    {"approx", "≈"},
    {"equiv", "≡"},
    {"infin", "∞"},
    {"empty", "∅"},
    {"sum", "∑"},
    {"prod", "∏"},
    {"radic", "√"},
    {"part", "∂"},
    {"int", "∫"},
    {"nabla", "∇"},
    {"isin", "∈"},
    {"notin", "∉"},
    /* Arrows */
    {"larr", "←"},
    {"rarr", "→"},
    {"uarr", "↑"},
    {"darr", "↓"},
    {"harr", "↔"},
    {"lArr", "⇐"},
    {"rArr", "⇒"},
    {"hArr", "⇔"},
    /* Greek Letters */
    {"alpha", "α"},
    {"beta", "β"},
    {"gamma", "γ"},
    {"delta", "δ"},
    {"epsilon", "ε"},
    {"theta", "θ"},
    {"lambda", "λ"},
    {"mu", "μ"},
    {"pi", "π"},
    {"sigma", "σ"},
    {"omega", "ω"},
    {"Delta", "Δ"},
    {"Omega", "Ω"},
    {"Sigma", "Σ"},
    {"Pi", "Π"},
};

/* Exact name first, then any case (&AMP; and the like in hand-written files) */
static const gchar* decode_named_entity(const gchar* name, gint len) {
    for (gint pass = 0; pass < 2; pass++) {
        for (gsize i = 0; i < G_N_ELEMENTS(ENTITIES); i++) {
            const gchar* e = ENTITIES[i].name;
            if ((gint)strlen(e) != len) continue;
            if (pass == 0 ? strncmp(name, e, len) == 0 : g_ascii_strncasecmp(name, e, len) == 0) {
                return ENTITIES[i].text;
            }
        }
    }
    return NULL;
}

void html_text_append_entity(GString* out, const gchar** ptr) {
    const gchar* p = *ptr; /* points to '&' */
    const gchar* semi = strchr(p, ';');
    if (semi == NULL || semi - p > 12) {
        g_string_append_c(out, '&');
        (*ptr)++;
        return;
    }

    gint elen = (gint)(semi - p - 1);
    const gchar* ename = p + 1;

    if (ename[0] == '#') {
        gunichar c = 0;
        if (ename[1] == 'x' || ename[1] == 'X') {
            c = (gunichar)g_ascii_strtoull(ename + 2, NULL, 16);
        } else {
            c = (gunichar)g_ascii_strtoull(ename + 1, NULL, 10);
        }
        if (c > 0 && g_unichar_validate(c)) {
            gchar buf[7] = {0};
            gint ulen = g_unichar_to_utf8(c, buf);
            buf[ulen] = '\0';
            g_string_append(out, buf);
            *ptr = semi + 1;
            return;
        }
    } else {
        const gchar* decoded = decode_named_entity(ename, elen);
        if (decoded != NULL) {
            g_string_append(out, decoded);
            *ptr = semi + 1;
            return;
        }
    }

    /* Fallback */
    g_string_append_len(out, p, semi - p + 1);
    *ptr = semi + 1;
}

gchar* html_text_attr(const gchar* tag_str, const gchar* attr_name) {
    if (tag_str == NULL || attr_name == NULL) return NULL;
    gsize alen = strlen(attr_name);
    const gchar* p = tag_str;
    while (*p != '\0' && *p != '>') {
        if ((p == tag_str || isspace((unsigned char)*(p - 1))) &&
            g_ascii_strncasecmp(p, attr_name, alen) == 0) {
            const gchar* eq = p + alen;
            while (*eq == ' ' || *eq == '\t') eq++;
            if (*eq == '=') {
                eq++;
                while (*eq == ' ' || *eq == '\t') eq++;
                if (*eq == '"' || *eq == '\'') {
                    gchar q = *eq;
                    eq++;
                    const gchar* qend = strchr(eq, q);
                    if (qend != NULL) {
                        return g_strndup(eq, qend - eq);
                    }
                } else {
                    const gchar* vend = eq;
                    while (*vend != '\0' && !isspace((unsigned char)*vend) && *vend != '>' && *vend != '/') vend++;
                    return g_strndup(eq, vend - eq);
                }
            }
        }
        p++;
    }
    return NULL;
}

const gchar* html_text_find_case(const gchar* haystack, const gchar* needle, const gchar* limit) {
    if (haystack == NULL || needle == NULL) return NULL;
    gsize nlen = strlen(needle);
    const gchar* h = haystack;
    while (*h != '\0' && (limit == NULL || h + nlen <= limit)) {
        if (g_ascii_strncasecmp(h, needle, nlen) == 0) return h;
        h++;
    }
    return NULL;
}

gchar* html_text_strip(const gchar* src) {
    if (src == NULL) return g_strdup("");
    GString* res = g_string_new(NULL);
    const gchar* p = src;
    gboolean last_space = TRUE;
    while (*p != '\0') {
        if (*p == '<') {
            const gchar* close = strchr(p, '>');
            if (close != NULL) {
                p = close + 1;
            } else {
                p++;
            }
            continue;
        }
        if (*p == '&') {
            html_text_append_entity(res, &p);
            last_space = FALSE;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            if (!last_space) {
                g_string_append_c(res, ' ');
                last_space = TRUE;
            }
            p++;
            continue;
        }
        g_string_append_c(res, *p);
        last_space = FALSE;
        p++;
    }
    if (res->len > 0 && res->str[res->len - 1] == ' ') {
        g_string_truncate(res, res->len - 1);
    }
    return g_string_free(res, FALSE);
}


/* Extract language name from class="language-..." or class="lang-..." */
gchar* html_text_code_lang(const gchar* tag_str) {
    gchar* cls = html_text_attr(tag_str, "class");
    if (cls == NULL) return NULL;
    const gchar* p = cls;
    gchar* lang = NULL;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (g_str_has_prefix(p, "language-") || g_str_has_prefix(p, "lang-")) {
            const gchar* lstart = strchr(p, '-') + 1;
            const gchar* lend = lstart;
            while (*lend != '\0' && !isspace((unsigned char)*lend) && *lend != '"' && *lend != '\'') lend++;
            if (lend > lstart) {
                lang = g_strndup(lstart, lend - lstart);
                break;
            }
        }
        while (*p != '\0' && !isspace((unsigned char)*p)) p++;
    }
    g_free(cls);
    return lang;
}

/* Extract <meta name="..." content="..."> */
gchar* html_text_meta(const gchar* html, const gchar* name) {
    const gchar* p = html;
    while ((p = html_text_find_case(p, "<meta", NULL)) != NULL) {
        const gchar* gt = strchr(p, '>');
        if (gt == NULL) break;
        gchar* tag = g_strndup(p, gt - p + 1);
        gchar* mname = html_text_attr(tag, "name");
        if (mname == NULL) {
            mname = html_text_attr(tag, "property");
        }
        if (mname != NULL && g_ascii_strcasecmp(mname, name) == 0) {
            gchar* cont = html_text_attr(tag, "content");
            g_free(mname);
            g_free(tag);
            return cont;
        }
        g_free(mname);
        g_free(tag);
        p = gt + 1;
    }
    return NULL;
}

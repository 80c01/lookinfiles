/* Jupyter notebooks to Markdown (see notebook.h). */
#include "formats/notebook.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#include "formats/json_view.h"

gchar* notebook_strip_ansi(const gchar* text) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p++) {
        if (*p == '\x1b' && p[1] == '[') {
            p += 2;
            while (*p != '\0' && !g_ascii_isalpha(*p)) {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            continue;
        }
        g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

/* Fence that does not appear in the text */
static const gchar* fence_for(const gchar* text) {
    return strstr(text, "```") != NULL ? "~~~~" : "```";
}

/* Fenced block of text (language may be ""), cut to max_lines */
static void append_block(GString* md, const gchar* text, const gchar* language, guint max_lines) {
    gchar* clean = notebook_strip_ansi(text);
    const gchar* fence = fence_for(clean);
    g_string_append_printf(md, "%s%s\n", fence, language);
    guint lines = 0;
    const gchar* p = clean;
    while (*p != '\0' && lines < max_lines) {
        const gchar* nl = strchr(p, '\n');
        gsize n = nl != NULL ? (gsize)(nl - p) : strlen(p);
        g_string_append_len(md, p, (gssize)n);
        g_string_append_c(md, '\n');
        lines++;
        p += n + (nl != NULL);
    }
    if (*p != '\0') {
        guint more = 0;
        for (const gchar* q = p; *q != '\0'; q++) {
            more += (*q == '\n');
        }
        g_string_append_printf(md, ngettext("… (%u more line)\n", "… (%u more lines)\n", more + 1), more + 1);
    }
    g_string_append_printf(md, "%s\n\n", fence);
    g_free(clean);
}

/* Base64 data of an image output, with the line breaks Jupyter adds removed */
static gchar* image_data(const JsonValue* v) {
    gchar* data = json_value_dup_text(v);
    if (data == NULL) {
        return NULL;
    }
    gchar* w = data;
    for (const gchar* r = data; *r != '\0'; r++) {
        if (*r != '\n' && *r != '\r' && *r != ' ') {
            *w++ = *r;
        }
    }
    *w = '\0';
    return data;
}

gchar* notebook_data_uri(const gchar* mime, const gchar* b64) {
    GString* uri = g_string_new("data:");
    g_string_append_printf(uri, "%s;base64,", mime);
    for (const gchar* r = b64; *r != '\0'; r++) {
        if (!g_ascii_isspace(*r)) g_string_append_c(uri, *r);
    }
    return g_string_free(uri, FALSE);
}

gchar* notebook_inline_attachments(const gchar* source, NotebookAttachmentFunc func, gpointer data) {
    static const gchar PREFIX[] = "attachment:";
    GString* out = g_string_new(NULL);
    const gchar* p = source;
    for (const gchar* hit = strstr(p, PREFIX); hit != NULL; hit = strstr(p, PREFIX)) {
        const gchar* name = hit + strlen(PREFIX);
        gsize len = strcspn(name, ")\"' \t\n>");
        gchar* escaped = g_strndup(name, len);
        gchar* plain = g_uri_unescape_string(escaped, NULL);
        gchar* uri = func(plain != NULL ? plain : escaped, data);
        g_string_append_len(out, p, hit - p);
        if (uri != NULL) {
            g_string_append(out, uri);
        } else {
            g_string_append_len(out, hit, (gssize)(name + len - hit));
        }
        g_free(uri);
        g_free(plain);
        g_free(escaped);
        p = name + len;
    }
    g_string_append(out, p);
    return g_string_free(out, FALSE);
}

/* The data: URI of attachment name of the cell's attachments (data) */
static gchar* json_attachment(const gchar* name, gpointer data) {
    const JsonValue* bundle = json_value_get((const JsonValue*)data, name);
    for (guint i = 0; i < json_value_get_length(bundle); i++) {
        const JsonValue* member = json_value_get_item(bundle, i);
        gchar* mime = json_value_dup_key(member);
        gchar* uri = NULL;
        if (mime != NULL && g_str_has_prefix(mime, "image/")) {
            gchar* b64 = json_value_dup_text(member);
            if (b64 != NULL && strlen(b64) <= NOTEBOOK_MAX_IMAGE) uri = notebook_data_uri(mime, b64);
            g_free(b64);
        }
        g_free(mime);
        if (uri != NULL) return uri;
    }
    return NULL;
}

static void append_rich_output(GString* md, const JsonValue* data, const gchar* label) {
    static const gchar* const IMAGES[] = { "image/png", "image/jpeg", "image/gif" };
    for (gsize i = 0; i < G_N_ELEMENTS(IMAGES); i++) {
        const JsonValue* img = json_value_get(data, IMAGES[i]);
        if (img != NULL) {
            gchar* b64 = image_data(img);
            if (b64 != NULL && strlen(b64) <= NOTEBOOK_MAX_IMAGE) {
                g_string_append_printf(md, "![output](data:%s;base64,%s)\n\n", IMAGES[i], b64);
                g_free(b64);
                return;
            }
            g_free(b64);
            g_string_append_printf(md, "*%s*\n\n", _("(image too large to show)"));
            return;
        }
    }
    const JsonValue* svg = json_value_get(data, "image/svg+xml");
    if (svg != NULL) {
        gchar* text = json_value_dup_text(svg);
        if (text != NULL && strlen(text) <= NOTEBOOK_MAX_IMAGE) {
            gchar* b64 = g_base64_encode((const guchar*)text, strlen(text));
            g_string_append_printf(md, "![output](data:image/svg+xml;base64,%s)\n\n", b64);
            g_free(b64);
            g_free(text);
            return;
        }
        g_free(text);
    }
    /* A formula (SymPy, IPython Math): shown by the Markdown renderer */
    const JsonValue* latex = json_value_get(data, "text/latex");
    if (latex != NULL) {
        gchar* text = json_value_dup_text(latex);
        gchar* tex = text != NULL ? g_strstrip(text) : NULL;
        if (tex != NULL && (tex[0] == '$' || g_str_has_prefix(tex, "\\[") || g_str_has_prefix(tex, "\\(") ||
                            g_str_has_prefix(tex, "\\begin{equation") || g_str_has_prefix(tex, "\\begin{align"))) {
            if (label != NULL) g_string_append_printf(md, "*%s*\n\n", label);
            /* $x$ alone on its line: as display math, like Jupyter */
            gboolean inline_math = tex[0] == '$' && tex[1] != '$';
            g_string_append_printf(md, inline_math ? "$%s$\n\n" : "%s\n\n", tex);
            g_free(text);
            return;
        }
        g_free(text);
    }
    const JsonValue* markdown = json_value_get(data, "text/markdown");
    if (markdown != NULL) {
        gchar* text = json_value_dup_text(markdown);
        g_string_append_printf(md, "%s\n\n", text != NULL ? text : "");
        g_free(text);
        return;
    }
    const JsonValue* plain = json_value_get(data, "text/plain");
    if (plain != NULL) {
        gchar* text = json_value_dup_text(plain);
        if (label != NULL) {
            g_string_append_printf(md, "*%s*\n\n", label);
        }
        append_block(md, text != NULL ? text : "", "", NOTEBOOK_MAX_OUTPUT_LINES);
        g_free(text);
        return;
    }
    if (json_value_get(data, "text/html") != NULL) {
        g_string_append_printf(md, "*%s*\n\n", _("(HTML output)"));
    }
}

static void append_outputs(GString* md, const JsonValue* outputs, const gchar* count) {
    for (guint i = 0; i < json_value_get_length(outputs); i++) {
        const JsonValue* out = json_value_get_item(outputs, i);
        gchar* type = json_value_dup_string(json_value_get(out, "output_type"));
        if (type == NULL) {
            continue;
        }
        if (strcmp(type, "stream") == 0) {
            gchar* name = json_value_dup_string(json_value_get(out, "name"));
            gchar* text = json_value_dup_text(json_value_get(out, "text"));
            if (g_strcmp0(name, "stderr") == 0) {
                g_string_append(md, "*stderr*\n\n");
            }
            append_block(md, text != NULL ? text : "", "", NOTEBOOK_MAX_OUTPUT_LINES);
            g_free(text);
            g_free(name);
        } else if (strcmp(type, "execute_result") == 0 || strcmp(type, "display_data") == 0 ||
                   strcmp(type, "pyout") == 0) {
            const JsonValue* data = json_value_get(out, "data");
            gchar* label = (strcmp(type, "display_data") != 0 && count != NULL)
                               ? g_strdup_printf("Out [%s]:", count) : NULL;
            append_rich_output(md, data != NULL ? data : out, label);
            g_free(label);
        } else if (strcmp(type, "error") == 0 || strcmp(type, "pyerr") == 0) {
            GString* err = g_string_new(NULL);
            const JsonValue* tb = json_value_get(out, "traceback");
            for (guint k = 0; k < json_value_get_length(tb); k++) {
                gchar* line = json_value_dup_string(json_value_get_item(tb, k));
                g_string_append_printf(err, "%s\n", line != NULL ? line : "");
                g_free(line);
            }
            if (err->len == 0) {
                gchar* ename = json_value_dup_string(json_value_get(out, "ename"));
                gchar* evalue = json_value_dup_string(json_value_get(out, "evalue"));
                g_string_append_printf(err, "%s: %s", ename != NULL ? ename : "Error", evalue != NULL ? evalue : "");
                g_free(evalue);
                g_free(ename);
            }
            append_block(md, err->str, "", NOTEBOOK_MAX_OUTPUT_LINES);
            g_string_free(err, TRUE);
        }
        g_free(type);
    }
}

gchar* notebook_to_markdown(const gchar* text, gsize len, NotebookInfo* info, GError** error) {
    g_autoptr(JsonValue) root = json_value_parse(text, len, error);
    if (root == NULL) {
        return NULL;
    }
    const JsonValue* cells = json_value_get(root, "cells");
    if (cells == NULL) {
        /* nbformat 3: cells inside worksheets */
        const JsonValue* sheets = json_value_get(root, "worksheets");
        cells = json_value_get(json_value_get_item(sheets, 0), "cells");
    }
    if (cells == NULL || json_value_get_type(cells) != JSON_VALUE_ARRAY) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a Jupyter notebook (no cells)"));
        return NULL;
    }
    const JsonValue* meta = json_value_get(root, "metadata");
    gchar* language = json_value_dup_string(json_value_get(json_value_get(meta, "language_info"), "name"));
    if (language == NULL) {
        language = json_value_dup_string(json_value_get(json_value_get(meta, "kernelspec"), "language"));
    }
    gchar* kernel = json_value_dup_string(json_value_get(json_value_get(meta, "kernelspec"), "display_name"));

    GString* md = g_string_new(NULL);
    guint n_code = 0;
    for (guint i = 0; i < json_value_get_length(cells); i++) {
        const JsonValue* cell = json_value_get_item(cells, i);
        gchar* type = json_value_dup_string(json_value_get(cell, "cell_type"));
        const JsonValue* src = json_value_get(cell, "source");
        if (src == NULL) {
            src = json_value_get(cell, "input"); /* nbformat 3 */
        }
        gchar* source = json_value_dup_text(src);
        if (g_strcmp0(type, "markdown") == 0 || g_strcmp0(type, "heading") == 0) {
            const JsonValue* attachments = json_value_get(cell, "attachments");
            gchar* shown = attachments != NULL && source != NULL
                               ? notebook_inline_attachments(source, json_attachment, (gpointer)attachments)
                               : NULL;
            g_string_append_printf(md, "%s\n\n", shown != NULL ? shown : source != NULL ? source : "");
            g_free(shown);
        } else if (g_strcmp0(type, "code") == 0) {
            n_code++;
            const JsonValue* count_v = json_value_get(cell, "execution_count");
            if (count_v == NULL) {
                count_v = json_value_get(cell, "prompt_number");
            }
            gchar* count = json_value_dup_string(count_v);
            if (count != NULL && strcmp(count, "null") == 0) {
                g_clear_pointer(&count, g_free);
            }
            g_string_append_printf(md, "*In [%s]:*\n\n", count != NULL ? count : " ");
            append_block(md, source != NULL ? source : "", language != NULL ? language : "", G_MAXUINT);
            append_outputs(md, json_value_get(cell, "outputs"), count);
            g_free(count);
        } else if (source != NULL) {
            append_block(md, source, "", G_MAXUINT);
        }
        g_free(source);
        g_free(type);
    }
    if (info != NULL) {
        info->n_cells = json_value_get_length(cells);
        info->n_code = n_code;
        info->language = language;
        info->kernel = kernel;
    } else {
        g_free(language);
        g_free(kernel);
    }
    return g_string_free(md, FALSE);
}

void notebook_info_clear(NotebookInfo* info) {
    g_clear_pointer(&info->language, g_free);
    g_clear_pointer(&info->kernel, g_free);
}

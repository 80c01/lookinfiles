/* Markdown parser (see markdown_parse.h). */
#include "markup/markdown_parse.h"

#include "core/i18n.h"
#include "markup/markdown_table.h"
#include "media/image_utils.h"

#include <ctype.h>
#include <string.h>

/* The character at p, whole (a multi-byte character is not cut) */
static gsize char_len(const gchar* p) {
    return (gsize)(g_utf8_next_char(p) - p);
}

/* A link's text, recorded with its url */
static void append_link(MdOut* out, const gchar* text, const gchar* url) {
    gint start = md_out_offset(out);
    md_out_append(out, text, -1);
    md_out_tag_from(out, start, "md_link");
    md_out_link_from(out, start, url);
}

/* ![alt](url): the picture (read now), else its alt text with a glyph */
static void append_image(MdOut* out, const gchar* alt, const gchar* url, const gchar* doc_dir) {
    gchar* img_path = NULL;
    if (g_path_is_absolute(url)) {
        img_path = g_strdup(url);
    } else if (doc_dir != NULL) {
        img_path = g_build_filename(doc_dir, url, NULL);
    }
    /* Natural size, shrunk to the view width (VS Code) */
    GdkPixbuf* pix = pixbuf_from_data_uri(url, FIT_IMAGE_MAX_WIDTH);
    if (pix == NULL && img_path != NULL && !g_str_has_prefix(url, "data:") &&
        g_file_test(img_path, G_FILE_TEST_EXISTS)) {
        pix = pixbuf_from_file(img_path, FIT_IMAGE_MAX_WIDTH);
    }
    g_free(img_path);
    if (pix != NULL) {
        md_out_add_image(out, G_OBJECT(pix));
        g_object_unref(pix);
        return;
    }
    gint start = md_out_offset(out);
    md_out_add_glyph(out, "image", NULL);
    gchar* placeholder = g_strdup_printf("[%s]", alt);
    md_out_append(out, placeholder, -1);
    g_free(placeholder);
    md_out_tag_from(out, start, "md_image");
}

static void append_inline(MdOut* out, const gchar* text, const gchar* doc_dir);

/* inner rendered with tag over it */
static void append_inline_tagged(MdOut* out, const gchar* inner, gsize len, const gchar* tag,
                                 const gchar* doc_dir) {
    gchar* copy = g_strndup(inner, len);
    gint start = md_out_offset(out);
    append_inline(out, copy, doc_dir);
    md_out_tag_from(out, start, tag);
    g_free(copy);
}

static const struct { const gchar* entity; const gchar* text; } ENTITIES[] = {
    {"&quot;", "\""}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}, {"&#39;", "'"},
};

/* Inline formatting: emphasis, code, links, pictures, autolinks, entities */
static void append_inline(MdOut* out, const gchar* text, const gchar* doc_dir) {
    const gchar* p = text;
    while (*p != '\0') {
        /* Backslash escape */
        if (*p == '\\' && p[1] != '\0') {
            p++;
            md_out_append(out, p, (gssize)char_len(p));
            p = g_utf8_next_char(p);
            continue;
        }

        /* Image: ![alt](url) */
        if (*p == '!' && p[1] == '[') {
            const gchar* alt_end = strchr(p + 2, ']');
            if (alt_end && alt_end[1] == '(') {
                const gchar* url_end = strchr(alt_end + 2, ')');
                if (url_end) {
                    gchar* alt = g_strndup(p + 2, alt_end - (p + 2));
                    gchar* url = g_strndup(alt_end + 2, url_end - (alt_end + 2));
                    append_image(out, alt, url, doc_dir);
                    g_free(alt);
                    g_free(url);
                    p = url_end + 1;
                    continue;
                }
            }
        }

        /* Link: [label](url) */
        if (*p == '[') {
            const gchar* label_end = strchr(p + 1, ']');
            if (label_end && label_end[1] == '(') {
                const gchar* url_end = strchr(label_end + 2, ')');
                if (url_end) {
                    gchar* label = g_strndup(p + 1, label_end - (p + 1));
                    gchar* url = g_strndup(label_end + 2, url_end - (label_end + 2));
                    append_link(out, (label[0] != '\0') ? label : url, url);
                    g_free(label);
                    g_free(url);
                    p = url_end + 1;
                    continue;
                }
            }
        }

        /* Inline code: `code` or ``code`` */
        if (*p == '`') {
            gint bcount = (p[1] == '`') ? 2 : 1;
            const gchar* code_start = p + bcount;
            const gchar* code_end = (bcount == 2) ? strstr(code_start, "``") : strchr(code_start, '`');
            if (code_end) {
                gint start = md_out_offset(out);
                md_out_append(out, code_start, code_end - code_start);
                md_out_tag_from(out, start, "md_inline_code");
                p = code_end + bcount;
                continue;
            }
        }

        /* Strikethrough: ~~text~~ */
        if (p[0] == '~' && p[1] == '~') {
            const gchar* strike_end = strstr(p + 2, "~~");
            if (strike_end) {
                append_inline_tagged(out, p + 2, (gsize)(strike_end - (p + 2)), "md_strike", doc_dir);
                p = strike_end + 2;
                continue;
            }
        }

        /* Bold + Italic: ***text*** or ___text___ */
        if (strncmp(p, "***", 3) == 0 || strncmp(p, "___", 3) == 0) {
            const gchar* delim = (p[0] == '*') ? "***" : "___";
            const gchar* bi_end = strstr(p + 3, delim);
            if (bi_end) {
                append_inline_tagged(out, p + 3, (gsize)(bi_end - (p + 3)), "md_bold_italic", doc_dir);
                p = bi_end + 3;
                continue;
            }
        }

        /* Bold: **text** or __text__ */
        if (strncmp(p, "**", 2) == 0 || strncmp(p, "__", 2) == 0) {
            const gchar* delim = (p[0] == '*') ? "**" : "__";
            const gchar* b_end = strstr(p + 2, delim);
            if (b_end) {
                append_inline_tagged(out, p + 2, (gsize)(b_end - (p + 2)), "md_bold", doc_dir);
                p = b_end + 2;
                continue;
            }
        }

        /* Italic: *text* or _text_ */
        if (*p == '*' || *p == '_') {
            gchar delim = *p;
            /* For underscore, ignore if in middle of word (e.g. snake_case) */
            gboolean skip = (delim == '_' && p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_'));
            if (!skip) {
                const gchar* it_end = strchr(p + 1, delim);
                if (it_end && it_end != p + 1 && (delim != '_' || !isalnum((unsigned char)it_end[1]))) {
                    append_inline_tagged(out, p + 1, (gsize)(it_end - (p + 1)), "md_italic", doc_dir);
                    p = it_end + 1;
                    continue;
                }
            }
        }

        /* Autolink: <http://...> or <https://...>; <br>; other tags dropped */
        if (*p == '<') {
            const gchar* closing = strchr(p + 1, '>');
            if (closing) {
                if (g_str_has_prefix(p + 1, "http://") || g_str_has_prefix(p + 1, "https://") ||
                    g_str_has_prefix(p + 1, "mailto:")) {
                    gchar* url = g_strndup(p + 1, closing - (p + 1));
                    append_link(out, url, url);
                    g_free(url);
                } else if (g_ascii_strncasecmp(p, "<br>", 4) == 0 || g_ascii_strncasecmp(p, "<br/>", 5) == 0 ||
                           g_ascii_strncasecmp(p, "<br />", 6) == 0) {
                    md_out_append(out, "\n", 1);
                }
                p = closing + 1;
                continue;
            }
        }

        /* HTML entities */
        if (*p == '&') {
            gboolean found = FALSE;
            for (gsize k = 0; k < G_N_ELEMENTS(ENTITIES) && !found; k++) {
                if (g_str_has_prefix(p, ENTITIES[k].entity)) {
                    md_out_append(out, ENTITIES[k].text, -1);
                    p += strlen(ENTITIES[k].entity);
                    found = TRUE;
                }
            }
            if (found) {
                continue;
            }
        }

        /* Plain text up to the next special character */
        const gchar* next = p;
        while (*next != '\0' && strchr("\\![`~*_<&", *next) == NULL) {
            next++;
        }
        if (next > p) {
            md_out_append(out, p, next - p);
            p = next;
        } else {
            md_out_append(out, p, 1); /* a special character starting nothing */
            p++;
        }
    }
}

static gboolean is_hr_line(const gchar* line) {
    const gchar* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '-' && *p != '*' && *p != '_') return FALSE;
    gchar c = *p;
    gint count = 0;
    while (*p != '\0') {
        if (*p == c) count++;
        else if (*p != ' ' && *p != '\t') return FALSE;
        p++;
    }
    return count >= 3;
}

/* Lines that continue a paragraph: not blank and not the start of another block */
static gboolean is_paragraph_continuation(const gchar* line) {
    const gchar* t = line;
    while (*t == ' ') t++;
    if (*t == '\0' || *t == '>' || *t == '|' || is_hr_line(line) ||
        g_str_has_prefix(t, "```") || g_str_has_prefix(t, "~~~")) {
        return FALSE;
    }
    if (*t == '#') {
        gint n = 0;
        while (t[n] == '#') n++;
        if (n <= 6 && (t[n] == ' ' || t[n] == '\0')) return FALSE;
    }
    if ((*t == '-' || *t == '*' || *t == '+') && t[1] == ' ') return FALSE;
    if (*t == '=' || *t == '-') {
        const gchar* c = t;
        while (*c == *t) c++;
        while (*c == ' ') c++;
        if (*c == '\0') return FALSE; /* setext underline */
    }
    if (isdigit((unsigned char)*t)) {
        const gchar* d = t;
        while (isdigit((unsigned char)*d)) d++;
        if ((*d == '.' || *d == ')') && d[1] == ' ') return FALSE;
    }
    return TRUE;
}

/* GitHub alert marker ("[!NOTE]"...) of a quote line: title, glyph (icons.h) and tag */
typedef struct { const gchar* marker; const gchar* title; const gchar* glyph; const gchar* tag; } MdAlert;
static const MdAlert MD_ALERTS[] = {
    {"[!NOTE]", "Note", "note", "md_alert_note"},
    {"[!TIP]", "Tip", "tip", "md_alert_tip"},
    {"[!IMPORTANT]", "Important", "important", "md_alert_important"},
    {"[!WARNING]", "Warning", "warning", "md_alert_warning"},
    {"[!CAUTION]", "Caution", "caution", "md_alert_caution"},
};

/* Diagrams and math need a renderer (JavaScript, TeX): their source is
 * shown, with a label saying what it is */
static const struct { const gchar* lang; const gchar* label; } SOURCE_LABELS[] = {
    { "mermaid", N_("Mermaid diagram (source)") }, { "plantuml", N_("PlantUML diagram (source)") },
    { "dot", N_("Graphviz diagram (source)") }, { "graphviz", N_("Graphviz diagram (source)") },
    { "math", N_("Math (TeX source)") }, { "latex", N_("Math (TeX source)") },
    { "tex", N_("TeX (source)") }, { "katex", N_("Math (TeX source)") },
};

/* Text of a list item: its first line plus the following continuation lines
 * (wrapped source lines, indented or lazy), joined with spaces like VS Code.
 * Advances *i to the last line used. Caller frees. */
static gchar* gather_item_text(gchar** lines, gint num_lines, gint* i, const gchar* first) {
    GString* text = g_string_new(first);
    while (*i + 1 < num_lines && is_paragraph_continuation(lines[*i + 1])) {
        const gchar* next = lines[*i + 1];
        while (*next == ' ' || *next == '\t') next++;
        g_string_append_c(text, ' ');
        g_string_append(text, next);
        (*i)++;
    }
    return g_string_free(text, FALSE);
}

typedef struct {
    MdOut* out;
    gchar** lines;
    gint num_lines;
    const gchar* doc_dir;
    gboolean widget_tables;
    gboolean prev_was_empty;
    const gchar* alert_tag; /* inside a GitHub alert: its colour tag */
    gboolean last_was_li;
    gint last_li_level;
    gboolean last_li_is_ordered;
} Parser;

/* Fenced code block from line i: a padded panel, highlighted like the
 * editor. Returns the closing fence line. */
static gint parse_fence(Parser* ps, gint i, const gchar* trim) {
    MdOut* out = ps->out;
    gchar fence = trim[0];
    gint fence_len = 0;
    while (trim[fence_len] == fence) fence_len++;
    const gchar* info = trim + fence_len;
    while (*info == ' ') info++;

    GString* code = g_string_new(NULL);
    gint j = i + 1;
    for (; j < ps->num_lines; j++) {
        const gchar* t = ps->lines[j];
        while (*t == ' ') t++;
        gint count = 0;
        while (t[count] == fence) count++;
        if (count >= fence_len) {
            break;
        }
        g_string_append(code, ps->lines[j]);
        g_string_append_c(code, '\n');
    }
    /* Space between the text and the panel, above and below */
    md_out_append_tagged(out, "\n", "md_block_gap");
    for (gsize k = 0; k < G_N_ELEMENTS(SOURCE_LABELS); k++) {
        gsize n = strlen(SOURCE_LABELS[k].lang);
        if (g_ascii_strncasecmp(info, SOURCE_LABELS[k].lang, n) == 0 && !g_ascii_isalnum(info[n])) {
            gchar* label = g_strdup_printf("%s\n", _(SOURCE_LABELS[k].label));
            md_out_append_tagged(out, label, "md_code_label");
            g_free(label);
            break;
        }
    }
    md_out_append_tagged(out, " \n", "md_code_pad");
    gint code_start = md_out_offset(out);
    md_out_append_tagged(out, code->str, "md_code_block");
    md_out_add_code(out, code_start, code->str, info);
    md_out_append_tagged(out, " \n", "md_code_pad");
    md_out_append_tagged(out, "\n", "md_block_gap");
    g_string_free(code, TRUE);
    return j;
}

/* text rendered inline, then a line break, all with tag; h1/h2 get a rule under */
static void append_heading(Parser* ps, const gchar* text, gint level) {
    gchar tag[16];
    g_snprintf(tag, sizeof tag, "md_h%d", level);
    gint start = md_out_offset(ps->out);
    append_inline(ps->out, text, ps->doc_dir);
    md_out_append(ps->out, "\n", 1);
    md_out_tag_from(ps->out, start, tag);
    if (level <= 2) {
        md_out_append_tagged(ps->out, " \n", "md_rule"); /* VS Code: border under h1/h2 */
    }
}

/* Quote line (tinted panel with a bar) or GitHub alert title */
static void parse_quote(Parser* ps, const gchar* trim) {
    MdOut* out = ps->out;
    const gchar* q_text = trim + 1;
    while (*q_text == ' ') q_text++;
    const MdAlert* alert = NULL;
    for (gsize k = 0; k < G_N_ELEMENTS(MD_ALERTS); k++) {
        if (g_str_has_prefix(q_text, MD_ALERTS[k].marker)) {
            alert = &MD_ALERTS[k];
        }
    }
    gint start = md_out_offset(out);
    if (alert != NULL) {
        ps->alert_tag = alert->tag;
        md_out_append_tagged(out, "▌ ", ps->alert_tag);
        md_out_add_glyph(out, alert->glyph, ps->alert_tag);
        gchar* title = g_strdup_printf("%s\n", alert->title);
        md_out_append_tagged(out, title, ps->alert_tag);
        g_free(title);
    } else {
        /* An alert's text keeps the normal colour: only the bar is coloured */
        md_out_append_tagged(out, "▌ ", ps->alert_tag != NULL ? ps->alert_tag : "md_quote_bar");
        append_inline(out, q_text, ps->doc_dir);
        md_out_append(out, "\n", 1);
    }
    md_out_tag_from(out, start, "md_quote");
}

/* Table from line i (header and separator rows checked). Returns its last line. */
static gint parse_table(Parser* ps, gint i) {
    GPtrArray* rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    g_ptr_array_add(rows, markdown_table_split_row(ps->lines[i])); /* header */
    GPtrArray* aligns = markdown_table_split_row(ps->lines[i + 1]);
    i += 2; /* skip header and separator row */
    while (i < ps->num_lines) {
        const gchar* r = ps->lines[i];
        while (*r == ' ') r++;
        if (r[0] != '|') break;
        g_ptr_array_add(rows, markdown_table_split_row(ps->lines[i]));
        i++;
    }
    guint max_cols = 0;
    for (guint r = 0; r < rows->len; r++) {
        max_cols = MAX(max_cols, ((GPtrArray*)g_ptr_array_index(rows, r))->len);
    }
    if (max_cols == 0) {
        g_ptr_array_unref(rows);
        g_ptr_array_unref(aligns);
    } else if (ps->widget_tables) {
        md_out_add_table(ps->out, rows, aligns, max_cols);
    } else {
        markdown_table_append_text(ps->out, rows, max_cols);
        g_ptr_array_unref(rows);
        g_ptr_array_unref(aligns);
    }
    return i - 1;
}

/* A list item: its marker (with marker_tag) and its text, the line with
 * line_tag (hanging indent of its kind and level) */
static void append_item(Parser* ps, gint* i, const gchar* marker, const gchar* marker_tag,
                        const gchar* text, const gchar* line_tag) {
    MdOut* out = ps->out;
    gint start = md_out_offset(out);
    md_out_append_tagged(out, marker, marker_tag);
    gchar* item_text = gather_item_text(ps->lines, ps->num_lines, i, text);
    append_inline(out, item_text, ps->doc_dir);
    g_free(item_text);
    md_out_tag_from(out, start, line_tag);
    md_out_append(out, "\n", 1);
    ps->prev_was_empty = FALSE;
    ps->last_was_li = TRUE;
}

/* List items of line i; FALSE if it is none */
static gboolean parse_list_item(Parser* ps, gint* i, const gchar* line) {
    gint leading_spaces = 0;
    while (line[leading_spaces] == ' ') leading_spaces++;
    const gchar* item = line + leading_spaces;
    gint level = MIN(leading_spaces / 2, 7);
    gchar line_tag[32];

    /* Task list item: - [ ] todo, - [x] done, - [/] or - [-] running */
    if ((item[0] == '-' || item[0] == '*' || item[0] == '+') && item[1] == ' ' && item[2] == '[' &&
        item[3] != '\0' && strchr(" xX/-", item[3]) && item[4] == ']' && item[5] == ' ') {
        gboolean done = (item[3] == 'x' || item[3] == 'X');
        gboolean running = (item[3] == '/' || item[3] == '-');
        g_snprintf(line_tag, sizeof line_tag, "md_li_task_d%d", level);
        append_item(ps, i, done ? "☑\t" : running ? "◧\t" : "☐\t",
                    done ? "md_list_task_done" : running ? "md_list_task_running" : "md_list_task_todo",
                    item + 6, line_tag);
        ps->last_li_level = level;
        ps->last_li_is_ordered = FALSE;
        return TRUE;
    }

    /* Unordered list item: - , * , + (the tab after the disc is untagged) */
    if ((item[0] == '-' || item[0] == '*' || item[0] == '+') && item[1] == ' ') {
        const gchar* bullet = (level % 3 == 0) ? "●" : (level % 3 == 1) ? "○" : "■";
        g_snprintf(line_tag, sizeof line_tag, "md_li_ul_d%d", level);
        gint start = md_out_offset(ps->out);
        md_out_append_tagged(ps->out, bullet, "md_list_disc");
        md_out_append(ps->out, "\t", 1);
        gchar* item_text = gather_item_text(ps->lines, ps->num_lines, i, item + 2);
        append_inline(ps->out, item_text, ps->doc_dir);
        g_free(item_text);
        md_out_tag_from(ps->out, start, line_tag);
        md_out_append(ps->out, "\n", 1);
        ps->prev_was_empty = FALSE;
        ps->last_was_li = TRUE;
        ps->last_li_level = level;
        ps->last_li_is_ordered = FALSE;
        return TRUE;
    }

    /* Ordered list item: 1. , 2. , etc. */
    if (isdigit((unsigned char)item[0])) {
        const gchar* odot = item;
        while (isdigit((unsigned char)*odot)) odot++;
        if ((*odot == '.' || *odot == ')') && odot[1] == ' ') {
            gchar* num = g_strdup_printf("%.*s\t", (gint)(odot - item + 1), item);
            g_snprintf(line_tag, sizeof line_tag, "md_li_ol_d%d", level);
            append_item(ps, i, num, "md_list_bullet", odot + 2, line_tag);
            g_free(num);
            ps->last_li_level = level;
            ps->last_li_is_ordered = TRUE;
            return TRUE;
        }
    }

    /* Indented continuation paragraph of a list item */
    if (ps->last_was_li && leading_spaces >= 2 && item[0] != '\0') {
        g_snprintf(line_tag, sizeof line_tag, "md_li_p_%s_d%d", ps->last_li_is_ordered ? "ol" : "ul",
                   ps->last_li_level);
        gint start = md_out_offset(ps->out);
        append_inline(ps->out, item, ps->doc_dir);
        md_out_tag_from(ps->out, start, line_tag);
        md_out_append(ps->out, "\n", 1);
        ps->prev_was_empty = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* Paragraph from line i: consecutive source lines join into one wrapped
 * paragraph (like VS Code); a line ending with two spaces or a backslash
 * breaks. Returns its last line. */
static gint parse_paragraph(Parser* ps, gint i) {
    GString* para = g_string_new(NULL);
    gint j = i;
    while (TRUE) {
        const gchar* ln = ps->lines[j];
        while (*ln == ' ') ln++;
        gsize len = strlen(ln);
        gboolean hard = (len >= 2 && ln[len - 1] == ' ' && ln[len - 2] == ' ') ||
                        (len >= 1 && ln[len - 1] == '\\');
        while (len > 0 && (ln[len - 1] == ' ' || (hard && ln[len - 1] == '\\'))) len--;
        g_string_append_len(para, ln, (gssize)len);
        if (j + 1 >= ps->num_lines || !is_paragraph_continuation(ps->lines[j + 1])) {
            break;
        }
        g_string_append_c(para, hard ? '\n' : ' ');
        j++;
    }
    append_inline(ps->out, para->str, ps->doc_dir);
    md_out_append(ps->out, "\n", 1);
    g_string_free(para, TRUE);
    return j;
}

/* TRUE if line i starts a table (a header row, then a separator row) */
static gboolean is_table_start(Parser* ps, gint i, const gchar* trim) {
    if (trim[0] != '|' || i + 1 >= ps->num_lines || strchr(ps->lines[i + 1], '|') == NULL) {
        return FALSE;
    }
    const gchar* sep = ps->lines[i + 1];
    while (*sep == ' ') sep++;
    gint dashes = 0;
    for (const gchar* c = sep; *c != '\0'; c++) {
        dashes += (*c == '-');
    }
    return sep[0] == '|' && dashes >= 3;
}

MdOut* markdown_parse(const gchar* markdown_text, const gchar* doc_path, gboolean is_preview,
                      const gchar* file_name, gint64 total_size, gboolean widget_tables) {
    MdOut* out = md_out_new();
    gchar* doc_dir = doc_path ? g_path_get_dirname(doc_path) : NULL;
    if (is_preview) {
        gchar* banner = g_strdup_printf(
            "  [ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total) — Display truncated\n\n",
            file_name ? file_name : "file", total_size);
        md_out_append_tagged(out, banner, "md_preview_banner");
        g_free(banner);
    }

    Parser ps = {0};
    ps.out = out;
    ps.lines = g_strsplit(markdown_text != NULL ? markdown_text : "", "\n", -1);
    ps.num_lines = (gint)g_strv_length(ps.lines);
    ps.doc_dir = doc_dir;
    ps.widget_tables = widget_tables;

    for (gint i = 0; i < ps.num_lines; i++) {
        const gchar* line = ps.lines[i];
        const gchar* trim = line;
        while (*trim == ' ') trim++;

        if (g_str_has_prefix(trim, "```") || g_str_has_prefix(trim, "~~~")) {
            ps.last_was_li = FALSE;
            ps.alert_tag = NULL;
            i = parse_fence(&ps, i, trim); /* closing fence (or end of text) */
            ps.prev_was_empty = TRUE;      /* the gap replaces the blank line that follows */
            continue;
        }

        /* ATX headings: # through ###### */
        if (trim[0] == '#') {
            gint h_level = 0;
            while (trim[h_level] == '#') h_level++;
            if (h_level <= 6 && (trim[h_level] == ' ' || trim[h_level] == '\0')) {
                ps.last_was_li = FALSE;
                ps.alert_tag = NULL;
                const gchar* h_text = trim + h_level;
                while (*h_text == ' ') h_text++;
                gchar* h_clean = g_strdup(h_text);
                gint h_len = (gint)strlen(h_clean);
                while (h_len > 0 && (h_clean[h_len - 1] == ' ' || h_clean[h_len - 1] == '#')) {
                    h_clean[--h_len] = '\0';
                }
                append_heading(&ps, h_clean, h_level);
                g_free(h_clean);
                ps.prev_was_empty = FALSE;
                continue;
            }
        }

        if (is_hr_line(line)) {
            ps.last_was_li = FALSE;
            ps.alert_tag = NULL;
            md_out_append_tagged(out, "\n", "md_gap");
            md_out_append_tagged(out, " \n", "md_hr");
            md_out_append_tagged(out, "\n", "md_gap");
            ps.prev_was_empty = FALSE;
            continue;
        }

        if (trim[0] == '>') {
            ps.last_was_li = FALSE;
            parse_quote(&ps, trim);
            ps.prev_was_empty = FALSE;
            continue;
        }
        ps.alert_tag = NULL;

        if (is_table_start(&ps, i, trim)) {
            ps.last_was_li = FALSE;
            i = parse_table(&ps, i);
            ps.prev_was_empty = FALSE;
            continue;
        }

        if (parse_list_item(&ps, &i, line)) {
            continue;
        }

        /* Setext heading: line followed by === or --- */
        if (trim[0] != '\0' && i + 1 < ps.num_lines) {
            const gchar* next_trim = ps.lines[i + 1];
            while (*next_trim == ' ') next_trim++;
            gboolean h1 = (next_trim[0] == '=' && next_trim[1] == '=');
            gboolean h2 = (next_trim[0] == '-' && next_trim[1] == '-' && !is_hr_line(next_trim));
            if (h1 || h2) {
                ps.last_was_li = FALSE;
                append_heading(&ps, trim, h1 ? 1 : 2);
                i++;
                ps.prev_was_empty = FALSE;
                continue;
            }
        }

        /* Blank line */
        if (trim[0] == '\0') {
            if (i > 0 && !ps.prev_was_empty) {
                md_out_append_tagged(out, "\n", "md_gap");
                ps.prev_was_empty = TRUE;
            }
            continue;
        }

        ps.last_was_li = FALSE;
        i = parse_paragraph(&ps, i);
        ps.prev_was_empty = FALSE;
    }
    md_out_tag_from(out, 0, "md_body");

    g_strfreev(ps.lines);
    g_free(doc_dir);
    return out;
}

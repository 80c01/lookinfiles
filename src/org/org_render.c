/* Org document parsed for the preview (see org_render.h). */
#include "org/org_render.h"

#include "core/file_types.h"
#include "media/image_utils.h"
#include "org/org_doc.h"

#include <ctype.h>
#include <string.h>

void org_line_info_free(gpointer data) {
    OrgLineInfo* info = (OrgLineInfo*)data;
    if (info == NULL) return;
    g_free(info->todo_state);
    g_free(info->link_target);
    g_free(info);
}

static gboolean is_image_path(const gchar* path) {
    if (path == NULL) return FALSE;
    const gchar* ext = file_extension(path);
    static const gchar* const IMAGE_EXTS[] = {"png", "jpg", "jpeg", "gif", "svg", "webp", "bmp"};
    for (gsize i = 0; i < G_N_ELEMENTS(IMAGE_EXTS); i++) {
        if (g_ascii_strcasecmp(ext, IMAGE_EXTS[i]) == 0) return TRUE;
    }
    return FALSE;
}

/* text (len bytes, -1: to its end) with tag over it */
static void append_tagged_len(MdOut* out, const gchar* text, gssize len, const gchar* tag) {
    gint start = md_out_offset(out);
    md_out_append(out, text, len);
    md_out_tag_from(out, start, tag);
}

/* [[target][label]] whose target is a picture: the picture (read now),
 * else its label with a glyph */
static void append_image_link(MdOut* out, const gchar* target, const gchar* label, const gchar* doc_dir) {
    gchar* img_path = NULL;
    if (g_path_is_absolute(target)) {
        img_path = g_strdup(target);
    } else if (doc_dir != NULL) {
        img_path = g_build_filename(doc_dir, target, NULL);
    }
    GdkPixbuf* pix = NULL;
    if (img_path != NULL && g_file_test(img_path, G_FILE_TEST_EXISTS)) {
        /* Natural size, shrunk to the view width */
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
    gchar* ph = g_strdup_printf("[%s]", label);
    md_out_append(out, ph, -1);
    g_free(ph);
    md_out_tag_from(out, start, "org_link");
}

/* Characters that may start inline markup */
#define INLINE_SPECIALS "[<*/_=~+"

/* Inline markup: *bold*, /italic/, _underline_, =verbatim=, ~code~,
 * +strike+, [[link][desc]], timestamps. dim_done: the text of a done item. */
static void append_inline(MdOut* out, const gchar* text, const gchar* doc_dir, gboolean dim_done,
                          OrgLineInfo* line_info) {
    if (text == NULL || *text == '\0') return;

    const gchar* p = text;
    while (*p != '\0') {
        /* Org links: [[url][label]] or [[url]] */
        if (*p == '[' && p[1] == '[') {
            const gchar* link_end = strstr(p + 2, "]]");
            if (link_end != NULL) {
                gchar* inner = g_strndup(p + 2, link_end - (p + 2));
                const gchar* sep = strstr(inner, "][");
                gchar* target = (sep != NULL) ? g_strndup(inner, sep - inner) : g_strdup(inner);
                gchar* label = (sep != NULL) ? g_strdup(sep + 2) : g_strdup(inner);
                const gchar* clean_target = target;
                if (g_str_has_prefix(clean_target, "file:")) {
                    clean_target += 5;
                }
                if (is_image_path(clean_target)) {
                    append_image_link(out, clean_target, label, doc_dir);
                } else {
                    /* Regular link: styled text, as in Markdown */
                    append_tagged_len(out, label[0] != '\0' ? label : target, -1, "org_link");
                    if (line_info != NULL && line_info->link_target == NULL) {
                        line_info->link_target = g_strdup(clean_target);
                    }
                }
                g_free(inner);
                g_free(target);
                g_free(label);
                p = link_end + 2;
                continue;
            }
        }

        /* Active timestamp <2026-10-28 Wed>, inactive [2026-10-28 Wed] */
        if ((*p == '<' || *p == '[') && isdigit((unsigned char)p[1])) {
            const gchar* ts_end = strchr(p, *p == '<' ? '>' : ']');
            if (ts_end != NULL && (ts_end - p) < 40) {
                append_tagged_len(out, p, ts_end - p + 1, *p == '<' ? "org_timestamp" : "org_inactive_ts");
                p = ts_end + 1;
                continue;
            }
        }

        /* Emphasis after a space or an opening bracket */
        if ((*p == '*' || *p == '/' || *p == '_' || *p == '=' || *p == '~' || *p == '+') &&
            p > text && (isspace((unsigned char)p[-1]) || p[-1] == '(' || p[-1] == '{') &&
            p[1] != '\0' && !isspace((unsigned char)p[1])) {
            gchar delim = *p;
            const gchar* end = strchr(p + 1, delim);
            if (end != NULL && end > p + 1 &&
                (end[1] == '\0' || isspace((unsigned char)end[1]) || ispunct((unsigned char)end[1]))) {
                const gchar* tag_name = (delim == '*') ? "org_bold"
                                        : (delim == '/') ? "org_italic"
                                        : (delim == '_') ? "org_underline"
                                        : (delim == '+') ? "org_strike"
                                                         : "org_code";
                gint start = md_out_offset(out);
                md_out_append(out, p + 1, end - (p + 1));
                md_out_tag_from(out, start, tag_name);
                if (dim_done) {
                    md_out_tag_from(out, start, "org_done_text");
                }
                p = end + 1;
                continue;
            }
        }

        /* Plain text, up to the next character that may start markup */
        const gchar* next = p + 1;
        while (*next != '\0' && strchr(INLINE_SPECIALS, *next) == NULL) {
            next++;
        }
        append_tagged_len(out, p, next - p, dim_done ? "org_done_text" : NULL);
        p = next;
    }
}

/* New line info for the next preview line */
static OrgLineInfo* line_info_new(GPtrArray* line_maps, MdOut* out, guint file_line) {
    OrgLineInfo* info = g_new0(OrgLineInfo, 1);
    info->buffer_line = md_out_line(out);
    info->file_line = (gint)file_line;
    g_ptr_array_add(line_maps, info);
    return info;
}

static const struct { const gchar* keyword; const gchar* tag; } TODO_KEYWORDS[] = {
    {"TODO", "org_todo"}, {"DONE", "org_done"}, {"NEXT", "org_next"},
    {"RUNNING", "org_running"}, {"WAITING", "org_waiting"}, {"CANCELLED", "org_cancelled"},
};

/* Heading line: fold arrow, TODO badge, priority, title, tags, fold cookie */
static void append_heading(MdOut* out, OrgLineInfo* info, const gchar* raw_line, gint hlevel,
                           gboolean is_folded, const gchar* doc_dir) {
    info->heading_level = hlevel;
    gint start = md_out_offset(out);
    md_out_append(out, is_folded ? "▶ " : "▼ ", -1);

    /* [TODO/DONE] [#A] Title [:tag1:tag2:] */
    const gchar* rest = raw_line + hlevel + 1;
    while (*rest == ' ') rest++;
    const gchar* todo_kw = NULL;
    for (gsize k = 0; k < G_N_ELEMENTS(TODO_KEYWORDS) && todo_kw == NULL; k++) {
        gsize n = strlen(TODO_KEYWORDS[k].keyword);
        if (strncmp(rest, TODO_KEYWORDS[k].keyword, n) == 0 && (rest[n] == ' ' || rest[n] == '\0')) {
            todo_kw = TODO_KEYWORDS[k].keyword;
            info->todo_state = g_strdup(todo_kw);
            gchar* badge = g_strdup_printf(" %s ", todo_kw);
            append_tagged_len(out, badge, -1, TODO_KEYWORDS[k].tag);
            g_free(badge);
            md_out_append(out, " ", 1);
            rest += n;
            while (*rest == ' ') rest++;
        }
    }

    /* Priority cookie [#A], [#B], [#C] */
    if (rest[0] == '[' && rest[1] == '#' && rest[2] != '\0' && rest[3] == ']') {
        gchar prio = rest[2];
        gchar* prio_str = g_strdup_printf("[#%c] ", prio);
        append_tagged_len(out, prio_str, -1,
                          (prio == 'A') ? "org_prio_a" : (prio == 'B') ? "org_prio_b" : "org_prio_c");
        g_free(prio_str);
        rest += 4;
        while (*rest == ' ') rest++;
    }

    /* Tags at the end of the heading: :work:urgent: */
    gchar* title_part = g_strdup(rest);
    gchar* tag_part = NULL;
    gchar* colon = strrchr(title_part, ':');
    if (colon != NULL && colon > title_part && *(colon + 1) == '\0') {
        gchar* start_colon = colon - 1;
        while (start_colon > title_part && *start_colon != ' ' && *start_colon != '\t') {
            if (*start_colon == ':') {
                tag_part = g_strdup(start_colon);
                *start_colon = '\0';
                g_strstrip(title_part);
                break;
            }
            start_colon--;
        }
    }

    gboolean dim_done = (g_strcmp0(todo_kw, "DONE") == 0 || g_strcmp0(todo_kw, "CANCELLED") == 0);
    append_inline(out, title_part, doc_dir, dim_done, info);
    g_free(title_part);
    if (tag_part != NULL) {
        md_out_append(out, "  ", 2);
        append_tagged_len(out, tag_part, -1, "org_tag");
        g_free(tag_part);
    }
    if (is_folded) {
        append_tagged_len(out, " ...", -1, "org_fold_cookie");
    }

    static const gchar* const HEADING_TAGS[] = {"org_h1", "org_h2", "org_h3", "org_h4", "org_h5", "org_h6"};
    md_out_tag_from(out, start, HEADING_TAGS[CLAMP(hlevel, 1, 6) - 1]);
    if (dim_done) {
        md_out_tag_from(out, start, "org_done_text");
    }
    md_out_append(out, "\n", 1);
}

/* Planning line: SCHEDULED:, DEADLINE:, CLOSED: and their timestamps */
static void append_planning(MdOut* out, const gchar* trimmed) {
    static const struct { const gchar* label; const gchar* tag; } LABELS[] = {
        {"DEADLINE:", "org_deadline_label"}, {"SCHEDULED:", "org_scheduled_label"}, {"CLOSED:", "org_closed_label"},
    };
    md_out_append(out, "    ", 4);
    const gchar* p = trimmed;
    while (*p != '\0') {
        gboolean labelled = FALSE;
        for (gsize k = 0; k < G_N_ELEMENTS(LABELS) && !labelled; k++) {
            if (g_str_has_prefix(p, LABELS[k].label)) {
                gchar* label = g_strconcat(LABELS[k].label, " ", NULL);
                append_tagged_len(out, label, -1, LABELS[k].tag);
                g_free(label);
                p += strlen(LABELS[k].label);
                while (*p == ' ') p++;
                labelled = TRUE;
            }
        }
        if (labelled) {
            continue;
        }
        if (*p == '<' || *p == '[') {
            const gchar* end_ts = strchr(p, (*p == '<') ? '>' : ']');
            if (end_ts != NULL) {
                append_tagged_len(out, p, end_ts - p + 1, (*p == '<') ? "org_timestamp" : "org_inactive_ts");
                p = end_ts + 1;
                while (*p == ' ') p++;
                if (*p != '\0') md_out_append(out, "  ", 2);
                continue;
            }
        }
        const gchar* next = g_utf8_next_char(p);
        md_out_append(out, p, next - p);
        p = next;
    }
    md_out_append(out, "\n", 1);
}

/* List item from lead (after the indentation): bullet or checkbox, text */
static void append_list_item(MdOut* out, OrgLineInfo* info, const gchar* raw_line, const gchar* lead,
                             const gchar* doc_dir) {
    gint ind = (gint)(lead - raw_line);
    for (gint k = 0; k < ind + 2; k++) {
        md_out_append(out, " ", 1);
    }
    /* Past the bullet */
    if (*lead == '-' || *lead == '+' || *lead == '*') {
        lead++;
        while (*lead == ' ') lead++;
    } else if (isdigit((unsigned char)*lead)) {
        while (isdigit((unsigned char)*lead) || *lead == '.' || *lead == ')') lead++;
        while (*lead == ' ') lead++;
    }
    /* Checkbox: [ ], [X], [-] */
    if (lead[0] == '[' && (lead[1] == ' ' || lead[1] == 'X' || lead[1] == 'x' || lead[1] == '-') && lead[2] == ']') {
        info->is_checkbox = TRUE;
        info->checkbox_done = (lead[1] == 'X' || lead[1] == 'x');
        gboolean partial = (lead[1] == '-');
        append_tagged_len(out, info->checkbox_done ? "☑ " : partial ? "⊟ " : "☐ ", -1,
                          info->checkbox_done ? "org_checkbox_checked"
                          : partial           ? "org_checkbox_partial"
                                              : "org_checkbox_unchecked");
        lead += 3;
        while (*lead == ' ') lead++;
    } else {
        md_out_append(out, "• ", -1);
    }
    append_inline(out, lead, doc_dir, info->checkbox_done, info);
    md_out_append(out, "\n", 1);
}

/* Title, author and date lines (#+TITLE...) of the first lines, shown first */
static void append_title(MdOut* out, gchar** lines, guint num_lines) {
    gchar* title = NULL;
    gchar* author = NULL;
    gchar* date = NULL;
    for (guint i = 0; i < num_lines && i < 40; i++) {
        const gchar* l = lines[i];
        if (g_str_has_prefix(l, "#+TITLE:") || g_str_has_prefix(l, "#+title:")) {
            g_free(title);
            title = g_strstrip(g_strdup(l + 8));
        } else if (g_str_has_prefix(l, "#+AUTHOR:") || g_str_has_prefix(l, "#+author:")) {
            g_free(author);
            author = g_strstrip(g_strdup(l + 9));
        } else if (g_str_has_prefix(l, "#+DATE:") || g_str_has_prefix(l, "#+date:")) {
            g_free(date);
            date = g_strstrip(g_strdup(l + 7));
        }
    }
    if (title != NULL && title[0] != '\0') {
        gint start = md_out_offset(out);
        md_out_append(out, title, -1);
        md_out_append(out, "\n", 1);
        md_out_tag_from(out, start, "org_title");
        if (author != NULL || date != NULL) {
            GString* meta = g_string_new("By ");
            if (author) g_string_append(meta, author);
            if (author && date) g_string_append(meta, "  •  ");
            if (date) g_string_append(meta, date);
            g_string_append(meta, "\n\n");
            append_tagged_len(out, meta->str, (gssize)meta->len, "org_meta");
            g_string_free(meta, TRUE);
        } else {
            md_out_append(out, "\n", 1);
        }
    }
    g_free(title);
    g_free(author);
    g_free(date);
}

/* "  line\n" with tag */
static void append_block_line(MdOut* out, const gchar* line, const gchar* tag) {
    gint start = md_out_offset(out);
    md_out_append(out, "  ", 2);
    md_out_append(out, line, -1);
    md_out_append(out, "\n", 1);
    md_out_tag_from(out, start, tag);
}

MdOut* org_render_parse(const gchar* org_text, const gchar* doc_path, gboolean is_preview,
                        const gchar* file_name, gint64 total_size, GHashTable* folded_lines,
                        gint global_fold, GPtrArray** line_maps_out) {
    MdOut* out = md_out_new();
    GPtrArray* line_maps = g_ptr_array_new_with_free_func(org_line_info_free);
    *line_maps_out = line_maps;
    if (org_text == NULL) {
        return out;
    }
    gchar* doc_dir = doc_path ? g_path_get_dirname(doc_path) : NULL;
    gchar** lines = g_strsplit(org_text, "\n", -1);
    guint num_lines = g_strv_length(lines);

    if (is_preview && file_name != NULL) {
        gchar* banner = g_strdup_printf("  [ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)  •  Org-mode\n\n",
                                        file_name, total_size);
        append_tagged_len(out, banner, -1, "org_banner");
        g_free(banner);
    }
    append_title(out, lines, num_lines);

    gint skip_until_heading_level = 0;
    gboolean in_src_block = FALSE;
    gboolean in_drawer = FALSE;

    for (guint i = 0; i < num_lines; i++) {
        const gchar* raw_line = lines[i];
        gint hlevel = org_heading_level(raw_line);

        if (hlevel > 0) {
            in_src_block = FALSE;
            in_drawer = FALSE;
            /* A heading at the folded one's level or above ends the skip */
            if (skip_until_heading_level > 0) {
                if (hlevel <= skip_until_heading_level) {
                    skip_until_heading_level = 0;
                } else {
                    continue;
                }
            }
            gboolean is_folded = (global_fold == ORG_GLOBAL_OVERVIEW) || (global_fold == ORG_GLOBAL_CONTENTS) ||
                                 (folded_lines != NULL &&
                                  g_hash_table_contains(folded_lines, GINT_TO_POINTER((gint)i)));
            append_heading(out, line_info_new(line_maps, out, i), raw_line, hlevel, is_folded, doc_dir);
            if (is_folded) {
                skip_until_heading_level = hlevel;
            }
            continue;
        }
        if (skip_until_heading_level > 0) {
            continue; /* inside a folded subtree */
        }

        gchar* trimmed = g_strstrip(g_strdup(raw_line));
        /* Drawer lines: :PROPERTIES: ... :END: */
        if (g_str_has_prefix(trimmed, ":PROPERTIES:") || g_str_has_prefix(trimmed, ":LOGBOOK:")) {
            in_drawer = TRUE;
        }
        if (in_drawer) {
            if (g_str_has_prefix(trimmed, ":END:")) {
                in_drawer = FALSE;
            }
            append_block_line(out, trimmed, "org_drawer");
        } else if (g_str_has_prefix(trimmed, "#+BEGIN_SRC") || g_str_has_prefix(trimmed, "#+begin_src")) {
            in_src_block = TRUE;
            append_block_line(out, trimmed, "org_src_header");
        } else if (in_src_block) {
            if (g_str_has_prefix(trimmed, "#+END_SRC") || g_str_has_prefix(trimmed, "#+end_src")) {
                in_src_block = FALSE;
                append_tagged_len(out, "  #+END_SRC\n\n", -1, "org_src_header");
            } else {
                gint start = md_out_offset(out);
                md_out_append(out, raw_line, -1);
                md_out_append(out, "\n", 1);
                md_out_tag_from(out, start, "org_src_block");
            }
        } else if (strstr(trimmed, "SCHEDULED:") != NULL || strstr(trimmed, "DEADLINE:") != NULL ||
                   strstr(trimmed, "CLOSED:") != NULL) {
            append_planning(out, trimmed);
        } else if (g_str_has_prefix(trimmed, "#+")) {
            /* Directives (#+TITLE... shown above too) */
            gint start = md_out_offset(out);
            md_out_append(out, trimmed, -1);
            md_out_append(out, "\n", 1);
            md_out_tag_from(out, start, "org_meta");
        } else {
            const gchar* lead = raw_line;
            while (*lead == ' ' || *lead == '\t') lead++;
            gboolean is_list = (*lead == '-' || *lead == '+' || (*lead == '*' && lead > raw_line) ||
                                isdigit((unsigned char)*lead));
            if (is_list) {
                append_list_item(out, line_info_new(line_maps, out, i), raw_line, lead, doc_dir);
            } else if (*trimmed == '|') {
                gboolean rule = (strstr(trimmed, "|-") != NULL || strstr(trimmed, "|=") != NULL);
                append_block_line(out, trimmed, rule ? "org_drawer" : "org_table");
            } else {
                OrgLineInfo* info = line_info_new(line_maps, out, i);
                md_out_append(out, "  ", 2);
                append_inline(out, raw_line, doc_dir, FALSE, info);
                md_out_append(out, "\n", 1);
            }
        }
        g_free(trimmed);
    }

    g_strfreev(lines);
    g_free(doc_dir);
    return out;
}

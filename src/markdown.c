#include "markdown.h"

void ensure_markdown_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    #define CREATE_TAG(name, ...) \
        if (!gtk_text_tag_table_lookup(tt, name)) { \
            gtk_text_buffer_create_tag(buffer, name, __VA_ARGS__, NULL); \
        }

    CREATE_TAG("md_h1",
               "scale", 1.8,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff",
               "pixels-above-lines", 14,
               "pixels-below-lines", 6);

    CREATE_TAG("md_h2",
               "scale", 1.45,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h3",
               "scale", 1.25,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#dcdcaa",
               "pixels-above-lines", 10,
               "pixels-below-lines", 3);

    CREATE_TAG("md_h4",
               "scale", 1.1,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ce9178",
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("md_h5",
               "scale", 1.0,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#c586c0",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_h6",
               "scale", 0.9,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#808080",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_bold_italic",
               "weight", PANGO_WEIGHT_BOLD,
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_strike",
               "strikethrough", TRUE,
               "foreground", "#808080");

    CREATE_TAG("md_inline_code",
               "family", "monospace",
               "background", "#2d2d2d",
               "foreground", "#ce9178");

    CREATE_TAG("md_code_block",
               "family", "monospace",
               "background", "#252526",
               "foreground", "#d4d4d4",
               "left-margin", 24,
               "right-margin", 24,
               "wrap-mode", GTK_WRAP_NONE,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_code_header",
               "family", "monospace",
               "scale", 0.85,
               "foreground", "#808080",
               "background", "#121314",
               "left-margin", 24,
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("md_quote",
               "style", PANGO_STYLE_ITALIC,
               "foreground", "#b0b0b0",
               "left-margin", 24,
               "pixels-above-lines", 2,
               "pixels-below-lines", 2);

    CREATE_TAG("md_quote_bar",
               "foreground", "#569cd6",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_list_bullet",
               "foreground", "#569cd6",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_list_task_done",
               "foreground", "#6a9955",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_list_task_todo",
               "foreground", "#dcdcaa",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_hr",
               "foreground", "#3e3e42",
               "pixels-above-lines", 8,
               "pixels-below-lines", 8);

    CREATE_TAG("md_link",
               "foreground", "#3794ff",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("md_table",
               "family", "monospace",
               "foreground", "#cccccc",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_header",
               "family", "monospace",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0",
               "background", "#252526",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_border",
               "family", "monospace",
               "foreground", "#555555",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_alert_note",
               "foreground", "#4fc1ff",
               "weight", PANGO_WEIGHT_BOLD,
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_alert_tip",
               "foreground", "#4ec9b0",
               "weight", PANGO_WEIGHT_BOLD,
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_alert_important",
               "foreground", "#c586c0",
               "weight", PANGO_WEIGHT_BOLD,
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_alert_warning",
               "foreground", "#dcdcaa",
               "weight", PANGO_WEIGHT_BOLD,
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_alert_caution",
               "foreground", "#f44747",
               "weight", PANGO_WEIGHT_BOLD,
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("md_image",
               "foreground", "#9cdcfe",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_preview_banner",
               "family", "monospace",
               "scale", 0.9,
               "foreground", "#dcdcaa",
               "background", "#2d2d2d",
               "pixels-above-lines", 4,
               "pixels-below-lines", 8);

    #undef CREATE_TAG

    for (gint d = 0; d < 8; d++) {
        gchar tname[32];
        gint base_margin = 18 + d * 20;

        /* Unordered list item: bullet at base_margin, text & wrapped lines at base_margin + 14 */
        g_snprintf(tname, sizeof(tname), "md_li_ul_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -14,
                NULL);
        }

        /* Ordered list item: number at base_margin, text & wrapped lines at base_margin + 19 */
        g_snprintf(tname, sizeof(tname), "md_li_ol_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -19,
                NULL);
        }

        /* Task list item: checkbox at base_margin, text & wrapped lines at base_margin + 17 */
        g_snprintf(tname, sizeof(tname), "md_li_task_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -17,
                NULL);
        }

        /* Continuation paragraph in unordered list: aligns with text at base_margin + 14 */
        g_snprintf(tname, sizeof(tname), "md_li_p_ul_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin + 14,
                "indent", 0,
                "pixels-above-lines", 6,
                "pixels-below-lines", 4,
                NULL);
        }

        /* Continuation paragraph in ordered / task list: aligns with text at base_margin + 19 */
        g_snprintf(tname, sizeof(tname), "md_li_p_ol_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin + 19,
                "indent", 0,
                "pixels-above-lines", 6,
                "pixels-below-lines", 4,
                NULL);
        }
    }
}

static void apply_tag_mark(GtkTextBuffer* buffer, const gchar* tag_name,
                           GtkTextMark* start_mark, GtkTextIter* end_iter) {
    if (tag_name == NULL) return;
    GtkTextIter start_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);
    gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start_iter, end_iter);
}

/* Parse inline formatting and insert into buffer */
static void render_inline_text(GtkTextBuffer* buffer, GtkTextIter* iter,
                               const gchar* text, const gchar* doc_dir) {
    const gchar* p = text;
    while (*p != '\0') {
        /* Backslash escape */
        if (*p == '\\' && p[1] != '\0') {
            p++;
            gtk_text_buffer_insert(buffer, iter, p, 1);
            p++;
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
                    gchar* img_path = NULL;
                    if (g_path_is_absolute(url)) {
                        img_path = g_strdup(url);
                    } else if (doc_dir != NULL) {
                        img_path = g_build_filename(doc_dir, url, NULL);
                    }
                    gboolean loaded = FALSE;
                    if (img_path != NULL && g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                        GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(img_path, 480, -1, TRUE, NULL);
                        if (pix != NULL) {
                            gtk_text_buffer_insert_pixbuf(buffer, iter, pix);
                            g_object_unref(pix);
                            loaded = TRUE;
                        }
                    }
                    g_free(img_path);
                    if (!loaded) {
                        GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                        gchar* placeholder = g_strdup_printf("🖼 [%s]", alt);
                        gtk_text_buffer_insert(buffer, iter, placeholder, -1);
                        g_free(placeholder);
                        apply_tag_mark(buffer, "md_image", m, iter);
                        gtk_text_buffer_delete_mark(buffer, m);
                    }
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
                    const gchar* display = (label[0] != '\0') ? label : url;
                    GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                    gtk_text_buffer_insert(buffer, iter, display, -1);
                    apply_tag_mark(buffer, "md_link", m, iter);
                    gtk_text_buffer_delete_mark(buffer, m);
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
            const gchar* code_end = (bcount == 2) ? strstr(code_start, "``")
                                                  : strchr(code_start, '`');
            if (code_end) {
                gchar* code = g_strndup(code_start, code_end - code_start);
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                gtk_text_buffer_insert(buffer, iter, code, -1);
                apply_tag_mark(buffer, "md_inline_code", m, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                g_free(code);
                p = code_end + bcount;
                continue;
            }
        }

        /* Strikethrough: ~~text~~ */
        if (p[0] == '~' && p[1] == '~') {
            const gchar* strike_end = strstr(p + 2, "~~");
            if (strike_end) {
                gchar* inner = g_strndup(p + 2, strike_end - (p + 2));
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                render_inline_text(buffer, iter, inner, doc_dir);
                apply_tag_mark(buffer, "md_strike", m, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                g_free(inner);
                p = strike_end + 2;
                continue;
            }
        }

        /* Bold + Italic: ***text*** or ___text___ */
        if ((strncmp(p, "***", 3) == 0 || strncmp(p, "___", 3) == 0)) {
            const gchar* delim = (p[0] == '*') ? "***" : "___";
            const gchar* bi_end = strstr(p + 3, delim);
            if (bi_end) {
                gchar* inner = g_strndup(p + 3, bi_end - (p + 3));
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                render_inline_text(buffer, iter, inner, doc_dir);
                apply_tag_mark(buffer, "md_bold_italic", m, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                g_free(inner);
                p = bi_end + 3;
                continue;
            }
        }

        /* Bold: **text** or __text__ */
        if ((strncmp(p, "**", 2) == 0 || strncmp(p, "__", 2) == 0)) {
            const gchar* delim = (p[0] == '*') ? "**" : "__";
            const gchar* b_end = strstr(p + 2, delim);
            if (b_end) {
                gchar* inner = g_strndup(p + 2, b_end - (p + 2));
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                render_inline_text(buffer, iter, inner, doc_dir);
                apply_tag_mark(buffer, "md_bold", m, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                g_free(inner);
                p = b_end + 2;
                continue;
            }
        }

        /* Italic: *text* or _text_ */
        if (*p == '*' || *p == '_') {
            gchar delim = *p;
            gboolean skip = FALSE;
            /* For underscore, ignore if in middle of word (e.g. snake_case) */
            if (delim == '_' && p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) {
                skip = TRUE;
            }
            if (!skip) {
                const gchar* it_end = strchr(p + 1, delim);
                if (it_end && it_end != p + 1) {
                    if (delim != '_' || !isalnum((unsigned char)it_end[1])) {
                        gchar* inner = g_strndup(p + 1, it_end - (p + 1));
                        GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                        render_inline_text(buffer, iter, inner, doc_dir);
                        apply_tag_mark(buffer, "md_italic", m, iter);
                        gtk_text_buffer_delete_mark(buffer, m);
                        g_free(inner);
                        p = it_end + 1;
                        continue;
                    }
                }
            }
        }

        /* Autolink: <http://...> or <https://...> */
        if (*p == '<') {
            const gchar* closing = strchr(p + 1, '>');
            if (closing) {
                if (g_str_has_prefix(p + 1, "http://") ||
                    g_str_has_prefix(p + 1, "https://") ||
                    g_str_has_prefix(p + 1, "mailto:")) {
                    gchar* url = g_strndup(p + 1, closing - (p + 1));
                    GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                    gtk_text_buffer_insert(buffer, iter, url, -1);
                    apply_tag_mark(buffer, "md_link", m, iter);
                    gtk_text_buffer_delete_mark(buffer, m);
                    g_free(url);
                    p = closing + 1;
                    continue;
                }
                if (g_ascii_strncasecmp(p, "<br>", 4) == 0 ||
                    g_ascii_strncasecmp(p, "<br/>", 5) == 0 ||
                    g_ascii_strncasecmp(p, "<br />", 6) == 0) {
                    gtk_text_buffer_insert(buffer, iter, "\n", 1);
                    p = closing + 1;
                    continue;
                }
                p = closing + 1;
                continue;
            }
        }

        /* HTML entities */
        if (*p == '&') {
            if (g_str_has_prefix(p, "&quot;")) {
                gtk_text_buffer_insert(buffer, iter, "\"", 1);
                p += 6;
                continue;
            }
            if (g_str_has_prefix(p, "&amp;")) {
                gtk_text_buffer_insert(buffer, iter, "&", 1);
                p += 5;
                continue;
            }
            if (g_str_has_prefix(p, "&lt;")) {
                gtk_text_buffer_insert(buffer, iter, "<", 1);
                p += 4;
                continue;
            }
            if (g_str_has_prefix(p, "&gt;")) {
                gtk_text_buffer_insert(buffer, iter, ">", 1);
                p += 4;
                continue;
            }
            if (g_str_has_prefix(p, "&nbsp;")) {
                gtk_text_buffer_insert(buffer, iter, " ", 1);
                p += 6;
                continue;
            }
            if (g_str_has_prefix(p, "&#39;")) {
                gtk_text_buffer_insert(buffer, iter, "'", 1);
                p += 5;
                continue;
            }
        }

        /* Plain text chunk up to next special character */
        const gchar* next = p;
        while (*next != '\0' &&
               *next != '\\' &&
               *next != '!' &&
               *next != '[' &&
               *next != '`' &&
               *next != '~' &&
               *next != '*' &&
               *next != '_' &&
               *next != '<' &&
               *next != '&') {
            next++;
        }
        if (next > p) {
            gtk_text_buffer_insert(buffer, iter, p, (gint)(next - p));
            p = next;
        } else {
            gtk_text_buffer_insert(buffer, iter, p, 1);
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

static gchar* clean_table_cell_text(const gchar* text) {
    GString* clean = g_string_new(NULL);
    const gchar* p = text;
    while (*p != '\0') {
        if (*p == '`' || *p == '*' || *p == '_' || *p == '~') {
            p++;
            continue;
        }
        if (*p == '[' && strchr(p, ']')) {
            const gchar* end = strchr(p, ']');
            p++;
            while (p < end) {
                g_string_append_c(clean, *p);
                p++;
            }
            if (*p == ']' && p[1] == '(') {
                const gchar* uend = strchr(p + 2, ')');
                if (uend) {
                    p = uend + 1;
                    continue;
                }
            }
            continue;
        }
        g_string_append_c(clean, *p);
        p++;
    }
    return g_string_free(clean, FALSE);
}

static GPtrArray* split_table_row_cells(const gchar* row_str) {
    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
    const gchar* p = row_str;
    while (*p == ' ') p++;
    if (*p == '|') p++; /* skip leading border pipe */

    GString* cur = g_string_new(NULL);
    while (*p != '\0') {
        if (*p == '\\' && p[1] == '|') {
            g_string_append_c(cur, '|');
            p += 2;
            continue;
        }
        if (*p == '|') {
            const gchar* rest = p + 1;
            while (*rest == ' ') rest++;
            if (*rest == '\0') {
                break;
            }
            gchar* raw = g_string_free(cur, FALSE);
            gchar* cleaned = clean_table_cell_text(raw);
            g_free(raw);
            g_ptr_array_add(cells, g_strstrip(cleaned));
            cur = g_string_new(NULL);
            p++;
            continue;
        }
        g_string_append_c(cur, *p);
        p++;
    }
    if (cur->len > 0) {
        gchar* raw = g_string_free(cur, FALSE);
        gchar* cleaned = clean_table_cell_text(raw);
        g_free(raw);
        g_ptr_array_add(cells, g_strstrip(cleaned));
    } else {
        g_string_free(cur, TRUE);
    }
    return cells;
}

void render_markdown_to_buffer(GtkTextBuffer* buffer,
                               const gchar* markdown_text,
                               const gchar* doc_path,
                               gboolean is_preview,
                               const gchar* file_name,
                               gint64 total_size) {
    gchar* doc_dir = doc_path ? g_path_get_dirname(doc_path) : NULL;
    GtkTextIter iter;
    gtk_text_buffer_set_text(buffer, "", 0);
    gtk_text_buffer_get_start_iter(buffer, &iter);

    if (is_preview) {
        GtkTextMark* pm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
        gchar* banner = g_strdup_printf(
            "  [ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total) — Display truncated\n\n",
            file_name ? file_name : "file", total_size);
        gtk_text_buffer_insert(buffer, &iter, banner, -1);
        apply_tag_mark(buffer, "md_preview_banner", pm, &iter);
        gtk_text_buffer_delete_mark(buffer, pm);
        g_free(banner);
    }

    gchar** lines = g_strsplit(markdown_text, "\n", -1);
    gint num_lines = g_strv_length(lines);

    gboolean in_code_block = FALSE;
    gchar code_fence_char = '`';
    gint code_fence_len = 0;
    gboolean prev_was_empty = FALSE;
    gboolean last_was_li = FALSE;
    gint last_li_level = 0;
    gboolean last_li_is_ordered = FALSE;

    for (gint i = 0; i < num_lines; i++) {
        const gchar* line = lines[i];

        /* Check for code block fence */
        const gchar* trim = line;
        while (*trim == ' ') trim++;
        if (!in_code_block && (g_str_has_prefix(trim, "```") || g_str_has_prefix(trim, "~~~"))) {
            in_code_block = TRUE;
            last_was_li = FALSE;
            code_fence_char = trim[0];
            code_fence_len = 0;
            while (trim[code_fence_len] == code_fence_char) code_fence_len++;
            const gchar* lang = trim + code_fence_len;
            while (*lang == ' ') lang++;

            GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            if (*lang != '\0') {
                gchar* header = g_strdup_printf("  [ %s ]\n", lang);
                gtk_text_buffer_insert(buffer, &iter, header, -1);
                g_free(header);
            } else {
                gtk_text_buffer_insert(buffer, &iter, "  [ Code ]\n", -1);
            }
            apply_tag_mark(buffer, "md_code_header", m, &iter);
            gtk_text_buffer_delete_mark(buffer, m);
            prev_was_empty = FALSE;
            continue;
        }

        if (in_code_block) {
            last_was_li = FALSE;
            gint count = 0;
            const gchar* c = trim;
            while (*c == code_fence_char) { count++; c++; }
            if (count >= code_fence_len) {
                in_code_block = FALSE;
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                prev_was_empty = TRUE;
                continue;
            }
            GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, line, -1);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            apply_tag_mark(buffer, "md_code_block", m, &iter);
            gtk_text_buffer_delete_mark(buffer, m);
            prev_was_empty = FALSE;
            continue;
        }

        /* Check for ATX Headings: # through ###### */
        if (trim[0] == '#') {
            gint h_level = 0;
            while (trim[h_level] == '#') h_level++;
            if (h_level >= 1 && h_level <= 6 && (trim[h_level] == ' ' || trim[h_level] == '\0')) {
                last_was_li = FALSE;
                const gchar* h_text = trim + h_level;
                while (*h_text == ' ') h_text++;
                gchar* h_clean = g_strdup(h_text);
                gint h_len = (gint)strlen(h_clean);
                while (h_len > 0 && (h_clean[h_len - 1] == ' ' || h_clean[h_len - 1] == '#')) {
                    h_clean[--h_len] = '\0';
                }

                gchar tag_name[16];
                snprintf(tag_name, sizeof(tag_name), "md_h%d", h_level);

                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                render_inline_text(buffer, &iter, h_clean, doc_dir);
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                apply_tag_mark(buffer, tag_name, m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);

                if (h_level == 1) {
                    GtkTextMark* m2 = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, "────────────────────────────────────────────────────────────\n", -1);
                    apply_tag_mark(buffer, "md_hr", m2, &iter);
                    gtk_text_buffer_delete_mark(buffer, m2);
                } else if (h_level == 2) {
                    GtkTextMark* m2 = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, "┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈\n", -1);
                    apply_tag_mark(buffer, "md_hr", m2, &iter);
                    gtk_text_buffer_delete_mark(buffer, m2);
                }

                g_free(h_clean);
                prev_was_empty = FALSE;
                continue;
            }
        }

        /* Check for Horizontal Rule */
        if (is_hr_line(line)) {
            last_was_li = FALSE;
            GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, "────────────────────────────────────────────────────────────\n", -1);
            apply_tag_mark(buffer, "md_hr", m, &iter);
            gtk_text_buffer_delete_mark(buffer, m);
            prev_was_empty = FALSE;
            continue;
        }

        /* Check for Blockquote & GitHub Alerts */
        if (trim[0] == '>') {
            last_was_li = FALSE;
            const gchar* q_text = trim + 1;
            while (*q_text == ' ') q_text++;

            if (g_str_has_prefix(q_text, "[!NOTE]")) {
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  ℹ️  NOTE\n", -1);
                apply_tag_mark(buffer, "md_alert_note", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);
                prev_was_empty = FALSE;
                continue;
            } else if (g_str_has_prefix(q_text, "[!TIP]")) {
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  💡  TIP\n", -1);
                apply_tag_mark(buffer, "md_alert_tip", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);
                prev_was_empty = FALSE;
                continue;
            } else if (g_str_has_prefix(q_text, "[!IMPORTANT]")) {
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  ❗  IMPORTANT\n", -1);
                apply_tag_mark(buffer, "md_alert_important", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);
                prev_was_empty = FALSE;
                continue;
            } else if (g_str_has_prefix(q_text, "[!WARNING]")) {
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  ⚠️  WARNING\n", -1);
                apply_tag_mark(buffer, "md_alert_warning", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);
                prev_was_empty = FALSE;
                continue;
            } else if (g_str_has_prefix(q_text, "[!CAUTION]")) {
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  🛑  CAUTION\n", -1);
                apply_tag_mark(buffer, "md_alert_caution", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);
                prev_was_empty = FALSE;
                continue;
            }

            GtkTextMark* bar_m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, "  │ ", -1);
            apply_tag_mark(buffer, "md_quote_bar", bar_m, &iter);
            gtk_text_buffer_delete_mark(buffer, bar_m);

            GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            render_inline_text(buffer, &iter, q_text, doc_dir);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            apply_tag_mark(buffer, "md_quote", m, &iter);
            gtk_text_buffer_delete_mark(buffer, m);
            prev_was_empty = FALSE;
            continue;
        }

        /* Check for Tables: line starts with | or contains | with a following separator */
        if (trim[0] == '|' && i + 1 < num_lines && strchr(lines[i + 1], '|')) {
            const gchar* sep = lines[i + 1];
            while (*sep == ' ') sep++;
            gboolean is_table = FALSE;
            if (sep[0] == '|') {
                const gchar* sc = sep;
                gint dash_cnt = 0;
                while (*sc != '\0') {
                    if (*sc == '-') dash_cnt++;
                    sc++;
                }
                if (dash_cnt >= 3) is_table = TRUE;
            }

            if (is_table) {
                last_was_li = FALSE;
                GPtrArray* parsed_rows = g_ptr_array_new();
                g_ptr_array_add(parsed_rows, split_table_row_cells(lines[i])); /* header */
                i += 2; /* skip header and separator row */
                while (i < num_lines) {
                    const gchar* r = lines[i];
                    while (*r == ' ') r++;
                    if (r[0] != '|') break;
                    g_ptr_array_add(parsed_rows, split_table_row_cells(lines[i]));
                    i++;
                }
                i--;

                guint num_rows = parsed_rows->len;
                guint max_cols = 0;
                for (guint r = 0; r < num_rows; r++) {
                    GPtrArray* rc = g_ptr_array_index(parsed_rows, r);
                    if (rc->len > max_cols) max_cols = rc->len;
                }

                if (max_cols > 0) {
                    gint* col_widths = g_new0(gint, max_cols);
                    for (guint c = 0; c < max_cols; c++) col_widths[c] = 3;

                    for (guint r = 0; r < num_rows; r++) {
                        GPtrArray* row_cells = g_ptr_array_index(parsed_rows, r);
                        for (guint c = 0; c < row_cells->len && c < max_cols; c++) {
                            const gchar* cell_text = g_ptr_array_index(row_cells, c);
                            gint clen = (gint)g_utf8_strlen(cell_text, -1);
                            if (clen > col_widths[c]) col_widths[c] = clen;
                        }
                    }

                    GString* border_top = g_string_new("  ┌");
                    GString* border_mid = g_string_new("  ├");
                    GString* border_bot = g_string_new("  └");
                    for (guint c = 0; c < max_cols; c++) {
                        for (gint w = 0; w < col_widths[c] + 2; w++) {
                            g_string_append(border_top, "─");
                            g_string_append(border_mid, "─");
                            g_string_append(border_bot, "─");
                        }
                        if (c + 1 < max_cols) {
                            g_string_append(border_top, "┬");
                            g_string_append(border_mid, "┼");
                            g_string_append(border_bot, "┴");
                        } else {
                            g_string_append(border_top, "┐\n");
                            g_string_append(border_mid, "┤\n");
                            g_string_append(border_bot, "┘\n");
                        }
                    }

                    GtkTextMark* tb_mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, border_top->str, -1);
                    apply_tag_mark(buffer, "md_table_border", tb_mark, &iter);
                    gtk_text_buffer_delete_mark(buffer, tb_mark);

                    for (guint r = 0; r < num_rows; r++) {
                        GPtrArray* row_cells = g_ptr_array_index(parsed_rows, r);
                        GString* row_str = g_string_new("  │");
                        for (guint c = 0; c < max_cols; c++) {
                            const gchar* cell_text = (c < row_cells->len) ? g_ptr_array_index(row_cells, c) : "";
                            gint clen = (gint)g_utf8_strlen(cell_text, -1);
                            gint pad = col_widths[c] - clen;
                            g_string_append_printf(row_str, " %s", cell_text);
                            for (gint p = 0; p < pad; p++) g_string_append_c(row_str, ' ');
                            g_string_append(row_str, " │");
                        }
                        g_string_append_c(row_str, '\n');

                        GtkTextMark* rm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                        gtk_text_buffer_insert(buffer, &iter, row_str->str, -1);
                        apply_tag_mark(buffer, (r == 0) ? "md_table_header" : "md_table", rm, &iter);
                        gtk_text_buffer_delete_mark(buffer, rm);
                        g_string_free(row_str, TRUE);

                        if (r == 0) {
                            GtkTextMark* mb_mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                            gtk_text_buffer_insert(buffer, &iter, border_mid->str, -1);
                            apply_tag_mark(buffer, "md_table_border", mb_mark, &iter);
                            gtk_text_buffer_delete_mark(buffer, mb_mark);
                        }
                    }

                    GtkTextMark* bb_mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, border_bot->str, -1);
                    apply_tag_mark(buffer, "md_table_border", bb_mark, &iter);
                    gtk_text_buffer_delete_mark(buffer, bb_mark);

                    g_string_free(border_top, TRUE);
                    g_string_free(border_mid, TRUE);
                    g_string_free(border_bot, TRUE);
                    g_free(col_widths);
                }

                for (guint r = 0; r < parsed_rows->len; r++) {
                    g_ptr_array_unref(g_ptr_array_index(parsed_rows, r));
                }
                g_ptr_array_unref(parsed_rows);
                prev_was_empty = FALSE;
                continue;
            }
        }

        /* Check for List items */
        gint leading_spaces = 0;
        while (line[leading_spaces] == ' ') leading_spaces++;
        const gchar* item_trim = line + leading_spaces;
        gint level = leading_spaces / 2;
        if (level > 7) level = 7;

        /* Task list item: - [ ] or - [x] or * [ ] etc. */
        if ((item_trim[0] == '-' || item_trim[0] == '*' || item_trim[0] == '+') &&
            item_trim[1] == ' ' && item_trim[2] == '[' &&
            (item_trim[3] == ' ' || item_trim[3] == 'x' || item_trim[3] == 'X') &&
            item_trim[4] == ']' && item_trim[5] == ' ') {
            gboolean done = (item_trim[3] == 'x' || item_trim[3] == 'X');
            const gchar* task_text = item_trim + 6;

            gchar tag_name[32];
            g_snprintf(tag_name, sizeof(tag_name), "md_li_task_d%d", level);

            GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

            GtkTextMark* bm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, done ? "☑ " : "☐ ", -1);
            apply_tag_mark(buffer, done ? "md_list_task_done" : "md_list_task_todo", bm, &iter);
            gtk_text_buffer_delete_mark(buffer, bm);

            GtkTextMark* im = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            render_inline_text(buffer, &iter, task_text, doc_dir);
            if (done) apply_tag_mark(buffer, "md_strike", im, &iter);
            gtk_text_buffer_delete_mark(buffer, im);

            GtkTextIter line_end = iter;
            apply_tag_mark(buffer, tag_name, lm, &line_end);
            gtk_text_buffer_delete_mark(buffer, lm);

            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            prev_was_empty = FALSE;
            last_was_li = TRUE;
            last_li_level = level;
            last_li_is_ordered = FALSE;
            continue;
        }

        /* Unordered list item: - , * , + */
        if ((item_trim[0] == '-' || item_trim[0] == '*' || item_trim[0] == '+') && item_trim[1] == ' ') {
            const gchar* list_text = item_trim + 2;
            const gchar* bullet = (level % 3 == 0) ? "• " : (level % 3 == 1) ? "◦ " : "▪ ";

            gchar tag_name[32];
            g_snprintf(tag_name, sizeof(tag_name), "md_li_ul_d%d", level);

            GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

            GtkTextMark* bm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, bullet, -1);
            apply_tag_mark(buffer, "md_list_bullet", bm, &iter);
            gtk_text_buffer_delete_mark(buffer, bm);

            render_inline_text(buffer, &iter, list_text, doc_dir);

            GtkTextIter line_end = iter;
            apply_tag_mark(buffer, tag_name, lm, &line_end);
            gtk_text_buffer_delete_mark(buffer, lm);

            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            prev_was_empty = FALSE;
            last_was_li = TRUE;
            last_li_level = level;
            last_li_is_ordered = FALSE;
            continue;
        }

        /* Ordered list item: 1. , 2. , etc. */
        if (isdigit((unsigned char)item_trim[0])) {
            const gchar* odot = item_trim;
            while (isdigit((unsigned char)*odot)) odot++;
            if ((*odot == '.' || *odot == ')') && odot[1] == ' ') {
                gchar* num_str = g_strndup(item_trim, odot - item_trim + 2);
                const gchar* list_text = odot + 2;

                gchar tag_name[32];
                g_snprintf(tag_name, sizeof(tag_name), "md_li_ol_d%d", level);

                GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

                GtkTextMark* bm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, num_str, -1);
                g_free(num_str);
                apply_tag_mark(buffer, "md_list_bullet", bm, &iter);
                gtk_text_buffer_delete_mark(buffer, bm);

                render_inline_text(buffer, &iter, list_text, doc_dir);

                GtkTextIter line_end = iter;
                apply_tag_mark(buffer, tag_name, lm, &line_end);
                gtk_text_buffer_delete_mark(buffer, lm);

                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                prev_was_empty = FALSE;
                last_was_li = TRUE;
                last_li_level = level;
                last_li_is_ordered = TRUE;
                continue;
            }
        }

        /* Indented continuation paragraph for list item */
        if (last_was_li && leading_spaces >= 2 && trim[0] != '\0') {
            gchar tag_name[32];
            if (last_li_is_ordered) {
                g_snprintf(tag_name, sizeof(tag_name), "md_li_p_ol_d%d", last_li_level);
            } else {
                g_snprintf(tag_name, sizeof(tag_name), "md_li_p_ul_d%d", last_li_level);
            }

            GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            render_inline_text(buffer, &iter, item_trim, doc_dir);
            GtkTextIter line_end = iter;
            apply_tag_mark(buffer, tag_name, lm, &line_end);
            gtk_text_buffer_delete_mark(buffer, lm);

            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            prev_was_empty = FALSE;
            continue;
        }

        /* Setext heading check: line followed by === or --- */
        if (trim[0] != '\0' && i + 1 < num_lines) {
            const gchar* next_trim = lines[i + 1];
            while (*next_trim == ' ') next_trim++;
            if (next_trim[0] == '=' && next_trim[1] == '=') {
                last_was_li = FALSE;
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                render_inline_text(buffer, &iter, trim, doc_dir);
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                apply_tag_mark(buffer, "md_h1", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);

                GtkTextMark* m2 = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "────────────────────────────────────────────────────────────\n", -1);
                apply_tag_mark(buffer, "md_hr", m2, &iter);
                gtk_text_buffer_delete_mark(buffer, m2);

                i++;
                prev_was_empty = FALSE;
                continue;
            }
            if (next_trim[0] == '-' && next_trim[1] == '-' && !is_hr_line(next_trim)) {
                last_was_li = FALSE;
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                render_inline_text(buffer, &iter, trim, doc_dir);
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                apply_tag_mark(buffer, "md_h2", m, &iter);
                gtk_text_buffer_delete_mark(buffer, m);

                GtkTextMark* m2 = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈\n", -1);
                apply_tag_mark(buffer, "md_hr", m2, &iter);
                gtk_text_buffer_delete_mark(buffer, m2);

                i++;
                prev_was_empty = FALSE;
                continue;
            }
        }

        /* Empty / blank line */
        if (trim[0] == '\0') {
            if (i > 0 && !prev_was_empty) {
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                prev_was_empty = TRUE;
            }
            continue;
        }

        /* Regular paragraph line */
        last_was_li = FALSE;
        render_inline_text(buffer, &iter, line, doc_dir);
        gtk_text_buffer_insert(buffer, &iter, "\n", 1);
        prev_was_empty = FALSE;
    }

    g_strfreev(lines);
    g_free(doc_dir);
}

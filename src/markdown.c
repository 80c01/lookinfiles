#include "markdown.h"
#include "image_utils.h"

void ensure_markdown_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    #define CREATE_TAG(name, ...) \
        if (!gtk_text_tag_table_lookup(tt, name)) { \
            gtk_text_buffer_create_tag(buffer, name, __VA_ARGS__, NULL); \
        }

    /* VS Code Markdown preview look (Dark+): headings in the text colour with
     * a rule under h1/h2, quiet lists, padded code panels, quotes with a bar.
     * Tags created later win: base spacing first, highlighting tags last. */
    CREATE_TAG("md_body",
               "pixels-inside-wrap", 4);

    CREATE_TAG("md_gap", /* blank line between blocks */
               "size-points", 8.0);

    CREATE_TAG("md_h1",
               "scale", 2.0,
               "weight", 600,
               "foreground", "#e6e6e6",
               "pixels-above-lines", 16,
               "pixels-below-lines", 6);

    CREATE_TAG("md_h2",
               "scale", 1.5,
               "weight", 600,
               "foreground", "#e6e6e6",
               "pixels-above-lines", 16,
               "pixels-below-lines", 6);

    CREATE_TAG("md_h3",
               "scale", 1.25,
               "weight", 600,
               "foreground", "#e6e6e6",
               "pixels-above-lines", 14,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h4",
               "scale", 1.0,
               "weight", 600,
               "foreground", "#e6e6e6",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h5",
               "scale", 0.875,
               "weight", 600,
               "foreground", "#e6e6e6",
               "pixels-above-lines", 10,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h6",
               "scale", 0.85,
               "weight", 600,
               "foreground", "#a0a0a0",
               "pixels-above-lines", 10,
               "pixels-below-lines", 4);

    /* Thin full-width line (under h1/h2, horizontal rule): a line holding a
     * space in a tiny font, with a paragraph background (GtkTextView paints no
     * background on a line without characters) */
    CREATE_TAG("md_rule",
               "size-points", 1.0,
               "paragraph-background", "#3a3b3d");

    CREATE_TAG("md_hr",
               "size-points", 1.6,
               "paragraph-background", "#3a3b3d");

    CREATE_TAG("md_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_bold_italic",
               "weight", PANGO_WEIGHT_BOLD,
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_strike",
               "strikethrough", TRUE,
               "foreground", "#909090");

    CREATE_TAG("md_inline_code",
               "family", "monospace",
               "scale", 0.92,
               "background", "#2a2b2d",
               "foreground", "#d7ba7d");

    /* Code block: padded panel */
    CREATE_TAG("md_code_block",
               "family", "monospace",
               "scale", 0.92,
               "foreground", "#d4d4d4",
               "paragraph-background", "#1c1d1f",
               "left-margin", 10,
               "indent", 16, /* every code line is a paragraph: 16 px inner padding */
               "right-margin", 16,
               "wrap-mode", GTK_WRAP_NONE,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    /* Padding lines above and below the code (VS Code: 16 px). Code and
     * padding lines share the same margins: a paragraph background starts at
     * its line's left margin, so different margins would notch the panel. */
    CREATE_TAG("md_code_pad",
               "size-points", 10.0,
               "left-margin", 10,
               "right-margin", 16,
               "paragraph-background", "#1c1d1f");

    CREATE_TAG("md_quote",
               "foreground", "#b8b8b8",
               "paragraph-background", "#1e1f21",
               "left-margin", 10,
               "pixels-above-lines", 0,
               "pixels-below-lines", 0);

    CREATE_TAG("md_quote_bar",
               "foreground", "#1c6ea4");

    CREATE_TAG("md_list_bullet",
               "foreground", "#cccccc");

    /* Disc / circle / square list markers, sized like VS Code's (the plain
     * bullet glyph is much smaller) */
    CREATE_TAG("md_list_disc",
               "foreground", "#cccccc",
               "scale", 0.62,
               "rise", 1 * PANGO_SCALE);

    CREATE_TAG("md_list_task_done",
               "foreground", "#cccccc");

    CREATE_TAG("md_list_task_todo",
               "foreground", "#cccccc");

    CREATE_TAG("md_link",
               "foreground", "#3794ff");

    CREATE_TAG("md_table",
               "family", "monospace",
               "scale", 0.92,
               "foreground", "#cccccc",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_header",
               "family", "monospace",
               "scale", 0.92,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#e6e6e6",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_border",
               "family", "monospace",
               "scale", 0.92,
               "foreground", "#45464a",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    /* GitHub alerts: coloured bar and title, body like a quote */
    CREATE_TAG("md_alert_note", "foreground", "#4493f8", "weight", 600);
    CREATE_TAG("md_alert_tip", "foreground", "#3fb950", "weight", 600);
    CREATE_TAG("md_alert_important", "foreground", "#ab7df8", "weight", 600);
    CREATE_TAG("md_alert_warning", "foreground", "#d29922", "weight", 600);
    CREATE_TAG("md_alert_caution", "foreground", "#f85149", "weight", 600);

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

    /* List items: the marker sits at the start of the line and a tab stop
     * puts the text at a fixed position, so the first line and the wrapped
     * lines start at the same x whatever the marker width (VS Code: 2em per
     * level). md_li_*: left-margin = marker x, negative indent = hanging lines. */
    for (gint d = 0; d < 8; d++) {
        gchar tname[32];
        gint text_x = 34 + d * 24;
        static const struct { const gchar* kind; gint marker_w; } KINDS[] = {
            {"ul", 16}, {"ol", 24}, {"task", 20},
        };
        for (gsize k = 0; k < G_N_ELEMENTS(KINDS); k++) {
            g_snprintf(tname, sizeof(tname), "md_li_%s_d%d", KINDS[k].kind, d);
            if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
                gint w = KINDS[k].marker_w;
                PangoTabArray* tabs = pango_tab_array_new_with_positions(1, TRUE, PANGO_TAB_LEFT, w);
                gtk_text_buffer_create_tag(buffer, tname,
                    "left-margin", text_x - w,
                    "indent", -w,
                    "tabs", tabs,
                    NULL);
                pango_tab_array_free(tabs);
            }
        }
        /* Continuation paragraph of a list item: aligned with the item text */
        for (gsize k = 0; k < 2; k++) {
            g_snprintf(tname, sizeof(tname), "md_li_p_%s_d%d", k == 0 ? "ul" : "ol", d);
            if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
                gtk_text_buffer_create_tag(buffer, tname,
                    "left-margin", text_x,
                    "indent", 0,
                    "pixels-above-lines", 6,
                    "pixels-below-lines", 4,
                    NULL);
            }
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
                            GdkTexture* tex = texture_new_for_pixbuf(pix);
                            gtk_text_buffer_insert_paintable(buffer, iter, GDK_PAINTABLE(tex));
                            g_object_unref(tex);
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
            g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
            cur = g_string_new(NULL);
            p++;
            continue;
        }
        g_string_append_c(cur, *p);
        p++;
    }
    if (cur->len > 0) {
        g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
    } else {
        g_string_free(cur, TRUE);
    }
    return cells;
}

/* Insert text with one tag */
static void insert_tagged(GtkTextBuffer* buffer, GtkTextIter* iter, const gchar* text, const gchar* tag) {
    GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    gtk_text_buffer_insert(buffer, iter, text, -1);
    apply_tag_mark(buffer, tag, m, iter);
    gtk_text_buffer_delete_mark(buffer, m);
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

#ifdef HAVE_GSV
/* GtkSourceView language of a code fence info string ("c", "py", "bash"...) */
static GtkSourceLanguage* code_language(const gchar* info) {
    if (info == NULL || info[0] == '\0') {
        return NULL;
    }
    static const struct { const gchar* alias; const gchar* id; } ALIASES[] = {
        {"javascript", "js"}, {"node", "js"}, {"ts", "typescript"}, {"py", "python3"},
        {"python", "python3"}, {"bash", "sh"}, {"shell", "sh"}, {"zsh", "sh"},
        {"console", "sh"}, {"yml", "yaml"}, {"md", "markdown"}, {"c++", "cpp"},
        {"cs", "c-sharp"}, {"csharp", "c-sharp"}, {"rs", "rust"}, {"golang", "go"},
        {"rb", "ruby"}, {"kt", "kotlin"}, {"htm", "html"}, {"patch", "diff"},
    };
    gchar* word = g_strndup(info, strcspn(info, " \t{"));
    gchar* id = g_ascii_strdown(word, -1);
    g_free(word);
    for (gsize i = 0; i < G_N_ELEMENTS(ALIASES); i++) {
        if (g_strcmp0(id, ALIASES[i].alias) == 0) {
            g_free(id);
            id = g_strdup(ALIASES[i].id);
            break;
        }
    }
    GtkSourceLanguageManager* lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = gtk_source_language_manager_get_language(lm, id);
    if (lang == NULL) {
        gchar* file = g_strconcat("code.", id, NULL);
        lang = gtk_source_language_manager_guess_language(lm, file, NULL);
        g_free(file);
    }
    g_free(id);
    return lang;
}

/* Colour a code block inserted at `start` (offset in buffer) like the editor:
 * highlight a scratch GtkSourceBuffer with the Dark+ scheme and copy its
 * colours as tags ("md_hl_RRGGBB[bi]") */
static void highlight_code(GtkTextBuffer* buffer, gint start, const gchar* code, const gchar* info) {
    GtkSourceLanguage* lang = code_language(info);
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(
        gtk_source_style_scheme_manager_get_default(), "vscode-dark");
    if (lang == NULL || scheme == NULL) {
        return;
    }
    GtkSourceBuffer* sb = gtk_source_buffer_new_with_language(lang);
    gtk_source_buffer_set_style_scheme(sb, scheme);
    gtk_source_buffer_set_highlight_syntax(sb, TRUE);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sb), code, -1);
    GtkTextIter it, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(sb), &it, &end);
    gtk_source_buffer_ensure_highlight(sb, &it, &end);

    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    while (!gtk_text_iter_equal(&it, &end)) {
        GtkTextIter next = it;
        if (!gtk_text_iter_forward_to_tag_toggle(&next, NULL)) {
            next = end;
        }
        GdkRGBA fg;
        gboolean has_fg = FALSE, bold = FALSE, italic = FALSE;
        GSList* tags = gtk_text_iter_get_tags(&it); /* ascending priority */
        for (GSList* l = tags; l != NULL; l = l->next) {
            gboolean fg_set = FALSE, weight_set = FALSE, style_set = FALSE;
            GdkRGBA* rgba = NULL;
            gint weight = 0;
            PangoStyle style = PANGO_STYLE_NORMAL;
            g_object_get(l->data, "foreground-set", &fg_set, "foreground-rgba", &rgba,
                         "weight-set", &weight_set, "weight", &weight,
                         "style-set", &style_set, "style", &style, NULL);
            if (fg_set && rgba != NULL) {
                fg = *rgba;
                has_fg = TRUE;
            }
            if (weight_set) bold = (weight >= PANGO_WEIGHT_SEMIBOLD);
            if (style_set) italic = (style == PANGO_STYLE_ITALIC);
            if (rgba != NULL) gdk_rgba_free(rgba);
        }
        g_slist_free(tags);
        if (has_fg) {
            gchar name[32];
            g_snprintf(name, sizeof name, "md_hl_%02x%02x%02x%s%s", (guint)(fg.red * 255 + 0.5),
                       (guint)(fg.green * 255 + 0.5), (guint)(fg.blue * 255 + 0.5),
                       bold ? "b" : "", italic ? "i" : "");
            if (gtk_text_tag_table_lookup(table, name) == NULL) {
                gtk_text_buffer_create_tag(buffer, name, "foreground-rgba", &fg,
                                           "weight", bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                                           "style", italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL,
                                           NULL);
            }
            GtkTextIter a, b;
            gtk_text_buffer_get_iter_at_offset(buffer, &a, start + gtk_text_iter_get_offset(&it));
            gtk_text_buffer_get_iter_at_offset(buffer, &b, start + gtk_text_iter_get_offset(&next));
            gtk_text_buffer_apply_tag_by_name(buffer, name, &a, &b);
        }
        it = next;
    }
    g_object_unref(sb);
}
#endif

/* GitHub alert marker ("[!NOTE]"...) of a quote line: title, icon and tag; NULL if none */
typedef struct { const gchar* marker; const gchar* title; const gchar* icon; const gchar* tag; } MdAlert;
static const MdAlert MD_ALERTS[] = {
    {"[!NOTE]", "Note", "ℹ️", "md_alert_note"},
    {"[!TIP]", "Tip", "💡", "md_alert_tip"},
    {"[!IMPORTANT]", "Important", "❗", "md_alert_important"},
    {"[!WARNING]", "Warning", "⚠️", "md_alert_warning"},
    {"[!CAUTION]", "Caution", "🛑", "md_alert_caution"},
};

/* Pango markup of a table cell's inline Markdown (code, bold, italic,
 * strikethrough, links), escaped */
static void cell_markup_append(GString* out, const gchar* text, gsize len) {
    gsize i = 0;
    while (i < len) {
        const gchar* p = text + i;
        const gchar* rest_end = text + len;
        if (*p == '`') {
            const gchar* e = memchr(p + 1, '`', rest_end - p - 1);
            if (e != NULL) {
                gchar* esc = g_markup_escape_text(p + 1, e - p - 1);
                g_string_append_printf(out, "<span font_family=\"monospace\" size=\"92%%\" "
                                            "foreground=\"#d7ba7d\" background=\"#2a2b2d\">%s</span>", esc);
                g_free(esc);
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if ((p[0] == '*' || p[0] == '_' || p[0] == '~') && i + 1 < len && p[1] == p[0]) {
            gchar marker[3] = { p[0], p[0], 0 };
            const gchar* e = g_strstr_len(p + 2, rest_end - p - 2, marker);
            if (e != NULL) {
                const gchar* tag = (p[0] == '~') ? "s" : "b";
                g_string_append_printf(out, "<%s>", tag);
                cell_markup_append(out, p + 2, e - p - 2);
                g_string_append_printf(out, "</%s>", tag);
                i = (gsize)(e - text) + 2;
                continue;
            }
        }
        if (p[0] == '*' || p[0] == '_') {
            const gchar* e = memchr(p + 1, p[0], rest_end - p - 1);
            if (e != NULL && e > p + 1) {
                g_string_append(out, "<i>");
                cell_markup_append(out, p + 1, e - p - 1);
                g_string_append(out, "</i>");
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if (p[0] == '[') {
            const gchar* close = memchr(p, ']', rest_end - p);
            if (close != NULL && close + 1 < rest_end && close[1] == '(') {
                const gchar* paren = memchr(close, ')', rest_end - close);
                if (paren != NULL) {
                    g_string_append(out, "<span foreground=\"#3794ff\">");
                    cell_markup_append(out, p + 1, close - p - 1);
                    g_string_append(out, "</span>");
                    i = (gsize)(paren - text) + 1;
                    continue;
                }
            }
        }
        gint n = g_utf8_skip[*(const guchar*)p];
        gchar* esc = g_markup_escape_text(p, n);
        g_string_append(out, esc);
        g_free(esc);
        i += (gsize)n;
    }
}

static gchar* cell_markup(const gchar* text) {
    GString* out = g_string_new(NULL);
    cell_markup_append(out, text, strlen(text));
    return g_string_free(out, FALSE);
}

/* Column alignment from the separator row: ":--" left, "--:" right, ":-:" center */
static gfloat cell_align(const gchar* sep_cell) {
    gsize n = strlen(sep_cell);
    gboolean left = (n > 0 && sep_cell[0] == ':');
    gboolean right = (n > 0 && sep_cell[n - 1] == ':');
    return (left && right) ? 0.5f : right ? 1.0f : 0.0f;
}

/* Table as a grid widget in the text (VS Code: no vertical borders, a line
 * under the header, faint lines between rows; CSS in theme.c) */
static void insert_table_grid(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* iter,
                              GPtrArray* rows, GPtrArray* aligns, guint cols) {
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "md-table");
    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* cells = g_ptr_array_index(rows, r);
        for (guint c = 0; c < cols; c++) {
            const gchar* text = (c < cells->len) ? g_ptr_array_index(cells, c) : "";
            gchar* markup = cell_markup(text);
            GtkWidget* label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), markup);
            g_free(markup);
            gfloat align = (c < aligns->len) ? cell_align(g_ptr_array_index(aligns, c)) : 0.0f;
            gtk_label_set_xalign(GTK_LABEL(label), align);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
            /* The text view gives anchored widgets their minimum size: ask for
             * the cell's length (up to 30 chars) so short cells never wrap */
            gchar* visible = clean_table_cell_text(text);
            gtk_label_set_width_chars(GTK_LABEL(label), MIN((gint)g_utf8_strlen(visible, -1) + 1, 30)); /* +1: bold header */
            g_free(visible);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_widget_set_focusable(label, FALSE);
            gtk_widget_set_valign(label, GTK_ALIGN_FILL);
            gtk_widget_add_css_class(label, r == 0 ? "md-th" : "md-td");
            if (r > 1) {
                gtk_widget_add_css_class(label, "md-td-sep");
            }
            gtk_grid_attach(GTK_GRID(grid), label, (gint)c, (gint)r, 1, 1);
        }
    }
    GtkTextChildAnchor* anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
    gtk_text_view_add_child_at_anchor(view, grid, anchor);
    gtk_text_buffer_insert(buffer, iter, "\n", 1);
}

void render_markdown_to_buffer(GtkTextBuffer* buffer,
                               GtkTextView* view,
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

    gboolean prev_was_empty = FALSE;
    const gchar* alert_tag = NULL; /* inside a GitHub alert: its colour tag */
    gboolean last_was_li = FALSE;
    gint last_li_level = 0;
    gboolean last_li_is_ordered = FALSE;

    for (gint i = 0; i < num_lines; i++) {
        const gchar* line = lines[i];

        /* Fenced code block: a padded panel, highlighted like the editor */
        const gchar* trim = line;
        while (*trim == ' ') trim++;
        if (g_str_has_prefix(trim, "```") || g_str_has_prefix(trim, "~~~")) {
            last_was_li = FALSE;
            alert_tag = NULL;
            gchar fence = trim[0];
            gint fence_len = 0;
            while (trim[fence_len] == fence) fence_len++;
            const gchar* info = trim + fence_len;
            while (*info == ' ') info++;

            GString* code = g_string_new(NULL);
            gint j = i + 1;
            for (; j < num_lines; j++) {
                const gchar* t = lines[j];
                while (*t == ' ') t++;
                gint count = 0;
                while (t[count] == fence) count++;
                if (count >= fence_len) {
                    break;
                }
                g_string_append(code, lines[j]);
                g_string_append_c(code, '\n');
            }
            insert_tagged(buffer, &iter, " \n", "md_code_pad");
            gint code_start = gtk_text_iter_get_offset(&iter);
            insert_tagged(buffer, &iter, code->str, "md_code_block");
#ifdef HAVE_GSV
            highlight_code(buffer, code_start, code->str, info);
#else
            (void)code_start;
#endif
            insert_tagged(buffer, &iter, " \n", "md_code_pad");
            g_string_free(code, TRUE);
            i = j; /* closing fence (or end of text) */
            prev_was_empty = FALSE;
            continue;
        }

        /* Check for ATX Headings: # through ###### */
        if (trim[0] == '#') {
            gint h_level = 0;
            while (trim[h_level] == '#') h_level++;
            if (h_level >= 1 && h_level <= 6 && (trim[h_level] == ' ' || trim[h_level] == '\0')) {
                last_was_li = FALSE;
                alert_tag = NULL;
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

                if (h_level <= 2) {
                    insert_tagged(buffer, &iter, " \n", "md_rule"); /* VS Code: border under h1/h2 */
                }

                g_free(h_clean);
                prev_was_empty = FALSE;
                continue;
            }
        }

        /* Check for Horizontal Rule */
        if (is_hr_line(line)) {
            last_was_li = FALSE;
            alert_tag = NULL;
            insert_tagged(buffer, &iter, "\n", "md_gap");
            insert_tagged(buffer, &iter, " \n", "md_hr");
            insert_tagged(buffer, &iter, "\n", "md_gap");
            prev_was_empty = FALSE;
            continue;
        }

        /* Blockquote (tinted panel with a bar) and GitHub alerts (coloured bar and title) */
        if (trim[0] == '>') {
            last_was_li = FALSE;
            const gchar* q_text = trim + 1;
            while (*q_text == ' ') q_text++;

            const MdAlert* alert = NULL;
            for (gsize k = 0; k < G_N_ELEMENTS(MD_ALERTS); k++) {
                if (g_str_has_prefix(q_text, MD_ALERTS[k].marker)) {
                    alert = &MD_ALERTS[k];
                }
            }
            if (alert != NULL) {
                alert_tag = alert->tag;
                GtkTextMark* qm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                insert_tagged(buffer, &iter, "▌ ", alert_tag);
                gchar* title = g_strdup_printf("%s  %s\n", alert->icon, alert->title);
                insert_tagged(buffer, &iter, title, alert_tag);
                g_free(title);
                apply_tag_mark(buffer, "md_quote", qm, &iter);
                gtk_text_buffer_delete_mark(buffer, qm);
                prev_was_empty = FALSE;
                continue;
            }

            GtkTextMark* qm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            insert_tagged(buffer, &iter, "▌ ", alert_tag != NULL ? alert_tag : "md_quote_bar");
            GtkTextMark* tm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            render_inline_text(buffer, &iter, q_text, doc_dir);
            if (alert_tag != NULL) {
                /* alert text keeps the normal colour, only the bar and title are coloured */
                GtkTextIter ts;
                gtk_text_buffer_get_iter_at_mark(buffer, &ts, tm);
                gtk_text_buffer_remove_tag_by_name(buffer, alert_tag, &ts, &iter);
            }
            gtk_text_buffer_delete_mark(buffer, tm);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            apply_tag_mark(buffer, "md_quote", qm, &iter);
            gtk_text_buffer_delete_mark(buffer, qm);
            prev_was_empty = FALSE;
            continue;
        }
        alert_tag = NULL;

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
                GPtrArray* aligns = split_table_row_cells(lines[i + 1]);
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

                if (max_cols > 0 && view != NULL) {
                    insert_table_grid(buffer, view, &iter, parsed_rows, aligns, max_cols);
                } else if (max_cols > 0) {
                    /* No view (no widgets): text table with box-drawing borders */
                    for (guint r = 0; r < num_rows; r++) {
                        GPtrArray* rc = g_ptr_array_index(parsed_rows, r);
                        for (guint c = 0; c < rc->len; c++) {
                            gchar* cleaned = clean_table_cell_text(g_ptr_array_index(rc, c));
                            g_free(g_ptr_array_index(rc, c));
                            g_ptr_array_index(rc, c) = cleaned;
                        }
                    }
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

                    GString* border_top = g_string_new("┌");
                    GString* border_mid = g_string_new("├");
                    GString* border_bot = g_string_new("└");
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
                        GString* row_str = g_string_new("│");
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
                g_ptr_array_unref(aligns);
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
            gtk_text_buffer_insert(buffer, &iter, done ? "☑\t" : "☐\t", -1);
            apply_tag_mark(buffer, done ? "md_list_task_done" : "md_list_task_todo", bm, &iter);
            gtk_text_buffer_delete_mark(buffer, bm);

            GtkTextMark* im = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            render_inline_text(buffer, &iter, task_text, doc_dir);
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
            const gchar* bullet = (level % 3 == 0) ? "●" : (level % 3 == 1) ? "○" : "■";

            gchar tag_name[32];
            g_snprintf(tag_name, sizeof(tag_name), "md_li_ul_d%d", level);

            GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

            GtkTextMark* bm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, bullet, -1);
            apply_tag_mark(buffer, "md_list_disc", bm, &iter);
            gtk_text_buffer_delete_mark(buffer, bm);
            gtk_text_buffer_insert(buffer, &iter, "\t", -1);

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
                gchar* num_str = g_strdup_printf("%.*s\t", (gint)(odot - item_trim + 1), item_trim);
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

                insert_tagged(buffer, &iter, " \n", "md_rule");

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

                insert_tagged(buffer, &iter, " \n", "md_rule");

                i++;
                prev_was_empty = FALSE;
                continue;
            }
        }

        /* Empty / blank line */
        if (trim[0] == '\0') {
            if (i > 0 && !prev_was_empty) {
                insert_tagged(buffer, &iter, "\n", "md_gap");
                prev_was_empty = TRUE;
            }
            continue;
        }

        /* Paragraph: consecutive source lines join into one wrapped paragraph
         * (like VS Code); a line ending with two spaces or a backslash breaks */
        last_was_li = FALSE;
        GString* para = g_string_new(NULL);
        gint j = i;
        while (TRUE) {
            const gchar* ln = lines[j];
            while (*ln == ' ') ln++;
            gsize len = strlen(ln);
            gboolean hard = (len >= 2 && ln[len - 1] == ' ' && ln[len - 2] == ' ') ||
                            (len >= 1 && ln[len - 1] == '\\');
            while (len > 0 && (ln[len - 1] == ' ' || (hard && ln[len - 1] == '\\'))) len--;
            g_string_append_len(para, ln, (gssize)len);
            if (j + 1 >= num_lines || !is_paragraph_continuation(lines[j + 1])) {
                break;
            }
            g_string_append_c(para, hard ? '\n' : ' ');
            j++;
        }
        render_inline_text(buffer, &iter, para->str, doc_dir);
        gtk_text_buffer_insert(buffer, &iter, "\n", 1);
        g_string_free(para, TRUE);
        i = j;
        prev_was_empty = FALSE;
    }

    GtkTextIter doc_start, doc_end;
    gtk_text_buffer_get_bounds(buffer, &doc_start, &doc_end);
    gtk_text_buffer_apply_tag_by_name(buffer, "md_body", &doc_start, &doc_end);

    g_strfreev(lines);
    g_free(doc_dir);
}

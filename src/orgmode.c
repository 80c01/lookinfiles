#include "orgmode.h"
#include "file_list.h"
#include "editor.h"
#include "viewer.h"
#include "ui.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

/* Structure tracking each line in the rendered buffer */
typedef struct {
    gint buffer_line;       /* line in GTK buffer (0-based) */
    gint file_line;         /* line in original .org file (0-based) */
    gint heading_level;     /* 1-6 if heading, 0 otherwise */
    gchar* todo_state;      /* "TODO", "DONE", "NEXT", "WAITING", etc., or NULL */
    gboolean is_checkbox;   /* TRUE if line contains a checkbox */
    gboolean checkbox_done; /* TRUE if [X] */
    gchar* link_target;     /* URL or file path if line contains primary link */
} OrgLineInfo;

static void org_line_info_free(gpointer data) {
    OrgLineInfo* info = (OrgLineInfo*)data;
    if (info == NULL) return;
    g_free(info->todo_state);
    g_free(info->link_target);
    g_free(info);
}

/* ------------------------------------------------------------------------- */
/* GtkSourceView 5 Syntax Definition for Orgmode                              */
/* ------------------------------------------------------------------------- */

void orgmode_install_gtksourceview_spec(void) {
    const gchar* data_dir = g_get_user_data_dir();
    gchar* spec_dir = g_build_filename(data_dir, "gtksourceview-5", "language-specs", NULL);
    g_mkdir_with_parents(spec_dir, 0755);

    gchar* lang_file = g_build_filename(spec_dir, "org.lang", NULL);
    if (!g_file_test(lang_file, G_FILE_TEST_EXISTS)) {
        const gchar* spec_xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<language id=\"org\" name=\"Org Mode\" version=\"2.0\" _section=\"Markup\">\n"
            "  <metadata>\n"
            "    <property name=\"mimetypes\">text/x-org;text/org</property>\n"
            "    <property name=\"globs\">*.org;*.org_archive</property>\n"
            "    <property name=\"line-comment-start\"># </property>\n"
            "  </metadata>\n"
            "  <styles>\n"
            "    <style id=\"header\" name=\"Header\" map-to=\"def:type\"/>\n"
            "    <style id=\"todo\" name=\"TODO\" map-to=\"def:error\"/>\n"
            "    <style id=\"done\" name=\"DONE\" map-to=\"def:comment\"/>\n"
            "    <style id=\"next\" name=\"NEXT\" map-to=\"def:statement\"/>\n"
            "    <style id=\"waiting\" name=\"WAITING\" map-to=\"def:warning\"/>\n"
            "    <style id=\"comment\" name=\"Comment\" map-to=\"def:comment\"/>\n"
            "    <style id=\"drawer\" name=\"Drawer\" map-to=\"def:preprocessor\"/>\n"
            "    <style id=\"block\" name=\"Block\" map-to=\"def:string\"/>\n"
            "    <style id=\"timestamp\" name=\"Timestamp\" map-to=\"def:special-char\"/>\n"
            "    <style id=\"link\" name=\"Link\" map-to=\"def:underlined\"/>\n"
            "    <style id=\"tag\" name=\"Tag\" map-to=\"def:identifier\"/>\n"
            "    <style id=\"checkbox\" name=\"Checkbox\" map-to=\"def:keyword\"/>\n"
            "  </styles>\n"
            "  <definitions>\n"
            "    <context id=\"org\">\n"
            "      <include>\n"
            "        <context id=\"comment\" style-ref=\"comment\">\n"
            "          <match>^# .*$</match>\n"
            "        </context>\n"
            "        <context id=\"block-header\" style-ref=\"drawer\">\n"
            "          <match>^#\\+[A-Za-z_]+:?.*$</match>\n"
            "        </context>\n"
            "        <context id=\"drawer\" style-ref=\"drawer\">\n"
            "          <match>^[ \\t]*:[A-Za-z_]+:.*$</match>\n"
            "        </context>\n"
            "        <context id=\"todo-kw\" style-ref=\"todo\">\n"
            "          <match>\\b(TODO)\\b</match>\n"
            "        </context>\n"
            "        <context id=\"done-kw\" style-ref=\"done\">\n"
            "          <match>\\b(DONE|CANCELLED)\\b</match>\n"
            "        </context>\n"
            "        <context id=\"next-kw\" style-ref=\"next\">\n"
            "          <match>\\b(NEXT)\\b</match>\n"
            "        </context>\n"
            "        <context id=\"waiting-kw\" style-ref=\"waiting\">\n"
            "          <match>\\b(WAITING)\\b</match>\n"
            "        </context>\n"
            "        <context id=\"timestamp\" style-ref=\"timestamp\">\n"
            "          <match>[&lt;\\[][0-9]{4}-[0-9]{2}-[0-9]{2}[^&gt;\\]]*[&gt;\\]]</match>\n"
            "        </context>\n"
            "        <context id=\"link\" style-ref=\"link\">\n"
            "          <match>\\[\\[[^\\]]+\\]\\]</match>\n"
            "        </context>\n"
            "        <context id=\"checkbox\" style-ref=\"checkbox\">\n"
            "          <match>\\[[ X\\-]\\]</match>\n"
            "        </context>\n"
            "        <context id=\"heading\" style-ref=\"header\">\n"
            "          <match>^\\*+ .*$</match>\n"
            "        </context>\n"
            "      </include>\n"
            "    </context>\n"
            "  </definitions>\n"
            "</language>\n";
        g_file_set_contents(lang_file, spec_xml, -1, NULL);
    }

    g_free(lang_file);
    g_free(spec_dir);
}

/* ------------------------------------------------------------------------- */
/* Text Tags Registration                                                    */
/* ------------------------------------------------------------------------- */

void ensure_orgmode_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    #define CREATE_TAG(name, ...) \
        if (!gtk_text_tag_table_lookup(tt, name)) { \
            gtk_text_buffer_create_tag(buffer, name, __VA_ARGS__, NULL); \
        }

    CREATE_TAG("org_title",
               "scale", 1.6,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff",
               "pixels-above-lines", 12,
               "pixels-below-lines", 6);

    CREATE_TAG("org_meta",
               "scale", 0.9,
               "style", PANGO_STYLE_ITALIC,
               "foreground", "#858585",
               "pixels-below-lines", 4);

    CREATE_TAG("org_banner",
               "scale", 0.9,
               "foreground", "#808080",
               "pixels-above-lines", 2,
               "pixels-below-lines", 6);

    CREATE_TAG("org_h1",
               "scale", 1.35,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("org_h2",
               "scale", 1.25,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ce9178",
               "pixels-above-lines", 10,
               "pixels-below-lines", 3);

    CREATE_TAG("org_h3",
               "scale", 1.15,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#dcdcaa",
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h4",
               "scale", 1.05,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h5",
               "scale", 1.0,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#c586c0",
               "pixels-above-lines", 4,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h6",
               "scale", 0.95,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#9cdcfe",
               "pixels-above-lines", 4,
               "pixels-below-lines", 2);

    CREATE_TAG("org_todo",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ffffff",
               "background", "#c74e39");

    CREATE_TAG("org_done",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ffffff",
               "background", "#388a34");

    CREATE_TAG("org_next",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ffffff",
               "background", "#0e639c");

    CREATE_TAG("org_waiting",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ffffff",
               "background", "#b8860b");

    CREATE_TAG("org_cancelled",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#808080",
               "background", "#2d2d2d",
               "strikethrough", TRUE);

    CREATE_TAG("org_done_text",
               "strikethrough", TRUE,
               "foreground", "#707070");

    CREATE_TAG("org_prio_a",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#f44747");

    CREATE_TAG("org_prio_b",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#e2c08d");

    CREATE_TAG("org_prio_c",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff");

    CREATE_TAG("org_tag",
               "scale", 0.85,
               "foreground", "#9cdcfe",
               "background", "#262626");

    CREATE_TAG("org_deadline_label",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#f14c4c");

    CREATE_TAG("org_scheduled_label",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0");

    CREATE_TAG("org_closed_label",
               "foreground", "#6e7681");

    CREATE_TAG("org_timestamp",
               "foreground", "#c586c0",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("org_inactive_ts",
               "foreground", "#6e7681");

    CREATE_TAG("org_drawer",
               "family", "monospace",
               "scale", 0.85,
               "foreground", "#6e7681",
               "background", "#181818");

    CREATE_TAG("org_src_block",
               "family", "monospace",
               "background", "#1e1e1e",
               "foreground", "#d4d4d4",
               "left-margin", 24,
               "right-margin", 24);

    CREATE_TAG("org_src_header",
               "family", "monospace",
               "scale", 0.85,
               "foreground", "#808080",
               "background", "#141414",
               "left-margin", 24);

    CREATE_TAG("org_quote",
               "style", PANGO_STYLE_ITALIC,
               "foreground", "#b0b0b0",
               "left-margin", 24);

    CREATE_TAG("org_table",
               "family", "monospace",
               "foreground", "#d4d4d4");

    CREATE_TAG("org_table_hdr",
               "family", "monospace",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff");

    CREATE_TAG("org_checkbox_checked",
               "foreground", "#73c991",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_checkbox_unchecked",
               "foreground", "#d4d4d4",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_checkbox_partial",
               "foreground", "#e2c08d",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_cookie",
               "foreground", "#4fc1ff",
               "scale", 0.9,
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_fold_cookie",
               "foreground", "#808080",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("org_underline",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("org_code",
               "family", "monospace",
               "foreground", "#ce9178",
               "background", "#2d2d2d");

    CREATE_TAG("org_strike",
               "strikethrough", TRUE,
               "foreground", "#808080");

    CREATE_TAG("org_link",
               "foreground", "#3794ff",
               "underline", PANGO_UNDERLINE_SINGLE);

    #undef CREATE_TAG
}

/* ------------------------------------------------------------------------- */
/* Parsing Helpers                                                           */
/* ------------------------------------------------------------------------- */

static gint get_heading_level(const gchar* line) {
    if (line == NULL || line[0] != '*') return 0;
    gint level = 0;
    while (line[level] == '*') {
        level++;
    }
    if (line[level] == ' ') {
        return level;
    }
    return 0;
}

static gboolean is_image_path(const gchar* path) {
    if (path == NULL) return FALSE;
    const gchar* ext = file_extension(path);
    return (g_ascii_strcasecmp(ext, "png") == 0 ||
            g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "gif") == 0 ||
            g_ascii_strcasecmp(ext, "svg") == 0 ||
            g_ascii_strcasecmp(ext, "webp") == 0 ||
            g_ascii_strcasecmp(ext, "bmp") == 0);
}

/* Inline markup renderer for Org format: *bold*, /italic/, _underline_, =verbatim=, ~code~, +strike+, [[link][desc]] */
static void render_inline_markup(GtkTextBuffer* buffer, GtkTextIter* iter,
                                 const gchar* text, const gchar* doc_dir,
                                 gboolean dim_done, OrgLineInfo* cur_line_info) {
    if (text == NULL || *text == '\0') return;

    const gchar* p = text;
    while (*p != '\0') {
        /* Org links: [[url][label]] or [[url]] */
        if (*p == '[' && p[1] == '[') {
            const gchar* link_end = strstr(p + 2, "]]");
            if (link_end != NULL) {
                gchar* inner = g_strndup(p + 2, link_end - (p + 2));
                const gchar* sep = strstr(inner, "][");
                gchar* target = NULL;
                gchar* label = NULL;
                if (sep != NULL) {
                    target = g_strndup(inner, sep - inner);
                    label = g_strdup(sep + 2);
                } else {
                    target = g_strdup(inner);
                    label = g_strdup(inner);
                }

                /* Check for file: prefix */
                const gchar* clean_target = target;
                if (g_str_has_prefix(clean_target, "file:")) {
                    clean_target += 5;
                }

                /* Check if it's an inline image */
                if (is_image_path(clean_target)) {
                    gchar* img_path = NULL;
                    if (g_path_is_absolute(clean_target)) {
                        img_path = g_strdup(clean_target);
                    } else if (doc_dir != NULL) {
                        img_path = g_build_filename(doc_dir, clean_target, NULL);
                    }
                    gboolean loaded = FALSE;
                    if (img_path != NULL && g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                        GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(img_path, 480, -1, TRUE, NULL);
                        if (pix != NULL) {
                            GdkTexture* tex = gdk_texture_new_for_pixbuf(pix);
                            gtk_text_buffer_insert_paintable(buffer, iter, GDK_PAINTABLE(tex));
                            g_object_unref(tex);
                            g_object_unref(pix);
                            loaded = TRUE;
                        }
                    }
                    g_free(img_path);
                    if (!loaded) {
                        gchar* ph = g_strdup_printf("🖼 [%s]", label ? label : target);
                        GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                        gtk_text_buffer_insert(buffer, iter, ph, -1);
                        GtkTextIter start;
                        gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
                        gtk_text_buffer_apply_tag_by_name(buffer, "org_link", &start, iter);
                        gtk_text_buffer_delete_mark(buffer, m);
                        g_free(ph);
                    }
                } else {
                    /* Regular link */
                    gchar* link_text = g_strdup_printf("🔗 %s", (label != NULL && label[0] != '\0') ? label : target);
                    GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                    gtk_text_buffer_insert(buffer, iter, link_text, -1);
                    GtkTextIter start;
                    gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
                    gtk_text_buffer_apply_tag_by_name(buffer, "org_link", &start, iter);
                    gtk_text_buffer_delete_mark(buffer, m);
                    g_free(link_text);

                    if (cur_line_info != NULL && cur_line_info->link_target == NULL) {
                        cur_line_info->link_target = g_strdup(clean_target);
                    }
                }

                g_free(inner);
                g_free(target);
                g_free(label);
                p = link_end + 2;
                continue;
            }
        }

        /* Active timestamp: <2026-10-28 Wed> */
        if (*p == '<' && isdigit(p[1])) {
            const gchar* ts_end = strchr(p, '>');
            if (ts_end != NULL && (ts_end - p) < 40) {
                gint len = ts_end - p + 1;
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                gtk_text_buffer_insert(buffer, iter, p, len);
                GtkTextIter start;
                gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_timestamp", &start, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                p = ts_end + 1;
                continue;
            }
        }

        /* Inactive timestamp: [2026-10-28 Wed] */
        if (*p == '[' && isdigit(p[1])) {
            const gchar* ts_end = strchr(p, ']');
            if (ts_end != NULL && (ts_end - p) < 40) {
                gint len = ts_end - p + 1;
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                gtk_text_buffer_insert(buffer, iter, p, len);
                GtkTextIter start;
                gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_inactive_ts", &start, iter);
                gtk_text_buffer_delete_mark(buffer, m);
                p = ts_end + 1;
                continue;
            }
        }

        /* Check for inline styling tokens: *bold*, /italic/, _underline_, =code=, ~code~, +strike+ */
        if ((*p == '*' || *p == '/' || *p == '_' || *p == '=' || *p == '~' || *p == '+') &&
            p > text && (isspace(p[-1]) || p[-1] == '(' || p[-1] == '{') && p[1] != '\0' && !isspace(p[1])) {
            gchar delim = *p;
            const gchar* end = strchr(p + 1, delim);
            if (end != NULL && end > p + 1 && (end[1] == '\0' || isspace(end[1]) || ispunct(end[1]))) {
                const gchar* tag_name = NULL;
                switch (delim) {
                    case '*': tag_name = "org_bold"; break;
                    case '/': tag_name = "org_italic"; break;
                    case '_': tag_name = "org_underline"; break;
                    case '=':
                    case '~': tag_name = "org_code"; break;
                    case '+': tag_name = "org_strike"; break;
                }
                gint content_len = end - (p + 1);
                GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                gtk_text_buffer_insert(buffer, iter, p + 1, content_len);
                GtkTextIter start;
                gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
                if (tag_name != NULL) {
                    gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, iter);
                }
                if (dim_done) {
                    gtk_text_buffer_apply_tag_by_name(buffer, "org_done_text", &start, iter);
                }
                gtk_text_buffer_delete_mark(buffer, m);
                p = end + 1;
                continue;
            }
        }

        /* Plain character */
        GtkTextMark* m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
        gtk_text_buffer_insert(buffer, iter, p, 1);
        if (dim_done) {
            GtkTextIter start;
            gtk_text_buffer_get_iter_at_mark(buffer, &start, m);
            gtk_text_buffer_apply_tag_by_name(buffer, "org_done_text", &start, iter);
        }
        gtk_text_buffer_delete_mark(buffer, m);
        p++;
    }
}

/* ------------------------------------------------------------------------- */
/* Main Orgmode Document Renderer                                            */
/* ------------------------------------------------------------------------- */

void render_orgmode_to_buffer(GtkTextBuffer* buffer,
                              const gchar* org_text,
                              const gchar* doc_path,
                              gboolean is_preview,
                              const gchar* file_name,
                              gint64 total_size,
                              GHashTable* folded_lines,
                              gint global_fold) {
    if (buffer == NULL || org_text == NULL) return;

    ensure_orgmode_tags(buffer);
    gtk_text_buffer_set_text(buffer, "", 0);

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);

    gchar* doc_dir = doc_path ? g_path_get_dirname(doc_path) : NULL;

    /* Line mappings for interactive click and navigation */
    GPtrArray* line_maps = g_ptr_array_new_with_free_func(org_line_info_free);

    gchar** lines = g_strsplit(org_text, "\n", -1);
    guint num_lines = g_strv_length(lines);

    /* First pass: detect title and document metadata */
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

    /* Render preview header banner if large file or metadata exists */
    if (is_preview && file_name != NULL) {
        GtkTextMark* bm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
        gchar* banner = g_strdup_printf("🦄 [ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)  •  Org Mode\n\n",
                                        file_name, total_size);
        gtk_text_buffer_insert(buffer, &iter, banner, -1);
        GtkTextIter bs;
        gtk_text_buffer_get_iter_at_mark(buffer, &bs, bm);
        gtk_text_buffer_apply_tag_by_name(buffer, "org_banner", &bs, &iter);
        gtk_text_buffer_delete_mark(buffer, bm);
        g_free(banner);
    }

    /* Render Document Title Banner if #+TITLE was found */
    if (title != NULL && title[0] != '\0') {
        GtkTextMark* tm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
        gtk_text_buffer_insert(buffer, &iter, title, -1);
        gtk_text_buffer_insert(buffer, &iter, "\n", -1);
        GtkTextIter ts;
        gtk_text_buffer_get_iter_at_mark(buffer, &ts, tm);
        gtk_text_buffer_apply_tag_by_name(buffer, "org_title", &ts, &iter);
        gtk_text_buffer_delete_mark(buffer, tm);

        if (author != NULL || date != NULL) {
            GString* meta = g_string_new("By ");
            if (author) g_string_append(meta, author);
            if (author && date) g_string_append(meta, "  •  ");
            if (date) g_string_append(meta, date);
            g_string_append(meta, "\n\n");

            GtkTextMark* mm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, meta->str, -1);
            GtkTextIter ms;
            gtk_text_buffer_get_iter_at_mark(buffer, &ms, mm);
            gtk_text_buffer_apply_tag_by_name(buffer, "org_meta", &ms, &iter);
            gtk_text_buffer_delete_mark(buffer, mm);
            g_string_free(meta, TRUE);
        } else {
            gtk_text_buffer_insert(buffer, &iter, "\n", -1);
        }
    }
    g_free(title);
    g_free(author);
    g_free(date);

    gint skip_until_heading_level = 0;
    gboolean in_src_block = FALSE;
    gboolean in_drawer = FALSE;

    for (guint i = 0; i < num_lines; i++) {
        const gchar* raw_line = lines[i];
        gint hlevel = get_heading_level(raw_line);

        /* Heading handling */
        if (hlevel > 0) {
            in_src_block = FALSE;
            in_drawer = FALSE;

            /* Check if this heading terminates a skip */
            if (skip_until_heading_level > 0) {
                if (hlevel <= skip_until_heading_level) {
                    skip_until_heading_level = 0;
                } else {
                    continue;
                }
            }

            /* Determine if this heading should be folded */
            gboolean is_folded = FALSE;
            if (global_fold == ORG_GLOBAL_OVERVIEW) {
                is_folded = (hlevel >= 1);
            } else if (global_fold == ORG_GLOBAL_CONTENTS) {
                is_folded = TRUE;
            } else if (folded_lines != NULL && g_hash_table_contains(folded_lines, GINT_TO_POINTER((gint)i))) {
                is_folded = TRUE;
            }

            gint buf_line = gtk_text_iter_get_line(&iter);
            OrgLineInfo* linfo = g_new0(OrgLineInfo, 1);
            linfo->buffer_line = buf_line;
            linfo->file_line = (gint)i;
            linfo->heading_level = hlevel;
            g_ptr_array_add(line_maps, linfo);

            /* Insert fold arrow */
            GtkTextMark* hm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            const gchar* arrow = is_folded ? "▶ " : "▼ ";
            gtk_text_buffer_insert(buffer, &iter, arrow, -1);

            /* Parse heading line content: [TODO/DONE] [#A] Title [:tag1:tag2:] */
            const gchar* rest = raw_line + hlevel + 1;
            while (*rest == ' ') rest++;

            /* Check for TODO keyword */
            const gchar* todo_kw = NULL;
            const gchar* todo_tag = NULL;
            if (g_str_has_prefix(rest, "TODO ") || g_strcmp0(rest, "TODO") == 0) {
                todo_kw = "TODO";
                todo_tag = "org_todo";
                rest += 4;
            } else if (g_str_has_prefix(rest, "DONE ") || g_strcmp0(rest, "DONE") == 0) {
                todo_kw = "DONE";
                todo_tag = "org_done";
                rest += 4;
            } else if (g_str_has_prefix(rest, "NEXT ") || g_strcmp0(rest, "NEXT") == 0) {
                todo_kw = "NEXT";
                todo_tag = "org_next";
                rest += 4;
            } else if (g_str_has_prefix(rest, "WAITING ") || g_strcmp0(rest, "WAITING") == 0) {
                todo_kw = "WAITING";
                todo_tag = "org_waiting";
                rest += 7;
            } else if (g_str_has_prefix(rest, "CANCELLED ") || g_strcmp0(rest, "CANCELLED") == 0) {
                todo_kw = "CANCELLED";
                todo_tag = "org_cancelled";
                rest += 9;
            }

            if (todo_kw != NULL) {
                linfo->todo_state = g_strdup(todo_kw);
                GtkTextMark* todo_m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gchar* badge = g_strdup_printf(" %s ", todo_kw);
                gtk_text_buffer_insert(buffer, &iter, badge, -1);
                g_free(badge);
                GtkTextIter todo_s;
                gtk_text_buffer_get_iter_at_mark(buffer, &todo_s, todo_m);
                gtk_text_buffer_apply_tag_by_name(buffer, todo_tag, &todo_s, &iter);
                gtk_text_buffer_delete_mark(buffer, todo_m);
                gtk_text_buffer_insert(buffer, &iter, " ", 1);
                while (*rest == ' ') rest++;
            }

            /* Check for priority cookie [#A], [#B], [#C] */
            if (rest[0] == '[' && rest[1] == '#' && rest[3] == ']') {
                gchar prio = rest[2];
                const gchar* prio_tag = (prio == 'A') ? "org_prio_a" : ((prio == 'B') ? "org_prio_b" : "org_prio_c");
                GtkTextMark* pm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gchar* prio_str = g_strdup_printf("[#%c] ", prio);
                gtk_text_buffer_insert(buffer, &iter, prio_str, -1);
                g_free(prio_str);
                GtkTextIter ps;
                gtk_text_buffer_get_iter_at_mark(buffer, &ps, pm);
                gtk_text_buffer_apply_tag_by_name(buffer, prio_tag, &ps, &iter);
                gtk_text_buffer_delete_mark(buffer, pm);
                rest += 4;
                while (*rest == ' ') rest++;
            }

            /* Check for tags at end of heading line: e.g. :work:urgent: */
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

            /* Render heading text */
            gboolean dim_done = (g_strcmp0(todo_kw, "DONE") == 0 || g_strcmp0(todo_kw, "CANCELLED") == 0);
            render_inline_markup(buffer, &iter, title_part, doc_dir, dim_done, linfo);
            g_free(title_part);

            /* Render tags badge if present */
            if (tag_part != NULL) {
                gtk_text_buffer_insert(buffer, &iter, "  ", 2);
                GtkTextMark* tag_m = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, tag_part, -1);
                GtkTextIter tag_s;
                gtk_text_buffer_get_iter_at_mark(buffer, &tag_s, tag_m);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_tag", &tag_s, &iter);
                gtk_text_buffer_delete_mark(buffer, tag_m);
                g_free(tag_part);
            }

            /* If folded, append ... */
            if (is_folded) {
                GtkTextMark* fm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, " ...", 4);
                GtkTextIter fs;
                gtk_text_buffer_get_iter_at_mark(buffer, &fs, fm);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_fold_cookie", &fs, &iter);
                gtk_text_buffer_delete_mark(buffer, fm);
            }

            /* Apply heading level tag (org_h1 .. org_h6) */
            GtkTextIter hs;
            gtk_text_buffer_get_iter_at_mark(buffer, &hs, hm);
            const gchar* htag = (hlevel == 1) ? "org_h1" :
                                (hlevel == 2) ? "org_h2" :
                                (hlevel == 3) ? "org_h3" :
                                (hlevel == 4) ? "org_h4" :
                                (hlevel == 5) ? "org_h5" : "org_h6";
            gtk_text_buffer_apply_tag_by_name(buffer, htag, &hs, &iter);
            if (dim_done) {
                gtk_text_buffer_apply_tag_by_name(buffer, "org_done_text", &hs, &iter);
            }
            gtk_text_buffer_delete_mark(buffer, hm);

            gtk_text_buffer_insert(buffer, &iter, "\n", 1);

            if (is_folded) {
                skip_until_heading_level = hlevel;
            }
            continue;
        }

        /* If currently skipping folded subtree, bypass line */
        if (skip_until_heading_level > 0) {
            continue;
        }

        /* Drawer lines: :PROPERTIES: ... :END: */
        gchar* trimmed = g_strstrip(g_strdup(raw_line));
        if (g_str_has_prefix(trimmed, ":PROPERTIES:") || g_str_has_prefix(trimmed, ":LOGBOOK:")) {
            in_drawer = TRUE;
        }
        if (in_drawer) {
            if (g_str_has_prefix(trimmed, ":END:")) {
                in_drawer = FALSE;
            }
            GtkTextMark* dm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, "  ", 2);
            gtk_text_buffer_insert(buffer, &iter, trimmed, -1);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            GtkTextIter ds;
            gtk_text_buffer_get_iter_at_mark(buffer, &ds, dm);
            gtk_text_buffer_apply_tag_by_name(buffer, "org_drawer", &ds, &iter);
            gtk_text_buffer_delete_mark(buffer, dm);
            g_free(trimmed);
            continue;
        }

        /* Source Code Blocks: #+BEGIN_SRC ... #+END_SRC */
        if (g_str_has_prefix(trimmed, "#+BEGIN_SRC") || g_str_has_prefix(trimmed, "#+begin_src")) {
            in_src_block = TRUE;
            GtkTextMark* shm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, "  ", 2);
            gtk_text_buffer_insert(buffer, &iter, trimmed, -1);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            GtkTextIter shs;
            gtk_text_buffer_get_iter_at_mark(buffer, &shs, shm);
            gtk_text_buffer_apply_tag_by_name(buffer, "org_src_header", &shs, &iter);
            gtk_text_buffer_delete_mark(buffer, shm);
            g_free(trimmed);
            continue;
        }
        if (in_src_block) {
            if (g_str_has_prefix(trimmed, "#+END_SRC") || g_str_has_prefix(trimmed, "#+end_src")) {
                in_src_block = FALSE;
                GtkTextMark* shm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, "  #+END_SRC\n\n", -1);
                GtkTextIter shs;
                gtk_text_buffer_get_iter_at_mark(buffer, &shs, shm);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_src_header", &shs, &iter);
                gtk_text_buffer_delete_mark(buffer, shm);
            } else {
                GtkTextMark* sbm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                gtk_text_buffer_insert(buffer, &iter, raw_line, -1);
                gtk_text_buffer_insert(buffer, &iter, "\n", 1);
                GtkTextIter sbs;
                gtk_text_buffer_get_iter_at_mark(buffer, &sbs, sbm);
                gtk_text_buffer_apply_tag_by_name(buffer, "org_src_block", &sbs, &iter);
                gtk_text_buffer_delete_mark(buffer, sbm);
            }
            g_free(trimmed);
            continue;
        }

        /* Planning lines: SCHEDULED:, DEADLINE:, CLOSED: */
        if (strstr(trimmed, "SCHEDULED:") != NULL || strstr(trimmed, "DEADLINE:") != NULL || strstr(trimmed, "CLOSED:") != NULL) {
            gtk_text_buffer_insert(buffer, &iter, "    ", 4);
            const gchar* p = trimmed;
            while (*p != '\0') {
                if (g_str_has_prefix(p, "DEADLINE:")) {
                    GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, "DEADLINE: ", 10);
                    GtkTextIter ls;
                    gtk_text_buffer_get_iter_at_mark(buffer, &ls, lm);
                    gtk_text_buffer_apply_tag_by_name(buffer, "org_deadline_label", &ls, &iter);
                    gtk_text_buffer_delete_mark(buffer, lm);
                    p += 9;
                    while (*p == ' ') p++;
                } else if (g_str_has_prefix(p, "SCHEDULED:")) {
                    GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, "SCHEDULED: ", 11);
                    GtkTextIter ls;
                    gtk_text_buffer_get_iter_at_mark(buffer, &ls, lm);
                    gtk_text_buffer_apply_tag_by_name(buffer, "org_scheduled_label", &ls, &iter);
                    gtk_text_buffer_delete_mark(buffer, lm);
                    p += 10;
                    while (*p == ' ') p++;
                } else if (g_str_has_prefix(p, "CLOSED:")) {
                    GtkTextMark* lm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                    gtk_text_buffer_insert(buffer, &iter, "CLOSED: ", 8);
                    GtkTextIter ls;
                    gtk_text_buffer_get_iter_at_mark(buffer, &ls, lm);
                    gtk_text_buffer_apply_tag_by_name(buffer, "org_closed_label", &ls, &iter);
                    gtk_text_buffer_delete_mark(buffer, lm);
                    p += 7;
                    while (*p == ' ') p++;
                } else if (*p == '<' || *p == '[') {
                    gchar close_char = (*p == '<') ? '>' : ']';
                    const gchar* end_ts = strchr(p, close_char);
                    if (end_ts != NULL) {
                        gint len = end_ts - p + 1;
                        GtkTextMark* tm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                        gtk_text_buffer_insert(buffer, &iter, p, len);
                        GtkTextIter ts;
                        gtk_text_buffer_get_iter_at_mark(buffer, &ts, tm);
                        gtk_text_buffer_apply_tag_by_name(buffer, (*p == '<') ? "org_timestamp" : "org_inactive_ts", &ts, &iter);
                        gtk_text_buffer_delete_mark(buffer, tm);
                        p = end_ts + 1;
                        while (*p == ' ') p++;
                        if (*p != '\0') gtk_text_buffer_insert(buffer, &iter, "  ", 2);
                        continue;
                    }
                    gtk_text_buffer_insert(buffer, &iter, p, 1);
                    p++;
                } else {
                    gtk_text_buffer_insert(buffer, &iter, p, 1);
                    p++;
                }
            }
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            g_free(trimmed);
            continue;
        }

        /* Frontmatter directives (#+TITLE, #+AUTHOR, etc. are already handled or can be rendered cleanly) */
        if (g_str_has_prefix(trimmed, "#+")) {
            GtkTextMark* fm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, trimmed, -1);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            GtkTextIter fs;
            gtk_text_buffer_get_iter_at_mark(buffer, &fs, fm);
            gtk_text_buffer_apply_tag_by_name(buffer, "org_meta", &fs, &iter);
            gtk_text_buffer_delete_mark(buffer, fm);
            g_free(trimmed);
            continue;
        }

        /* Checkbox and List items */
        const gchar* lead = raw_line;
        while (*lead == ' ' || *lead == '\t') lead++;
        gboolean is_list = (*lead == '-' || *lead == '+' || (*lead == '*' && lead > raw_line) || isdigit(*lead));
        if (is_list) {
            gint buf_line = gtk_text_iter_get_line(&iter);
            OrgLineInfo* linfo = g_new0(OrgLineInfo, 1);
            linfo->buffer_line = buf_line;
            linfo->file_line = (gint)i;

            /* Indentation */
            gint ind = lead - raw_line;
            for (gint k = 0; k < ind + 2; k++) {
                gtk_text_buffer_insert(buffer, &iter, " ", 1);
            }

            /* Advance past bullet */
            if (*lead == '-' || *lead == '+' || *lead == '*') {
                lead++;
                while (*lead == ' ') lead++;
            } else if (isdigit(*lead)) {
                while (isdigit(*lead) || *lead == '.' || *lead == ')') lead++;
                while (*lead == ' ') lead++;
            }

            /* Check for checkbox: [ ], [X], [-] */
            if (lead[0] == '[' && (lead[1] == ' ' || lead[1] == 'X' || lead[1] == 'x' || lead[1] == '-') && lead[2] == ']') {
                linfo->is_checkbox = TRUE;
                linfo->checkbox_done = (lead[1] == 'X' || lead[1] == 'x');

                GtkTextMark* cbm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
                const gchar* box_sym = linfo->checkbox_done ? "☑ " : ((lead[1] == '-') ? "⊟ " : "☐ ");
                const gchar* cbtag = linfo->checkbox_done ? "org_checkbox_checked" : ((lead[1] == '-') ? "org_checkbox_partial" : "org_checkbox_unchecked");

                gtk_text_buffer_insert(buffer, &iter, box_sym, -1);
                GtkTextIter cbs;
                gtk_text_buffer_get_iter_at_mark(buffer, &cbs, cbm);
                gtk_text_buffer_apply_tag_by_name(buffer, cbtag, &cbs, &iter);
                gtk_text_buffer_delete_mark(buffer, cbm);

                lead += 3;
                while (*lead == ' ') lead++;
            } else {
                /* Ordinary bullet */
                gtk_text_buffer_insert(buffer, &iter, "• ", -1);
            }

            g_ptr_array_add(line_maps, linfo);

            render_inline_markup(buffer, &iter, lead, doc_dir, linfo->checkbox_done, linfo);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            g_free(trimmed);
            continue;
        }

        /* Tables: lines starting with '|' */
        if (*trimmed == '|') {
            GtkTextMark* tm = gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);
            gtk_text_buffer_insert(buffer, &iter, "  ", 2);
            gtk_text_buffer_insert(buffer, &iter, trimmed, -1);
            gtk_text_buffer_insert(buffer, &iter, "\n", 1);
            GtkTextIter ts;
            gtk_text_buffer_get_iter_at_mark(buffer, &ts, tm);
            if (strstr(trimmed, "|-") != NULL || strstr(trimmed, "|=") != NULL) {
                gtk_text_buffer_apply_tag_by_name(buffer, "org_drawer", &ts, &iter);
            } else {
                gtk_text_buffer_apply_tag_by_name(buffer, "org_table", &ts, &iter);
            }
            gtk_text_buffer_delete_mark(buffer, tm);
            g_free(trimmed);
            continue;
        }

        /* Regular text line */
        gint buf_line = gtk_text_iter_get_line(&iter);
        OrgLineInfo* linfo = g_new0(OrgLineInfo, 1);
        linfo->buffer_line = buf_line;
        linfo->file_line = (gint)i;
        g_ptr_array_add(line_maps, linfo);

        gtk_text_buffer_insert(buffer, &iter, "  ", 2);
        render_inline_markup(buffer, &iter, raw_line, doc_dir, FALSE, linfo);
        gtk_text_buffer_insert(buffer, &iter, "\n", 1);
        g_free(trimmed);
    }

    g_strfreev(lines);
    g_free(doc_dir);

    /* Store line mappings on buffer */
    g_object_set_data_full(G_OBJECT(buffer), "org_line_maps", line_maps, (GDestroyNotify)g_ptr_array_unref);
}

/* ------------------------------------------------------------------------- */
/* Interactive Orgmode Actions (Click, TODO, Checkbox, Folding)              */
/* ------------------------------------------------------------------------- */

static OrgLineInfo* find_line_info(GtkTextBuffer* buffer, gint target_buf_line) {
    if (buffer == NULL) return NULL;
    GPtrArray* line_maps = (GPtrArray*)g_object_get_data(G_OBJECT(buffer), "org_line_maps");
    if (line_maps == NULL) return NULL;

    for (guint i = 0; i < line_maps->len; i++) {
        OrgLineInfo* info = (OrgLineInfo*)g_ptr_array_index(line_maps, i);
        if (info != NULL && info->buffer_line == target_buf_line) {
            return info;
        }
    }
    return NULL;
}

gboolean orgmode_toggle_heading_fold(AppData* app, gint file_line) {
    if (app == NULL) return FALSE;
    if (app->org_folded_lines == NULL) {
        app->org_folded_lines = g_hash_table_new(g_direct_hash, g_direct_equal);
    }

    gpointer key = GINT_TO_POINTER(file_line);
    if (g_hash_table_contains(app->org_folded_lines, key)) {
        g_hash_table_remove(app->org_folded_lines, key);
    } else {
        g_hash_table_add(app->org_folded_lines, key);
    }

    if (app->viewed_path != NULL && is_org_file(app->viewed_path)) {
        load_file_content(app, app->viewed_name, app->viewed_path);
        return TRUE;
    }
    return FALSE;
}

void orgmode_cycle_global_fold(AppData* app) {
    if (app == NULL) return;
    app->org_global_fold = (app->org_global_fold + 1) % 3;
    if (app->org_folded_lines != NULL) {
        g_hash_table_remove_all(app->org_folded_lines);
    }
    if (app->viewed_path != NULL && is_org_file(app->viewed_path)) {
        load_file_content(app, app->viewed_name, app->viewed_path);
    }
}

gboolean orgmode_cycle_todo_at_line(const gchar* file_path, gint file_line, GtkTextBuffer* editor_buffer) {
    gchar* contents = NULL;
    gsize len = 0;

    if (editor_buffer != NULL) {
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(editor_buffer, &s, &e);
        contents = gtk_text_buffer_get_text(editor_buffer, &s, &e, FALSE);
    } else if (file_path != NULL && g_file_get_contents(file_path, &contents, &len, NULL)) {
        /* loaded from disk */
    } else {
        return FALSE;
    }

    gchar** lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    guint n_lines = g_strv_length(lines);
    if ((guint)file_line >= n_lines) {
        g_strfreev(lines);
        return FALSE;
    }

    const gchar* line = lines[file_line];
    gint hlevel = get_heading_level(line);
    if (hlevel <= 0) {
        g_strfreev(lines);
        return FALSE;
    }

    const gchar* rest = line + hlevel + 1;
    while (*rest == ' ') rest++;

    /* Identify current TODO state */
    const gchar* next_kw = NULL;
    const gchar* after_kw = rest;

    if (g_str_has_prefix(rest, "TODO ") || g_strcmp0(rest, "TODO") == 0) {
        next_kw = "NEXT";
        after_kw = rest + 4;
    } else if (g_str_has_prefix(rest, "NEXT ") || g_strcmp0(rest, "NEXT") == 0) {
        next_kw = "DONE";
        after_kw = rest + 4;
    } else if (g_str_has_prefix(rest, "DONE ") || g_strcmp0(rest, "DONE") == 0) {
        next_kw = NULL; /* remove */
        after_kw = rest + 4;
    } else if (g_str_has_prefix(rest, "WAITING ") || g_strcmp0(rest, "WAITING") == 0) {
        next_kw = "DONE";
        after_kw = rest + 7;
    } else if (g_str_has_prefix(rest, "CANCELLED ") || g_strcmp0(rest, "CANCELLED") == 0) {
        next_kw = NULL; /* remove */
        after_kw = rest + 9;
    } else {
        next_kw = "TODO";
        after_kw = rest;
    }

    while (*after_kw == ' ') after_kw++;

    /* Build stars */
    GString* stars = g_string_new(NULL);
    for (gint k = 0; k < hlevel; k++) {
        g_string_append_c(stars, '*');
    }

    gchar* new_line = NULL;
    if (next_kw != NULL) {
        new_line = g_strdup_printf("%s %s %s", stars->str, next_kw, after_kw);
    } else {
        new_line = g_strdup_printf("%s %s", stars->str, after_kw);
    }
    g_string_free(stars, TRUE);

    g_free(lines[file_line]);
    lines[file_line] = new_line;

    gchar* updated_text = g_strjoinv("\n", lines);
    g_strfreev(lines);

    if (editor_buffer != NULL) {
        gtk_text_buffer_set_text(editor_buffer, updated_text, -1);
    }
    if (file_path != NULL) {
        g_file_set_contents(file_path, updated_text, -1, NULL);
    }
    g_free(updated_text);

    return TRUE;
}

/* Update progress cookies [X/Y] and [X%] on parent heading / list item */
static void update_parent_progress_cookie(gchar** lines, guint n_lines, gint file_line) {
    if (lines == NULL || (guint)file_line >= n_lines) return;

    /* Scan backwards to find the nearest parent heading or list item */
    gint parent_line = -1;
    for (gint p = file_line - 1; p >= 0; p--) {
        if (get_heading_level(lines[p]) > 0 || strstr(lines[p], "[/") != NULL || strstr(lines[p], "[%") != NULL) {
            parent_line = p;
            break;
        }
    }
    if (parent_line < 0) return;

    /* Check if parent line contains progress cookie: [X/Y] or [X%] */
    const gchar* pstr = lines[parent_line];
    const gchar* c_open = strstr(pstr, "[");
    if (c_open == NULL) return;
    const gchar* c_close = strstr(c_open, "]");
    if (c_close == NULL) return;

    gboolean has_slash = (strstr(c_open, "/") != NULL && strstr(c_open, "/") < c_close);
    gboolean has_percent = (strstr(c_open, "%") != NULL && strstr(c_open, "%") < c_close);
    if (!has_slash && !has_percent) return;

    /* Count sibling checkboxes under parent_line */
    gint total = 0;
    gint checked = 0;
    gint p_hlevel = get_heading_level(pstr);

    for (guint k = parent_line + 1; k < n_lines; k++) {
        gint h = get_heading_level(lines[k]);
        if (p_hlevel > 0 && h > 0 && h <= p_hlevel) {
            break; /* Next heading at same or higher level ends subtree */
        }
        const gchar* l = lines[k];
        if (strstr(l, "[ ]") != NULL || strstr(l, "[X]") != NULL || strstr(l, "[x]") != NULL || strstr(l, "[-]") != NULL) {
            total++;
            if (strstr(l, "[X]") != NULL || strstr(l, "[x]") != NULL) {
                checked++;
            }
        }
    }
    if (total == 0) return;

    /* Replace cookie in parent_line */
    gchar* before = g_strndup(pstr, c_open - pstr);
    const gchar* after = c_close + 1;
    gchar* new_cookie = has_slash ? g_strdup_printf("[%d/%d]", checked, total)
                                  : g_strdup_printf("[%d%%]", (checked * 100) / total);
    gchar* updated_parent = g_strconcat(before, new_cookie, after, NULL);
    g_free(before);
    g_free(new_cookie);
    g_free(lines[parent_line]);
    lines[parent_line] = updated_parent;
}

gboolean orgmode_toggle_checkbox_at_line(const gchar* file_path, gint file_line, GtkTextBuffer* editor_buffer) {
    gchar* contents = NULL;
    gsize len = 0;

    if (editor_buffer != NULL) {
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(editor_buffer, &s, &e);
        contents = gtk_text_buffer_get_text(editor_buffer, &s, &e, FALSE);
    } else if (file_path != NULL && g_file_get_contents(file_path, &contents, &len, NULL)) {
        /* loaded from disk */
    } else {
        return FALSE;
    }

    gchar** lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    guint n_lines = g_strv_length(lines);
    if ((guint)file_line >= n_lines) {
        g_strfreev(lines);
        return FALSE;
    }

    gchar* line = lines[file_line];
    gchar* box = strstr(line, "[ ]");
    gboolean toggled = FALSE;
    if (box != NULL) {
        box[1] = 'X';
        toggled = TRUE;
    } else {
        box = strstr(line, "[X]");
        if (box == NULL) box = strstr(line, "[x]");
        if (box == NULL) box = strstr(line, "[-]");
        if (box != NULL) {
            box[1] = ' ';
            toggled = TRUE;
        }
    }

    if (!toggled) {
        g_strfreev(lines);
        return FALSE;
    }

    /* Update parent progress cookie if present */
    update_parent_progress_cookie(lines, n_lines, file_line);

    gchar* updated_text = g_strjoinv("\n", lines);
    g_strfreev(lines);

    if (editor_buffer != NULL) {
        gtk_text_buffer_set_text(editor_buffer, updated_text, -1);
    }
    if (file_path != NULL) {
        g_file_set_contents(file_path, updated_text, -1, NULL);
    }
    g_free(updated_text);

    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Viewer Interaction Dispatcher                                             */
/* ------------------------------------------------------------------------- */

gboolean orgmode_handle_click(AppData* app, gdouble x, gdouble y) {
    if (app == NULL || app->text_view == NULL || app->buffer == NULL) return FALSE;
    if (app->viewed_name == NULL || !is_org_file(app->viewed_name)) return FALSE;
    if (app->render_raw) return FALSE;

    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(app->text_view), &iter, (gint)x, (gint)y);
    gint buf_line = gtk_text_iter_get_line(&iter);

    OrgLineInfo* info = find_line_info(app->buffer, buf_line);
    if (info == NULL) return FALSE;

    /* 1. If clicked on link, open it */
    if (info->link_target != NULL) {
        if (strstr(info->link_target, "://") != NULL) {
            gtk_show_uri(app->window, info->link_target, GDK_CURRENT_TIME);
            return TRUE;
        } else if (g_file_test(info->link_target, G_FILE_TEST_EXISTS)) {
            editor_open_file(app, info->link_target, info->link_target);
            return TRUE;
        }
    }

    /* Check if click was on TODO tag */
    gboolean on_todo = gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_todo")) ||
                       gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_done")) ||
                       gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_next")) ||
                       gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_waiting"));

    if (on_todo || info->heading_level > 0) {
        if (on_todo) {
            /* Cycle TODO state */
            orgmode_cycle_todo_at_line(app->viewed_path, info->file_line, NULL);
            load_file_content(app, app->viewed_name, app->viewed_path);
            return TRUE;
        } else {
            /* Toggle heading folding */
            orgmode_toggle_heading_fold(app, info->file_line);
            return TRUE;
        }
    }

    /* 2. If line has checkbox, toggle it */
    if (info->is_checkbox) {
        orgmode_toggle_checkbox_at_line(app->viewed_path, info->file_line, NULL);
        load_file_content(app, app->viewed_name, app->viewed_path);
        return TRUE;
    }

    return FALSE;
}

gboolean orgmode_handle_viewer_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->viewed_name == NULL || !is_org_file(app->viewed_name)) return FALSE;
    if (app->render_raw) return FALSE;

    /* Shift+Tab: Global Visibility Cycling */
    if ((keyval == GDK_KEY_ISO_Left_Tab) || (keyval == GDK_KEY_Tab && (state & GDK_SHIFT_MASK))) {
        orgmode_cycle_global_fold(app);
        return TRUE;
    }

    /* Tab: Fold / Unfold heading at current cursor or top of view */
    if (keyval == GDK_KEY_Tab) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, gtk_text_buffer_get_insert(app->buffer));
        gint buf_line = gtk_text_iter_get_line(&iter);
        OrgLineInfo* info = find_line_info(app->buffer, buf_line);
        if (info != NULL && info->heading_level > 0) {
            orgmode_toggle_heading_fold(app, info->file_line);
            return TRUE;
        }
        /* If not on heading, fold first heading above or global cycle */
        orgmode_cycle_global_fold(app);
        return TRUE;
    }

    /* 't': Cycle TODO state of current heading */
    if (keyval == GDK_KEY_t && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK))) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, gtk_text_buffer_get_insert(app->buffer));
        gint buf_line = gtk_text_iter_get_line(&iter);
        OrgLineInfo* info = find_line_info(app->buffer, buf_line);
        if (info != NULL && info->heading_level > 0) {
            orgmode_cycle_todo_at_line(app->viewed_path, info->file_line, NULL);
            load_file_content(app, app->viewed_name, app->viewed_path);
            return TRUE;
        }
    }

    /* Space or Return on checkbox: toggle */
    if ((keyval == GDK_KEY_space || keyval == GDK_KEY_Return) && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK))) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, gtk_text_buffer_get_insert(app->buffer));
        gint buf_line = gtk_text_iter_get_line(&iter);
        OrgLineInfo* info = find_line_info(app->buffer, buf_line);
        if (info != NULL && info->is_checkbox) {
            orgmode_toggle_checkbox_at_line(app->viewed_path, info->file_line, NULL);
            load_file_content(app, app->viewed_name, app->viewed_path);
            return TRUE;
        }
    }

    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Editor Orgmode Key Shortcuts                                              */
/* ------------------------------------------------------------------------- */

gboolean orgmode_editor_handle_key(AppData* app, EditorTab* tab, guint keyval, GdkModifierType state) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->name == NULL) return FALSE;
    if (!is_org_file(tab->name)) return FALSE;

    /* 1. Shift+Tab: Global Visibility Cycle */
    if ((keyval == GDK_KEY_ISO_Left_Tab) || (keyval == GDK_KEY_Tab && (state & GDK_SHIFT_MASK))) {
        /* Cycle overview / contents / all in preview */
        orgmode_cycle_global_fold(app);
        return TRUE;
    }

    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint cur_line = gtk_text_iter_get_line(&cur);

    /* 2. Shift+Right / Shift+Left: Cycle TODO State */
    if ((keyval == GDK_KEY_Right || keyval == GDK_KEY_Left) && (state & GDK_SHIFT_MASK)) {
        if (orgmode_cycle_todo_at_line(tab->path, cur_line, tab->buffer)) {
            gtk_text_buffer_set_modified(tab->buffer, TRUE);
            editor_tab_label_update(tab);
            return TRUE;
        }
    }

    /* 3. Alt+Return: Org Insert Heading at current level (M-RET) */
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && (state & GDK_ALT_MASK)) {
        GtkTextIter line_start = cur;
        gtk_text_iter_set_line_offset(&line_start, 0);
        GtkTextIter line_end = line_start;
        gtk_text_iter_forward_to_line_end(&line_end);
        gchar* line_text = gtk_text_buffer_get_text(tab->buffer, &line_start, &line_end, FALSE);
        gint hlevel = get_heading_level(line_text);
        g_free(line_text);

        if (hlevel == 0) hlevel = 1;

        GString* new_h = g_string_new("\n");
        for (gint k = 0; k < hlevel; k++) {
            g_string_append_c(new_h, '*');
        }
        g_string_append(new_h, " ");

        gtk_text_buffer_insert(tab->buffer, &cur, new_h->str, -1);
        g_string_free(new_h, TRUE);
        gtk_text_buffer_set_modified(tab->buffer, TRUE);
        editor_tab_label_update(tab);
        return TRUE;
    }

    /* 4. Ctrl+Return or Space on checkbox: Toggle Checkbox */
    if ((keyval == GDK_KEY_Return && (state & GDK_CONTROL_MASK)) ||
        (keyval == GDK_KEY_space && (state & GDK_CONTROL_MASK))) {
        if (orgmode_toggle_checkbox_at_line(tab->path, cur_line, tab->buffer)) {
            gtk_text_buffer_set_modified(tab->buffer, TRUE);
            editor_tab_label_update(tab);
            return TRUE;
        }
    }

    return FALSE;
}

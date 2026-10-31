#include "orgmode.h"
#include "fonts.h"
#include "text_tags.h"
#include "org_doc.h"
#include "image_utils.h"
#include "app_events.h"
#include "file_types.h"
#include "editor.h"

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
    gchar* todo_state;      /* "TODO", "DONE", "NEXT", "RUNNING", "WAITING", etc., or NULL */
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

static const gchar* ORG_LANG_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<language id=\"org\" name=\"Org\" version=\"2.0\" _section=\"Markup\">\n"
    "  <metadata>\n"
    "    <property name=\"mimetypes\">text/x-org;text/org</property>\n"
    "    <property name=\"globs\">*.org;*.org_archive</property>\n"
    "    <property name=\"line-comment-start\"># </property>\n"
    "  </metadata>\n"
    "  <styles>\n"
    "    <style id=\"header1\" name=\"Heading 1\" map-to=\"def:type\"/>\n"
    "    <style id=\"header2\" name=\"Heading 2\" map-to=\"def:function\"/>\n"
    "    <style id=\"header3\" name=\"Heading 3\" map-to=\"def:type\"/>\n"
    "    <style id=\"header4\" name=\"Heading 4+\" map-to=\"def:preprocessor\"/>\n"
    "    <style id=\"todo\" name=\"TODO keyword\" map-to=\"def:error\"/>\n"
    "    <style id=\"running\" name=\"RUNNING keyword\" map-to=\"def:special-char\"/>\n"
    "    <style id=\"done\" name=\"DONE keyword\" map-to=\"def:comment\"/>\n"
    "    <style id=\"priority\" name=\"Priority\" map-to=\"def:special-char\"/>\n"
    "    <style id=\"tag\" name=\"Tag\" map-to=\"def:identifier\"/>\n"
    "    <style id=\"cookie\" name=\"Statistics cookie\" map-to=\"def:decimal\"/>\n"
    "    <style id=\"comment\" name=\"Comment\" map-to=\"def:comment\"/>\n"
    "    <style id=\"keyword\" name=\"Keyword line\" map-to=\"def:preprocessor\"/>\n"
    "    <style id=\"drawer\" name=\"Drawer\" map-to=\"def:comment\"/>\n"
    "    <style id=\"planning\" name=\"Planning keyword\" map-to=\"def:keyword\"/>\n"
    "    <style id=\"timestamp\" name=\"Timestamp\" map-to=\"def:special-char\"/>\n"
    "    <style id=\"link\" name=\"Link\" map-to=\"def:underlined\"/>\n"
    "    <style id=\"checkbox\" name=\"Checkbox\" map-to=\"def:keyword\"/>\n"
    "    <style id=\"checkbox-done\" name=\"Checked checkbox\" map-to=\"def:comment\"/>\n"
    "    <style id=\"bullet\" name=\"List bullet\" map-to=\"def:keyword\"/>\n"
    "    <style id=\"table\" name=\"Table\" map-to=\"def:decimal\"/>\n"
    "    <style id=\"bold\" name=\"Bold\" map-to=\"def:strong-emphasis\"/>\n"
    "    <style id=\"italic\" name=\"Italic\" map-to=\"def:emphasis\"/>\n"
    "    <style id=\"code\" name=\"Code\" map-to=\"def:string\"/>\n"
    "    <style id=\"strike\" name=\"Strike-through\" map-to=\"def:comment\"/>\n"
    "    <style id=\"underline\" name=\"Underline\" map-to=\"def:underlined\"/>\n"
    "    <style id=\"block\" name=\"Block delimiter\" map-to=\"def:comment\"/>\n"
    "    <style id=\"block-content\" name=\"Block content\" map-to=\"def:string\"/>\n"
    "  </styles>\n"
    "  <definitions>\n"
    "    <context id=\"todo\" style-ref=\"todo\">\n"
    "      <match>(?&lt;=\\s)(TODO|NEXT|WAITING|WAIT|HOLD|STARTED|BUG)(?=\\s|$)</match>\n"
    "    </context>\n"
    "    <context id=\"running\" style-ref=\"running\">\n"
    "      <match>(?&lt;=\\s)RUNNING(?=\\s|$)</match>\n"
    "    </context>\n"
    "    <context id=\"done\" style-ref=\"done\">\n"
    "      <match>(?&lt;=\\s)(DONE|CANCELLED|CANCELED|FIXED)(?=\\s|$)</match>\n"
    "    </context>\n"
    "    <context id=\"priority\" style-ref=\"priority\">\n"
    "      <match>\\[#[A-Z0-9]\\]</match>\n"
    "    </context>\n"
    "    <context id=\"tags\" style-ref=\"tag\">\n"
    "      <match>(?&lt;=\\s):[A-Za-z0-9_@#%:]+:\\s*$</match>\n"
    "    </context>\n"
    "    <context id=\"cookie\" style-ref=\"cookie\">\n"
    "      <match>\\[[0-9]*(/[0-9]*|%)\\]</match>\n"
    "    </context>\n"
    "    <context id=\"timestamp\" style-ref=\"timestamp\">\n"
    "      <match>[&lt;\\[][0-9]{4}-[0-9]{2}-[0-9]{2}[^&gt;\\]\\n]*[&gt;\\]](--[&lt;\\[][0-9]{4}-[0-9]{2}-[0-9]{2}[^&gt;\\]\\n]*[&gt;\\]])?</match>\n"
    "    </context>\n"
    "    <context id=\"link\" style-ref=\"link\">\n"
    "      <match>\\[\\[[^\\]\\n]+\\](\\[[^\\]\\n]+\\])?\\]|\\b(https?|file|mailto):[^\\s\\]&gt;)\"]+</match>\n"
    "    </context>\n"
    "    <context id=\"emphasis\">\n"
    "      <include>\n"
    "        <context id=\"bold\" style-ref=\"bold\">\n"
    "          <match>(?&lt;![\\w*])\\*[^\\s*](?:[^*\\n]*[^\\s*])?\\*(?![\\w*])</match>\n"
    "        </context>\n"
    "        <context id=\"italic\" style-ref=\"italic\">\n"
    "          <match>(?&lt;![\\w/:])/[^\\s/](?:[^/\\n]*[^\\s/])?/(?![\\w/])</match>\n"
    "        </context>\n"
    "        <context id=\"verbatim\" style-ref=\"code\">\n"
    "          <match>(?&lt;![\\w=])=[^\\s=](?:[^=\\n]*[^\\s=])?=(?![\\w=])</match>\n"
    "        </context>\n"
    "        <context id=\"code\" style-ref=\"code\">\n"
    "          <match>(?&lt;![\\w~])~[^\\s~](?:[^~\\n]*[^\\s~])?~(?![\\w~])</match>\n"
    "        </context>\n"
    "        <context id=\"strike\" style-ref=\"strike\">\n"
    "          <match>(?&lt;![\\w+])\\+[^\\s+](?:[^+\\n]*[^\\s+])?\\+(?![\\w+])</match>\n"
    "        </context>\n"
    "        <context id=\"underline\" style-ref=\"underline\">\n"
    "          <match>(?&lt;![\\w_])_[^\\s_](?:[^_\\n]*[^\\s_])?_(?![\\w_])</match>\n"
    "        </context>\n"
    "      </include>\n"
    "    </context>\n"
    "    <context id=\"inline\">\n"
    "      <include>\n"
    "        <context ref=\"timestamp\"/>\n"
    "        <context ref=\"link\"/>\n"
    "        <context ref=\"cookie\"/>\n"
    "        <context ref=\"emphasis\"/>\n"
    "      </include>\n"
    "    </context>\n"
    "    <context id=\"org\">\n"
    "      <include>\n"
    "        <context id=\"header1\" style-ref=\"header1\" end-at-line-end=\"true\">\n"
    "          <start>^\\*(?=\\s)</start>\n"
    "          <include><context ref=\"todo\"/><context ref=\"running\"/><context ref=\"done\"/><context ref=\"priority\"/><context ref=\"tags\"/><context ref=\"inline\"/></include>\n"
    "        </context>\n"
    "        <context id=\"header2\" style-ref=\"header2\" end-at-line-end=\"true\">\n"
    "          <start>^\\*{2}(?=\\s)</start>\n"
    "          <include><context ref=\"todo\"/><context ref=\"running\"/><context ref=\"done\"/><context ref=\"priority\"/><context ref=\"tags\"/><context ref=\"inline\"/></include>\n"
    "        </context>\n"
    "        <context id=\"header3\" style-ref=\"header3\" end-at-line-end=\"true\">\n"
    "          <start>^\\*{3}(?=\\s)</start>\n"
    "          <include><context ref=\"todo\"/><context ref=\"running\"/><context ref=\"done\"/><context ref=\"priority\"/><context ref=\"tags\"/><context ref=\"inline\"/></include>\n"
    "        </context>\n"
    "        <context id=\"header4\" style-ref=\"header4\" end-at-line-end=\"true\">\n"
    "          <start>^\\*{4,}(?=\\s)</start>\n"
    "          <include><context ref=\"todo\"/><context ref=\"running\"/><context ref=\"done\"/><context ref=\"priority\"/><context ref=\"tags\"/><context ref=\"inline\"/></include>\n"
    "        </context>\n"
    "        <context id=\"block\" style-inside=\"true\" style-ref=\"block-content\">\n"
    "          <start>(?i)^\\s*#\\+begin_(\\w+).*$</start>\n"
    "          <end>(?i)^\\s*#\\+end_\\%{1@start}\\s*$</end>\n"
    "          <include>\n"
    "            <context sub-pattern=\"0\" where=\"start\" style-ref=\"block\"/>\n"
    "            <context sub-pattern=\"0\" where=\"end\" style-ref=\"block\"/>\n"
    "          </include>\n"
    "        </context>\n"
    "        <context id=\"keyword-line\" style-ref=\"keyword\">\n"
    "          <match>^\\s*#\\+\\w+:.*$</match>\n"
    "        </context>\n"
    "        <context id=\"comment\" style-ref=\"comment\">\n"
    "          <match>^\\s*#(\\s.*)?$</match>\n"
    "        </context>\n"
    "        <context id=\"drawer\" style-ref=\"drawer\">\n"
    "          <match>^\\s*:[A-Za-z0-9_-]+:(?=\\s|$)</match>\n"
    "        </context>\n"
    "        <context id=\"planning\" style-ref=\"planning\">\n"
    "          <match>\\b(SCHEDULED|DEADLINE|CLOSED|CLOCK):</match>\n"
    "        </context>\n"
    "        <context id=\"table\" style-ref=\"table\">\n"
    "          <match>^\\s*\\|.*$</match>\n"
    "        </context>\n"
    "        <context id=\"checkbox-done\" style-ref=\"checkbox-done\">\n"
    "          <match>(?&lt;=\\s)\\[[Xx]\\](?=\\s|$)</match>\n"
    "        </context>\n"
    "        <context id=\"checkbox\" style-ref=\"checkbox\">\n"
    "          <match>(?&lt;=\\s)\\[[ -]\\](?=\\s|$)</match>\n"
    "        </context>\n"
    "        <context id=\"bullet\" style-ref=\"bullet\">\n"
    "          <match>^\\s*([-+]|[0-9]+[.)])(?=\\s|$)|^\\s+\\*(?=\\s)</match>\n"
    "        </context>\n"
    "        <context ref=\"inline\"/>\n"
    "      </include>\n"
    "    </context>\n"
    "  </definitions>\n"
    "</language>\n";

void orgmode_install_gtksourceview_spec(void) {
    gchar* spec_dir = g_build_filename(g_get_user_data_dir(), "gtksourceview-5", "language-specs", NULL);
    g_mkdir_with_parents(spec_dir, 0755);
    gchar* lang_file = g_build_filename(spec_dir, "org.lang", NULL);
    /* Rewrite an older version of the spec */
    gchar* current = NULL;
    if (!g_file_get_contents(lang_file, &current, NULL, NULL) || g_strcmp0(current, ORG_LANG_XML) != 0) {
        g_file_set_contents(lang_file, ORG_LANG_XML, -1, NULL);
    }
    g_free(current);
    g_free(lang_file);
    g_free(spec_dir);
}

/* ------------------------------------------------------------------------- */
/* Text Tags Registration                                                    */
/* ------------------------------------------------------------------------- */

void ensure_orgmode_tags(GtkTextBuffer* buffer) {

    /* Colours and fonts as roles ("@accent"): they follow theme changes */
    #define CREATE_TAG(name, ...) text_tag_ensure(buffer, name, __VA_ARGS__, NULL)

    CREATE_TAG("org_title",
               "scale", 1.6,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@accent_fg",
               "pixels-above-lines", 12,
               "pixels-below-lines", 6);

    CREATE_TAG("org_meta",
               "scale", FONT_SCALE_SMALL,
               "style", PANGO_STYLE_ITALIC,
               "foreground", "@fg_subtle",
               "pixels-below-lines", 4);

    CREATE_TAG("org_banner",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "pixels-above-lines", 2,
               "pixels-below-lines", 6);

    CREATE_TAG("org_h1",
               "scale", 1.35,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_constant",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("org_h2",
               "scale", 1.25,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_function",
               "pixels-above-lines", 10,
               "pixels-below-lines", 3);

    CREATE_TAG("org_h3",
               "scale", 1.15,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_type",
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h4",
               "scale", 1.05,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_control",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h5",
               "scale", 1.0,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_string",
               "pixels-above-lines", 4,
               "pixels-below-lines", 2);

    CREATE_TAG("org_h6",
               "scale", 0.95,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_variable",
               "pixels-above-lines", 4,
               "pixels-below-lines", 2);

    CREATE_TAG("org_todo",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@badge_fg",
               "background", "@badge_red");

    CREATE_TAG("org_done",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@badge_fg",
               "background", "@badge_green");

    CREATE_TAG("org_next",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@badge_fg",
               "background", "@badge_blue");

    CREATE_TAG("org_running",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@highlight_fg",
               "background", "@highlight");

    CREATE_TAG("org_waiting",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@badge_fg",
               "background", "@badge_orange");

    CREATE_TAG("org_cancelled",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_subtle",
               "background", "@bg_code_inline",
               "strikethrough", TRUE);

    CREATE_TAG("org_done_text",
               "strikethrough", TRUE,
               "foreground", "@fg_faint");

    CREATE_TAG("org_prio_a",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@error");

    CREATE_TAG("org_prio_b",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_escape");

    CREATE_TAG("org_prio_c",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@accent_fg");

    CREATE_TAG("org_tag",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@syn_variable",
               "background", "@bg_code_inline");

    CREATE_TAG("org_deadline_label",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@error");

    CREATE_TAG("org_scheduled_label",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@syn_type");

    CREATE_TAG("org_closed_label",
               "foreground", "@fg_faint");

    CREATE_TAG("org_timestamp",
               "foreground", "@syn_escape",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("org_inactive_ts",
               "foreground", "@fg_faint");

    CREATE_TAG("org_drawer",
               "family", "@font_code",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_faint",
               "background", "@bg_block");

    CREATE_TAG("org_src_block",
               "family", "@font_code",
               "background", "@bg_block",
               "foreground", "@fg_text",
               "left-margin", 24,
               "right-margin", 24);

    CREATE_TAG("org_src_header",
               "family", "@font_code",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "background", "@bg_block",
               "left-margin", 24);

    CREATE_TAG("org_quote",
               "style", PANGO_STYLE_ITALIC,
               "foreground", "@fg_muted",
               "left-margin", 24);

    CREATE_TAG("org_table",
               "family", "@font_code",
               "foreground", "@fg_text");

    CREATE_TAG("org_table_hdr",
               "family", "@font_code",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@accent_fg");

    CREATE_TAG("org_checkbox_checked",
               "foreground", "@success",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_checkbox_unchecked",
               "foreground", "@fg_text",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_checkbox_partial",
               "foreground", "@warning",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_cookie",
               "foreground", "@accent_fg",
               "scale", FONT_SCALE_SMALL,
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_fold_cookie",
               "foreground", "@fg_subtle",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("org_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("org_underline",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("org_code",
               "family", "@font_code",
               "foreground", "@syn_string",
               "background", "@bg_code_inline");

    CREATE_TAG("org_strike",
               "strikethrough", TRUE,
               "foreground", "@fg_subtle");

    CREATE_TAG("org_link",
               "foreground", "@link",
               "underline", PANGO_UNDERLINE_SINGLE);

    #undef CREATE_TAG
}

/* ------------------------------------------------------------------------- */
/* Parsing Helpers                                                           */
/* ------------------------------------------------------------------------- */

gint orgmode_heading_level(const gchar* line) {
    return org_heading_level(line);
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
                                 gboolean dim_done, OrgLineInfo* cur_line_info,
                                 GtkTextView* view) {
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
                        /* Natural size, shrunk to the view width */
                        GdkPixbuf* pix = pixbuf_from_file(img_path, FIT_IMAGE_MAX_WIDTH);
                        if (pix != NULL) {
                            GdkTexture* tex = texture_new_for_pixbuf(pix);
                            GdkPaintable* fit = fit_image_new(tex, view);
                            gtk_text_buffer_insert_paintable(buffer, iter, fit);
                            g_object_unref(fit);
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
                              GtkTextView* view,
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
        gchar* banner = g_strdup_printf("🦄 [ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)  •  Org-mode\n\n",
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
        gint hlevel = orgmode_heading_level(raw_line);

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
            } else if (g_str_has_prefix(rest, "RUNNING ") || g_strcmp0(rest, "RUNNING") == 0) {
                todo_kw = "RUNNING";
                todo_tag = "org_running";
                rest += 7;
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
            render_inline_markup(buffer, &iter, title_part, doc_dir, dim_done, linfo, view);
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

            render_inline_markup(buffer, &iter, lead, doc_dir, linfo->checkbox_done, linfo, view);
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
        render_inline_markup(buffer, &iter, raw_line, doc_dir, FALSE, linfo, view);
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
        app_emit(app, APP_EVENT_PREVIEW_STALE);
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
        app_emit(app, APP_EVENT_PREVIEW_STALE);
    }
}

/* Run an org_doc command on line file_line of the Org file on disk and save it */
typedef gboolean (*FileLineOp)(GPtrArray* lines, OrgPos* pos, OrgTodoConfig* cfg, GDateTime* now);

static gboolean edit_file_line(const gchar* file_path, gint file_line, FileLineOp op) {
    gchar* contents = NULL;
    if (file_path == NULL || !g_file_get_contents(file_path, &contents, NULL, NULL)) return FALSE;
    GPtrArray* lines = org_lines_new(contents);
    g_free(contents);
    gboolean ok = FALSE;
    if (file_line >= 0 && (guint)file_line < lines->len) {
        OrgTodoConfig* cfg = org_todo_config_new(lines);
        GDateTime* now = g_date_time_new_now_local();
        OrgPos pos = {file_line, 0};
        ok = op(lines, &pos, cfg, now);
        if (ok) {
            gchar* text = org_lines_join(lines);
            ok = g_file_set_contents(file_path, text, -1, NULL);
            g_free(text);
        }
        g_date_time_unref(now);
        org_todo_config_free(cfg);
    }
    g_ptr_array_unref(lines);
    return ok;
}

static gboolean op_cycle_todo(GPtrArray* lines, OrgPos* pos, OrgTodoConfig* cfg, GDateTime* now) {
    if (org_heading_level(org_line(lines, pos->line)) == 0) return FALSE;
    return org_todo_cycle(lines, pos, pos->line, cfg, 1, now, NULL);
}

static gboolean op_toggle_checkbox(GPtrArray* lines, OrgPos* pos, OrgTodoConfig* cfg, GDateTime* now) {
    (void)now;
    if (!org_toggle_checkbox(lines, pos, NULL)) return FALSE;
    org_update_statistics(lines, cfg);
    return TRUE;
}

gboolean orgmode_cycle_todo_at_line(const gchar* file_path, gint file_line) {
    return edit_file_line(file_path, file_line, op_cycle_todo);
}

gboolean orgmode_toggle_checkbox_at_line(const gchar* file_path, gint file_line) {
    return edit_file_line(file_path, file_line, op_toggle_checkbox);
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
            GtkUriLauncher* launcher = gtk_uri_launcher_new(info->link_target);
            gtk_uri_launcher_launch(launcher, app->window, NULL, NULL, NULL);
            g_object_unref(launcher);
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
                       gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_running")) ||
                       gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(app->buffer), "org_waiting"));

    if (on_todo || info->heading_level > 0) {
        if (on_todo) {
            /* Cycle TODO state */
            orgmode_cycle_todo_at_line(app->viewed_path, info->file_line);
            app_emit(app, APP_EVENT_PREVIEW_STALE);
            return TRUE;
        } else {
            /* Toggle heading folding */
            orgmode_toggle_heading_fold(app, info->file_line);
            return TRUE;
        }
    }

    /* 2. If line has checkbox, toggle it */
    if (info->is_checkbox) {
        orgmode_toggle_checkbox_at_line(app->viewed_path, info->file_line);
        app_emit(app, APP_EVENT_PREVIEW_STALE);
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
            orgmode_cycle_todo_at_line(app->viewed_path, info->file_line);
            app_emit(app, APP_EVENT_PREVIEW_STALE);
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
            orgmode_toggle_checkbox_at_line(app->viewed_path, info->file_line);
            app_emit(app, APP_EVENT_PREVIEW_STALE);
            return TRUE;
        }
    }

    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Editor Orgmode Heading Folding & Visibility                               */
/* ------------------------------------------------------------------------- */

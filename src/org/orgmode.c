#include "org/orgmode.h"
#include "preview/preview_state.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"
#include "org/org_doc.h"
#include "markup/md_show.h"
#include "core/app_events.h"
#include "editor/editor.h"
#include "org/org_fold.h"

#include <string.h>

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

void orgmode_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* out, GPtrArray* line_maps) {
    ensure_orgmode_tags(buffer);
    md_out_show(buffer, view, out);
    /* Line of each heading, item and text line, for clicks and keys */
    g_object_set_data_full(G_OBJECT(buffer), "org_line_maps", g_ptr_array_ref(line_maps),
                           (GDestroyNotify)g_ptr_array_unref);
}

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
    GPtrArray* line_maps = NULL;
    MdOut* out = org_render_parse(org_text, doc_path, is_preview, file_name, total_size, folded_lines,
                                  global_fold, &line_maps);
    orgmode_show(buffer, view, out, line_maps);
    g_ptr_array_unref(line_maps);
    md_out_free(out);
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
    if (preview_state_name(app) == NULL || !is_org_file(preview_state_name(app))) return FALSE;
    if (preview_state_raw(app)) return FALSE;

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
            orgmode_cycle_todo_at_line(preview_state_path(app), info->file_line);
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
        orgmode_toggle_checkbox_at_line(preview_state_path(app), info->file_line);
        app_emit(app, APP_EVENT_PREVIEW_STALE);
        return TRUE;
    }

    return FALSE;
}

gboolean orgmode_handle_viewer_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || preview_state_name(app) == NULL || !is_org_file(preview_state_name(app))) return FALSE;
    if (preview_state_raw(app)) return FALSE;

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
            orgmode_cycle_todo_at_line(preview_state_path(app), info->file_line);
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
            orgmode_toggle_checkbox_at_line(preview_state_path(app), info->file_line);
            app_emit(app, APP_EVENT_PREVIEW_STALE);
            return TRUE;
        }
    }

    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Editor Orgmode Heading Folding & Visibility                               */
/* ------------------------------------------------------------------------- */

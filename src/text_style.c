/* Text view styling shared by the viewer, editor and preview renderers:
 * Dark+ GtkSourceView scheme, panel background, monospace and syntax language. */
#include "text_style.h"
#include "file_types.h"
#include "app_paths.h"
#include "palette.h"
#include "theme.h"

#ifdef HAVE_GSV
/* GtkSourceView style scheme; @role tokens are palette colours
 * (palette_expand()). */
static const gchar* VSCODE_SCHEME_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- Inherits from a built-in dark scheme so languages whose style ids\n"
    "     are not overridden here keep Dark+ compatible colors. -->\n"
    "<style-scheme id=\"vscode-dark\" _name=\"VS Code Dark\" version=\"1.0\"\n"
    "               parent-scheme=\"Adwaita-dark\">\n"
    "  <_description>VS Code Dark+ palette for the file explorer viewer</_description>\n"
    "  <style name=\"text\" background=\"@bg_view\" foreground=\"@fg_text\"/>\n"
    "  <style name=\"def\" foreground=\"@fg_text\"/>\n"
    "  <style name=\"selection\" background=\"@editor_selection\"/>\n"
    "  <style name=\"current-line\" background=\"@editor_current_line\"/>\n"
    "  <style name=\"current-line-search\" background=\"@editor_selection\"/>\n"
    "  <style name=\"bracket-match\" background=\"@editor_bracket\"/>\n"
    "  <style name=\"comment\" foreground=\"@syn_comment\"/>\n"
    "  <style name=\"def:comment\" foreground=\"@syn_comment\"/>\n"
    "  <style name=\"shebang\" foreground=\"@syn_comment\"/>\n"
    "  <style name=\"def:shebang\" foreground=\"@syn_comment\"/>\n"
    "  <style name=\"constant\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"def:constant\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"boolean\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"def:boolean\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"special-constant\" foreground=\"@syn_constant\"/>\n"
    "  <style name=\"def:special-constant\" foreground=\"@syn_constant\"/>\n"
    "  <style name=\"string\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"def:string\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"character\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"def:character\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"escape\" foreground=\"@syn_escape\"/>\n"
    "  <style name=\"def:escape\" foreground=\"@syn_escape\"/>\n"
    "  <style name=\"special-char\" foreground=\"@syn_escape\"/>\n"
    "  <style name=\"def:special-char\" foreground=\"@syn_escape\"/>\n"
    "  <style name=\"number\" foreground=\"@syn_number\"/>\n"
    "  <style name=\"def:decimal\" foreground=\"@syn_number\"/>\n"
    "  <style name=\"def:floating-point\" foreground=\"@syn_number\"/>\n"
    "  <style name=\"keyword\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"def:keyword\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"keyword-modifier\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"preprocessor\" foreground=\"@syn_control\"/>\n"
    "  <style name=\"def:preprocessor\" foreground=\"@syn_control\"/>\n"
    "  <style name=\"type\" foreground=\"@syn_type\"/>\n"
    "  <style name=\"def:type\" foreground=\"@syn_type\"/>\n"
    "  <style name=\"function\" foreground=\"@syn_function\"/>\n"
    "  <style name=\"def:function\" foreground=\"@syn_function\"/>\n"
    "  <style name=\"builtin-function\" foreground=\"@syn_function\"/>\n"
    "  <style name=\"variable\" foreground=\"@syn_variable\"/>\n"
    "  <style name=\"def:variable\" foreground=\"@syn_variable\"/>\n"
    "  <style name=\"operator\" foreground=\"@fg_text\"/>\n"
    "  <style name=\"def:operator\" foreground=\"@fg_text\"/>\n"
    "  <style name=\"error\" foreground=\"@syn_error\"/>\n"
    "  <style name=\"def:error\" foreground=\"@syn_error\"/>\n"
    "  <style name=\"included-file\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"include-statement\" foreground=\"@syn_control\"/>\n"
    "  <style name=\"net-address-in-comment\" foreground=\"@syn_comment\"/>\n"
    "  <!-- Org-mode (org.lang) -->\n"
    "  <style name=\"org:header1\" foreground=\"@syn_constant\" bold=\"true\"/>\n"
    "  <style name=\"org:header2\" foreground=\"@syn_function\" bold=\"true\"/>\n"
    "  <style name=\"org:header3\" foreground=\"@syn_type\" bold=\"true\"/>\n"
    "  <style name=\"org:header4\" foreground=\"@syn_control\" bold=\"true\"/>\n"
    "  <style name=\"org:todo\" foreground=\"@syn_error\" bold=\"true\"/>\n"
    "  <style name=\"org:running\" foreground=\"@highlight\" bold=\"true\"/>\n"
    "  <style name=\"org:done\" foreground=\"@syn_comment\" bold=\"true\"/>\n"
    "  <style name=\"org:priority\" foreground=\"@syn_escape\" bold=\"true\"/>\n"
    "  <style name=\"org:tag\" foreground=\"@syn_variable\" italic=\"true\"/>\n"
    "  <style name=\"org:cookie\" foreground=\"@syn_number\"/>\n"
    "  <style name=\"org:comment\" foreground=\"@syn_comment\"/>\n"
    "  <style name=\"org:keyword\" foreground=\"@syn_control\"/>\n"
    "  <style name=\"org:drawer\" foreground=\"@fg_subtle\"/>\n"
    "  <style name=\"org:planning\" foreground=\"@syn_keyword\" bold=\"true\"/>\n"
    "  <style name=\"org:timestamp\" foreground=\"@syn_escape\"/>\n"
    "  <style name=\"org:link\" foreground=\"@syn_constant\" underline=\"single\"/>\n"
    "  <style name=\"org:checkbox\" foreground=\"@syn_keyword\" bold=\"true\"/>\n"
    "  <style name=\"org:checkbox-done\" foreground=\"@syn_comment\" bold=\"true\"/>\n"
    "  <style name=\"org:bullet\" foreground=\"@syn_keyword\"/>\n"
    "  <style name=\"org:table\" foreground=\"@syn_number\"/>\n"
    "  <style name=\"org:bold\" bold=\"true\"/>\n"
    "  <style name=\"org:italic\" italic=\"true\"/>\n"
    "  <style name=\"org:code\" foreground=\"@syn_string\"/>\n"
    "  <style name=\"org:strike\" foreground=\"@fg_subtle\" strikethrough=\"true\"/>\n"
    "  <style name=\"org:underline\" underline=\"single\"/>\n"
    "  <style name=\"org:block\" foreground=\"@fg_subtle\"/>\n"
    "  <style name=\"org:block-content\" foreground=\"@syn_string\"/>\n"
    "</style-scheme>\n";

void apply_viewer_background(GtkWidget* view) {
    gtk_widget_set_name(view, "viewer");
    /* The rule targets #viewer only: one provider for the display is enough */
    static GdkDisplay* styled_display = NULL;
    GdkDisplay* display = gtk_widget_get_display(view);
    if (display != styled_display) {
        theme_add_css("#viewer text { background-color: @bg_view; }");
        styled_display = display;
    }
}

void apply_vscode_scheme(GtkTextBuffer* buffer, GtkWidget* view) {
    GtkSourceStyleSchemeManager* mgr =
        gtk_source_style_scheme_manager_get_default();
    gchar* cache_dir = app_cache_dir();
    gchar* dir = g_build_filename(cache_dir, "scheme", NULL);
    g_free(cache_dir);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        return;
    }
    gchar* path = g_build_filename(dir, "vscode-dark.xml", NULL);
    gchar* xml = palette_expand(VSCODE_SCHEME_XML);
    g_file_set_contents(path, xml, -1, NULL);
    g_free(xml);
    g_free(path);
    gtk_source_style_scheme_manager_append_search_path(mgr, dir);
    g_free(dir);
    gtk_source_style_scheme_manager_force_rescan(mgr);

    GtkSourceStyleScheme* scheme =
        gtk_source_style_scheme_manager_get_scheme(mgr, "vscode-dark");
    if (scheme != NULL) {
        gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buffer), scheme);
        apply_viewer_background(view);
    } else {
        g_printerr("[ VS Code scheme unavailable: keeping default syntax colors ]\n");
    }
}
#endif

/* Set monospace / fixed-width font on a text view */
void set_view_monospace(GtkWidget* view, gboolean monospace) {
    if (view == NULL) {
        return;
    }
    if (monospace) {
        gtk_widget_add_css_class(view, "code-font");
    } else {
        gtk_widget_remove_css_class(view, "code-font");
    }
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), monospace);
}

/* Return TRUE if the name of a text file indicates source code */
gboolean is_code_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    if (ext[0] != '\0') {
#define EXT_MATCH(x) (g_ascii_strcasecmp(ext, (x)) == 0)
        if (EXT_MATCH("c") || EXT_MATCH("h") || EXT_MATCH("cc") ||
            EXT_MATCH("cpp") || EXT_MATCH("cxx") || EXT_MATCH("hpp") ||
            EXT_MATCH("hh") || EXT_MATCH("hxx") || EXT_MATCH("py") ||
            EXT_MATCH("pyw") || EXT_MATCH("pyx") || EXT_MATCH("rs") ||
            EXT_MATCH("go") || EXT_MATCH("java") || EXT_MATCH("kt") ||
            EXT_MATCH("kts") || EXT_MATCH("scala") || EXT_MATCH("cs") ||
            EXT_MATCH("fs") || EXT_MATCH("vb") || EXT_MATCH("swift") ||
            EXT_MATCH("m") || EXT_MATCH("mm") || EXT_MATCH("js") ||
            EXT_MATCH("mjs") || EXT_MATCH("cjs") || EXT_MATCH("ts") ||
            EXT_MATCH("tsx") || EXT_MATCH("jsx") || EXT_MATCH("vue") ||
            EXT_MATCH("svelte") || EXT_MATCH("css") || EXT_MATCH("scss") ||
            EXT_MATCH("sass") || EXT_MATCH("less") || EXT_MATCH("html") ||
            EXT_MATCH("htm") || EXT_MATCH("xml") || EXT_MATCH("svg") ||
            EXT_MATCH("json") || EXT_MATCH("yaml") || EXT_MATCH("yml") ||
            EXT_MATCH("toml") || EXT_MATCH("ini") || EXT_MATCH("cfg") ||
            EXT_MATCH("conf") || EXT_MATCH("sh") || EXT_MATCH("bash") ||
            EXT_MATCH("zsh") || EXT_MATCH("fish") || EXT_MATCH("bat") ||
            EXT_MATCH("cmd") || EXT_MATCH("ps1") || EXT_MATCH("rb") ||
            EXT_MATCH("pl") || EXT_MATCH("pm") || EXT_MATCH("php") ||
            EXT_MATCH("lua") || EXT_MATCH("r") || EXT_MATCH("tcl") ||
            EXT_MATCH("awk") || EXT_MATCH("sed") || EXT_MATCH("sql") ||
            EXT_MATCH("s") || EXT_MATCH("asm") || EXT_MATCH("diff") ||
            EXT_MATCH("patch") || EXT_MATCH("cmake") || EXT_MATCH("mk") ||
            EXT_MATCH("log")) {
            return TRUE;
        }
#undef EXT_MATCH
    }

    if (g_ascii_strcasecmp(name, "Makefile") == 0 ||
        g_ascii_strcasecmp(name, "makefile") == 0 ||
        g_ascii_strcasecmp(name, "CMakeLists.txt") == 0 ||
        g_ascii_strcasecmp(name, "meson.build") == 0 ||
        g_ascii_strcasecmp(name, "Dockerfile") == 0 ||
        g_ascii_strcasecmp(name, "dockerfile") == 0) {
        return TRUE;
    }

#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
    GtkSourceLanguage* lang = gtk_source_language_manager_guess_language(mgr, name, ctype);
    g_free(ctype);
    if (lang != NULL) {
        return TRUE;
    }
#endif

    return FALSE;
}

gboolean apply_viewer_language(AppData* app, const gchar* name, const gchar* content_type) {
#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = NULL;
    if (name != NULL) {
        gchar* ctype = (content_type != NULL) ? g_strdup(content_type)
                                              : g_content_type_guess(name, NULL, 0, NULL);
        lang = gtk_source_language_manager_guess_language(mgr, name, ctype);
        g_free(ctype);
    }
    if (app->buffer != NULL && GTK_SOURCE_IS_BUFFER(app->buffer)) {
        GtkSourceBuffer* sbuf = GTK_SOURCE_BUFFER(app->buffer);
        gtk_source_buffer_set_language(sbuf, lang);
        gtk_source_buffer_set_highlight_syntax(sbuf, lang != NULL);
    }
    return lang != NULL;
#else
    (void)app;
    (void)name;
    (void)content_type;
    return FALSE;
#endif
}

/* Padding of plain text, directory listings and summaries in the preview */
void set_view_text_padding(GtkWidget* view) {
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE);
}

void apply_buffer_language(GtkTextBuffer* buffer, const gchar* name, gboolean org) {
#ifdef HAVE_GSV
    if (buffer == NULL || !GTK_SOURCE_IS_BUFFER(buffer)) return;
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = NULL;
    if (org) {
        lang = gtk_source_language_manager_get_language(mgr, "org");
    } else if (name != NULL) {
        gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
        lang = gtk_source_language_manager_guess_language(mgr, name, ctype);
        g_free(ctype);
        if (lang != NULL && g_strcmp0(gtk_source_language_get_id(lang), "org") == 0) {
            lang = NULL;
        }
    }
    GtkSourceBuffer* sb = GTK_SOURCE_BUFFER(buffer);
    if (gtk_source_buffer_get_language(sb) != lang) {
        gtk_source_buffer_set_language(sb, lang);
    }
    gtk_source_buffer_set_highlight_syntax(sb, lang != NULL);
#else
    (void)buffer;
    (void)name;
    (void)org;
#endif
}

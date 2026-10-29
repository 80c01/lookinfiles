/* Text view styling shared by the viewer, editor and preview renderers:
 * Dark+ GtkSourceView scheme, panel background, monospace and syntax language. */
#include "text_style.h"
#include "file_types.h"

#ifdef HAVE_GSV
/* VS Code Dark+ palette, as a GtkSourceView style scheme. */
static const gchar* VSCODE_SCHEME_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- Inherits from a built-in dark scheme so languages whose style ids\n"
    "     are not overridden here keep Dark+ compatible colors. -->\n"
    "<style-scheme id=\"vscode-dark\" _name=\"VS Code Dark\" version=\"1.0\"\n"
    "               parent-scheme=\"Adwaita-dark\">\n"
    "  <_description>VS Code Dark+ palette for the file explorer viewer</_description>\n"
    "  <style name=\"text\" background=\"#121314\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"def\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"selection\" background=\"#264f78\"/>\n"
    "  <style name=\"current-line\" background=\"#1e2022\"/>\n"
    "  <style name=\"current-line-search\" background=\"#314365\"/>\n"
    "  <style name=\"bracket-match\" background=\"#515553\"/>\n"
    "  <style name=\"comment\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"def:comment\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"shebang\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"def:shebang\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"constant\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:constant\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"boolean\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:boolean\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"special-constant\" foreground=\"#4fc1ff\"/>\n"
    "  <style name=\"def:special-constant\" foreground=\"#4fc1ff\"/>\n"
    "  <style name=\"string\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"def:string\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"character\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"def:character\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"escape\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"def:escape\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"special-char\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"def:special-char\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"number\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"def:decimal\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"def:floating-point\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"keyword\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:keyword\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"keyword-modifier\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"preprocessor\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"def:preprocessor\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"type\" foreground=\"#4ec9b0\"/>\n"
    "  <style name=\"def:type\" foreground=\"#4ec9b0\"/>\n"
    "  <style name=\"function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"def:function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"builtin-function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"variable\" foreground=\"#9cdcfe\"/>\n"
    "  <style name=\"def:variable\" foreground=\"#9cdcfe\"/>\n"
    "  <style name=\"operator\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"def:operator\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"error\" foreground=\"#f44747\"/>\n"
    "  <style name=\"def:error\" foreground=\"#f44747\"/>\n"
    "  <style name=\"included-file\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"include-statement\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"net-address-in-comment\" foreground=\"#6a9955\"/>\n"
    "</style-scheme>\n";

void apply_viewer_background(GtkWidget* view) {
    gtk_widget_set_name(view, "viewer");
    /* The rule targets #viewer only: one provider for the display is enough */
    static GdkDisplay* styled_display = NULL;
    GdkDisplay* display = gtk_widget_get_display(view);
    if (display != styled_display) {
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(
            css, "#viewer text { background-color: #121314; }");
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        styled_display = display;
    }
}

void apply_vscode_scheme(GtkTextBuffer* buffer, GtkWidget* view) {
    GtkSourceStyleSchemeManager* mgr =
        gtk_source_style_scheme_manager_get_default();
    gchar* dir = g_build_filename(g_get_user_cache_dir(), "file-explorer",
                                  "scheme", NULL);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        return;
    }
    gchar* path = g_build_filename(dir, "vscode-dark.xml", NULL);
    g_file_set_contents(path, VSCODE_SCHEME_XML, -1, NULL);
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

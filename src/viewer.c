#include "viewer.h"
#include "file_list.h"
#include "markdown.h"
#include "html.h"
#include "pdf.h"
#include "search.h"
#include "ui.h"

#ifdef HAVE_GSV
/* VS Code Dark+ palette, as a GtkSourceView style scheme. */
static const gchar* VSCODE_SCHEME_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- Inherits from a built-in dark scheme so languages whose style ids\n"
    "     are not overridden here keep Dark+ compatible colors. -->\n"
    "<style-scheme id=\"vscode-dark\" _name=\"VS Code Dark\" version=\"1.0\"\n"
    "               parent-scheme=\"oblivion\">\n"
    "  <_description>VS Code Dark+ palette for the file explorer viewer</_description>\n"
    "  <style name=\"def\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"selection\" background=\"#264f78\"/>\n"
    "  <style name=\"current-line\" background=\"#2a2d2e\"/>\n"
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
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css, "#viewer text { background-color: #1e1e1e; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(view),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
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
    GtkStyleContext* ctx = gtk_widget_get_style_context(view);
    if (monospace) {
        gtk_style_context_add_class(ctx, "code-font");
    } else {
        gtk_style_context_remove_class(ctx, "code-font");
    }
#if GTK_CHECK_VERSION(3, 16, 0)
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), monospace);
#endif
}

/* Return TRUE if file extension or content indicates source code */
gboolean is_code_file(const gchar* name) {
    if (name == NULL || is_image_file(name) || is_pdf_file(name)) {
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

void apply_viewer_language(AppData* app, const gchar* name) {
#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = NULL;
    if (name != NULL) {
        gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
        lang = gtk_source_language_manager_guess_language(mgr, name, ctype);
        g_free(ctype);
    }
    if (app->buffer != NULL && GTK_SOURCE_IS_BUFFER(app->buffer)) {
        GtkSourceBuffer* sbuf = GTK_SOURCE_BUFFER(app->buffer);
        gtk_source_buffer_set_language(sbuf, lang);
        gtk_source_buffer_set_highlight_syntax(sbuf, lang != NULL);
    }
#else
    (void)app;
    (void)name;
#endif
}

void viewer_scroll_lines(AppData* app, gint lines) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));

    GtkTextIter first;
    gtk_text_buffer_get_start_iter(app->buffer, &first);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(view, &first, &rect);
    gdouble line_h = (rect.height > 0.0) ? rect.height : 14.0;

    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + lines * line_h);
}

void viewer_scroll_page(AppData* app, gint direction) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gtk_adjustment_set_value(
        adj, gtk_adjustment_get_value(adj) + direction * gtk_adjustment_get_page_size(adj));
}

void viewer_goto(AppData* app, gboolean top) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkTextIter it;
    if (top) {
        gtk_text_buffer_get_start_iter(app->buffer, &it);
    } else {
        gtk_text_buffer_get_end_iter(app->buffer, &it);
    }
    gtk_text_view_scroll_to_iter(view, &it, 0.0, FALSE, 0.0, top ? 0.0 : 1.0);
}

gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    guint key = event->keyval;

    switch (key) {
    case GDK_KEY_j:
        app->pending_g = FALSE;
        viewer_scroll_lines(app, +1);
        return TRUE;
    case GDK_KEY_k:
        app->pending_g = FALSE;
        viewer_scroll_lines(app, -1);
        return TRUE;
    case GDK_KEY_g:
        if (app->pending_g) {
            app->pending_g = FALSE;
            viewer_goto(app, TRUE);
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G:
        app->pending_g = FALSE;
        viewer_goto(app, FALSE);
        return TRUE;
    case GDK_KEY_slash:
        app->pending_g = FALSE;
        show_search(app, TRUE);
        return TRUE;
    case GDK_KEY_n:
        app->pending_g = FALSE;
        do_viewer_search(app, +1);
        return TRUE;
    case GDK_KEY_N:
        app->pending_g = FALSE;
        do_viewer_search(app, -1);
        return TRUE;
    case GDK_KEY_b:
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, -1);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f:
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, +1);
            return TRUE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && is_renderable_file(app->viewed_name)) {
            app->render_raw = !app->render_raw;
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        return FALSE;
    default:
        app->pending_g = FALSE;
        return FALSE;
    }
}

/* Render image preview with centered metadata header and scaled pixbuf */
void render_image_preview(AppData* app, const gchar* name, const gchar* full) {
    gtk_text_buffer_set_text(app->buffer, "", -1);

    gint orig_w = 0, orig_h = 0;
    GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(full, &orig_w, &orig_h);
    if (fmt == NULL || orig_w <= 0 || orig_h <= 0) {
        gchar* msg = g_strdup_printf("[ Cannot decode image: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }

    gchar* fmt_name = gdk_pixbuf_format_get_name(fmt);
    gchar* fmt_upper = g_ascii_strup(fmt_name, -1);
    g_free(fmt_name);

    struct stat st;
    gint64 fsize = (g_stat(full, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    /* Setup text tags for styling and centering */
    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(app->buffer);
    if (!gtk_text_tag_table_lookup(tags, "img_center")) {
        gtk_text_buffer_create_tag(app->buffer, "img_center",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_header")) {
        gtk_text_buffer_create_tag(app->buffer, "img_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", 1.15,
                                   "foreground", "#4fc1ff",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_sub")) {
        gtk_text_buffer_create_tag(app->buffer, "img_sub",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#888888",
                                   "scale", 0.9,
                                   NULL);
    }

    /* Scaling */
    gint target_w = orig_w;
    gint target_h = orig_h;
    gboolean is_fitted = FALSE;

    if (!app->render_raw) {
        /* Default mode: fit within available preview pane width and height */
        GtkAllocation alloc;
        gtk_widget_get_allocation(app->text_view, &alloc);
        gint max_w = (alloc.width > 100) ? (alloc.width - 40) : 700;
        gint max_h = (alloc.height > 140) ? (alloc.height - 120) : 500;
        if (orig_w > max_w || orig_h > max_h) {
            gdouble scale = MIN((gdouble)max_w / orig_w, (gdouble)max_h / orig_h);
            target_w = MAX(1, (gint)(orig_w * scale));
            target_h = MAX(1, (gint)(orig_h * scale));
            is_fitted = TRUE;
        }
    }

    GError* err = NULL;
    GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(full, target_w, target_h, TRUE, &err);
    if (pix == NULL) {
        gchar* msg = g_strdup_printf("[ Error loading image: %s (%s) ]",
                                     name, err ? err->message : "unknown error");
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        if (err != NULL) {
            g_error_free(err);
        }
        g_free(fmt_upper);
        g_free(size_str);
        return;
    }

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    /* Spacing */
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    /* Header title */
    gchar* title = g_strdup_printf("🖼️  %s\n", name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    /* Subtitle with metadata & mode hint */
    const gchar* mode_hint = is_fitted ? "  [fitted, press m for 1:1]"
                           : (app->render_raw && (orig_w != target_w || orig_h != target_h))
                                       ? "  [1:1 scale, press m to fit]"
                                       : "  [1:1 scale]";
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s\n\n",
                                  fmt_upper, orig_w, orig_h, size_str, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);
    g_free(fmt_upper);
    g_free(size_str);

    /* Insert image */
    gtk_text_buffer_insert_pixbuf(app->buffer, &iter, pix);
    g_object_unref(pix);

    /* Apply center tag to the image line */
    GtkTextIter start_img = iter;
    gtk_text_iter_backward_char(&start_img);
    gtk_text_buffer_apply_tag_by_name(app->buffer, "img_center", &start_img, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "img_center", NULL);
}

void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    gboolean is_md = is_markdown_file(name) && !app->render_raw;
    gboolean is_html = is_html_file(name) && !app->render_raw;
    gboolean is_img = is_image_file(name);
    gboolean is_pdf = is_pdf_file(name);

    if (is_pdf) {
        apply_viewer_language(app, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
#if GTK_CHECK_VERSION(3, 18, 0)
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
#endif
        render_pdf_preview(app, name, full);
        app->viewer_match = -1;
        return;
    }

    if (is_img) {
        apply_viewer_language(app, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
#if GTK_CHECK_VERSION(3, 18, 0)
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
#endif
        render_image_preview(app, name, full);
        app->viewer_match = -1;
        return;
    }

    gboolean is_rendered = is_md || is_html;

    if (is_rendered) {
        apply_viewer_language(app, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
#if GTK_CHECK_VERSION(3, 18, 0)
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
#endif
    } else {
        gboolean code = is_code_file(name) || app->render_raw;
        set_view_monospace(app->text_view, code);
        apply_viewer_language(app, name);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
#if GTK_CHECK_VERSION(3, 18, 0)
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
#endif
    }

    struct stat st;
    gint64 fsize = (g_stat(full, &st) == 0) ? (gint64)st.st_size : -1;
    gchar* contents = NULL;
    gsize len = 0;
    gboolean preview = FALSE;

    if (fsize < 0) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        app->viewer_match = -1;
        return;
    }

    if (fsize > PREVIEW_THRESHOLD) {
        preview = TRUE;
        FILE* fp = g_fopen(full, "rb");
        if (fp != NULL) {
            contents = g_malloc0(PREVIEW_BYTES + 1);
            len = fread(contents, 1, PREVIEW_BYTES, fp);
            contents[len] = '\0';
            fclose(fp);
        }
    } else {
        g_file_get_contents(full, &contents, &len, NULL);
    }

    if (contents != NULL) {
        gchar* valid_text = NULL;
        if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
            valid_text = g_utf8_make_valid(contents, (gssize)len);
        }
        const gchar* src = valid_text ? valid_text : contents;

        if (is_md) {
            ensure_markdown_tags(app->buffer);
            render_markdown_to_buffer(app->buffer, src, full, preview, name, fsize);
        } else if (is_html) {
            ensure_html_tags(app->buffer);
            render_html_to_buffer(app->buffer, src, full, preview, name, fsize);
        } else {
            GString* text = g_string_new(NULL);
            if (preview) {
                g_string_append_printf(text,
                        "[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                        "Showing first %u bytes. Large file, display truncated.\n\n",
                        name, fsize, (guint)len);
            }
            g_string_append(text, src);
            gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
            g_string_free(text, TRUE);
        }
        app->viewer_match = -1;
        g_free(valid_text);
        g_free(contents);
    } else {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        app->viewer_match = -1;
    }
}

static gboolean deferred_refresh(gpointer data) {
    AppData* app = (AppData*)data;
    app->refresh_source = 0;

    if (app->dir_dirty) {
        app->dir_dirty = FALSE;
        refresh_listing(app);
    }
    if (app->file_dirty) {
        app->file_dirty = FALSE;
        if (app->viewed_path != NULL) {
            load_file_content(app, app->viewed_name, app->viewed_path);
        }
    }
    return G_SOURCE_REMOVE;
}

void schedule_refresh(AppData* app) {
    if (app->refresh_source == 0) {
        app->refresh_source = g_timeout_add(150, deferred_refresh, app);
    }
}

static void on_dir_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                           GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)file; (void)other_file;
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED:
    case G_FILE_MONITOR_EVENT_RENAMED:
        ((AppData*)data)->dir_dirty = TRUE;
        schedule_refresh((AppData*)data);
        break;
    default:
        break;
    }
}

static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                            GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)file; (void)other_file;
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_DELETED:
        ((AppData*)data)->file_dirty = TRUE;
        schedule_refresh((AppData*)data);
        break;
    default:
        break;
    }
}

void monitor_directory(AppData* app) {
    if (app->dir_monitor != NULL) {
        g_object_unref(app->dir_monitor);
        app->dir_monitor = NULL;
    }
    GFile* f = g_file_new_for_path(app->base_dir);
    GError* err = NULL;
    app->dir_monitor = g_file_monitor_directory(f, G_FILE_MONITOR_NONE, NULL, &err);
    if (err != NULL) {
        g_error_free(err);
        app->dir_monitor = NULL;
    } else {
        g_signal_connect(app->dir_monitor, "changed", G_CALLBACK(on_dir_changed), app);
    }
    g_object_unref(f);
}

void monitor_file(AppData* app, const char* path) {
    if (app->file_monitor != NULL) {
        g_object_unref(app->file_monitor);
        app->file_monitor = NULL;
    }
    if (path != NULL) {
        GFile* f = g_file_new_for_path(path);
        GError* err = NULL;
        app->file_monitor = g_file_monitor(f, G_FILE_MONITOR_NONE, NULL, &err);
        if (err != NULL) {
            g_error_free(err);
            app->file_monitor = NULL;
        } else {
            g_signal_connect(app->file_monitor, "changed",
                             G_CALLBACK(on_file_changed), app);
        }
        g_object_unref(f);
    }
}

void on_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    GtkTreeModel* model = NULL;
    GtkTreeIter it;

    if (!gtk_tree_selection_get_selected(selection, &model, &it)) {
        return;
    }

    gchar* name = NULL;
    gchar* type = NULL;
    gchar* full = NULL;
    gtk_tree_model_get(model, &it,
                       COL_NAME, &name,
                       COL_TYPE, &type,
                       COL_PATH, &full, -1);

    if (name == NULL || type == NULL || g_strcmp0(type, "-") == 0 || full == NULL) {
        g_free(name);
        g_free(type);
        g_free(full);
        return;
    }

    if (g_strcmp0(type, "dir") == 0) {
        set_view_monospace(app->text_view, FALSE);
        GString* listing = g_string_new(NULL);
        g_string_append_printf(listing, "[ Directory ] %s\n\n", name);

        GDir* sub = g_dir_open(full, 0, NULL);
        if (sub != NULL) {
            GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
            const gchar* sub_name;
            while ((sub_name = g_dir_read_name(sub)) != NULL) {
                if (!app->show_hidden && sub_name[0] == '.') {
                    continue;
                }
                g_ptr_array_add(names, g_strdup(sub_name));
            }
            g_dir_close(sub);
            g_ptr_array_sort(names, (GCompareFunc)g_strcmp0);

            for (guint i = 0; i < names->len; i++) {
                const gchar* sub_entry = g_ptr_array_index(names, i);
                gchar* sub_full = g_build_filename(full, sub_entry, NULL);
                gboolean sub_is_dir = g_file_test(sub_full, G_FILE_TEST_IS_DIR);
                g_free(sub_full);
                g_string_append_printf(listing, "  %s%s\n",
                                       sub_entry, sub_is_dir ? "/" : "");
            }
            if (names->len == 0) {
                g_string_append(listing, "  (empty)\n");
            }
            g_ptr_array_unref(names);
        } else {
            g_string_append(listing, "  (cannot read directory)\n");
        }

        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
#if GTK_CHECK_VERSION(3, 18, 0)
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
#endif
        gtk_text_buffer_set_text(app->buffer, listing->str, (gint)listing->len);
        app->viewer_match = -1;
        apply_viewer_language(app, NULL);
        g_string_free(listing, TRUE);
    } else {
        if (g_strcmp0(name, app->viewed_name) != 0) {
            app->render_raw = FALSE;
        }
        load_file_content(app, name, full);
    }

    g_free(app->viewed_path);
    g_free(app->viewed_name);
    if (g_strcmp0(type, "dir") == 0) {
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);
    } else {
        app->viewed_path = g_strdup(full);
        app->viewed_name = g_strdup(name);
        monitor_file(app, full);
    }

    g_free(full);
    g_free(name);
    g_free(type);
}

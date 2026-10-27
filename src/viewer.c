#include "viewer.h"
#include "git_tab.h"
#include "file_list.h"
#include "markdown.h"
#include "html.h"
#include "pdf.h"
#include "video.h"
#include "audio.h"
#include "search.h"
#include "ui.h"

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
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        css, "#viewer text { background-color: #121314; }");
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
    if (monospace) {
        gtk_widget_add_css_class(view, "code-font");
    } else {
        gtk_widget_remove_css_class(view, "code-font");
    }
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), monospace);
}

/* Return TRUE if file extension or content indicates source code */
gboolean is_code_file(const gchar* name) {
    if (name == NULL || is_image_file(name) || is_pdf_file(name) || is_video_file(name) || is_audio_file(name)) {
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

gboolean on_viewer_key_press(GtkEventControllerKey* controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    guint key = keyval;

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
        if (state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, -1);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_d:
    case GDK_KEY_D:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            bookmark_toggle_current(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a:
    case GDK_KEY_A:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, +1);
            return TRUE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_p:
    case GDK_KEY_P:
    case GDK_KEY_space:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && !app->render_raw) {
            if (is_video_file(app->viewed_name)) {
                video_preview_toggle_play(app);
                return TRUE;
            } else if (is_audio_file(app->viewed_name)) {
                audio_preview_toggle_play(app);
                return TRUE;
            }
        }
        return FALSE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && is_renderable_file(app->viewed_name)) {
            app->render_raw = !app->render_raw;
            workspace_save_current(app);
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_question:
        if ((state & GDK_CONTROL_MASK) == 0) {
            app->pending_g = FALSE;
            toggle_help_panel(app);
            return TRUE;
        }
        app->pending_g = FALSE;
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        app->pending_g = FALSE;
        return FALSE;
    }
}

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;
    gint alloc_w;
    gint alloc_h;

    /* Results populated by worker thread */
    gchar* error_msg;
    gchar* fmt_upper;
    gchar* size_str;
    gint orig_w;
    gint orig_h;
    gint target_w;
    gint target_h;
    gboolean is_fitted;
    GdkTexture* texture;
} ImagePreviewTaskData;

static void image_preview_task_data_free(gpointer data) {
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->error_msg);
    g_free(td->fmt_upper);
    g_free(td->size_str);
    if (td->texture != NULL) {
        g_object_unref(td->texture);
    }
    g_free(td);
}

static void image_preview_thread_func(GTask* task, gpointer source_object,
                                      gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    gint orig_w = 0, orig_h = 0;
    GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(td->full, &orig_w, &orig_h);
    if (fmt == NULL || orig_w <= 0 || orig_h <= 0) {
        td->error_msg = g_strdup_printf("[ Cannot decode image: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    gchar* fmt_name = gdk_pixbuf_format_get_name(fmt);
    td->fmt_upper = g_ascii_strup(fmt_name, -1);
    g_free(fmt_name);

    struct stat st;
    gint64 fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : 0;
    td->size_str = g_format_size((guint64)fsize);
    td->orig_w = orig_w;
    td->orig_h = orig_h;
    td->target_w = orig_w;
    td->target_h = orig_h;
    td->is_fitted = FALSE;

    if (!td->render_raw) {
        gint max_w = (td->alloc_w > 100) ? (td->alloc_w - 40) : 700;
        gint max_h = (td->alloc_h > 140) ? (td->alloc_h - 120) : 500;
        if (orig_w > max_w || orig_h > max_h) {
            gdouble scale = MIN((gdouble)max_w / orig_w, (gdouble)max_h / orig_h);
            td->target_w = MAX(1, (gint)(orig_w * scale));
            td->target_h = MAX(1, (gint)(orig_h * scale));
            td->is_fitted = TRUE;
        }
    }

    GError* err = NULL;
    GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(
        td->full, td->target_w, td->target_h, TRUE, &err);
    if (pix == NULL) {
        td->error_msg = g_strdup_printf("[ Error loading image: %s (%s) ]",
                                        td->name, err ? err->message : "unknown error");
        if (err != NULL) {
            g_error_free(err);
        }
        g_task_return_boolean(task, TRUE);
        return;
    }

    if (g_task_return_error_if_cancelled(task)) {
        g_object_unref(pix);
        return;
    }

    td->texture = gdk_texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    g_task_return_boolean(task, TRUE);
}

static void on_image_preview_ready(GObject* source_object, GAsyncResult* res,
                                   gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;

    apply_viewer_language(app, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    gtk_text_buffer_set_text(app->buffer, "", -1);
    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

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

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    gchar* title = g_strdup_printf("🖼️  %s\n", td->name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    const gchar* mode_hint = td->is_fitted ? "  [fitted, press m for 1:1]"
                           : (td->render_raw && (td->orig_w != td->target_w || td->orig_h != td->target_h))
                                       ? "  [1:1 scale, press m to fit]"
                                       : "  [1:1 scale]";
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s\n\n",
                                  td->fmt_upper, td->orig_w, td->orig_h, td->size_str, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);

    if (td->texture != NULL) {
        gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(td->texture));

        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "img_center", &start_img, &iter);
    }

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "img_center", NULL);
}

/* Render image preview asynchronously on a worker thread */
void render_image_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    ImagePreviewTaskData* td = g_new0(ImagePreviewTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    td->alloc_w = gtk_widget_get_width(app->text_view);
    td->alloc_h = gtk_widget_get_height(app->text_view);

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_image_preview_ready, NULL);
    g_task_set_task_data(task, td, image_preview_task_data_free);
    g_task_run_in_thread(task, image_preview_thread_func);
    g_object_unref(task);
}

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean is_md;
    gboolean is_html;
    gboolean render_raw;

    /* Results populated by worker thread */
    gint64 fsize;
    gsize raw_len;
    gboolean preview;
    gchar* utf8_text;
    gchar* error_msg;
} TextPreviewTaskData;

static void text_preview_task_data_free(gpointer data) {
    TextPreviewTaskData* td = (TextPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->utf8_text);
    g_free(td->error_msg);
    g_free(td);
}

static void text_preview_thread_func(GTask* task, gpointer source_object,
                                     gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    TextPreviewTaskData* td = (TextPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    struct stat st;
    td->fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : -1;
    if (td->fsize < 0) {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    gchar* contents = NULL;
    gsize len = 0;
    if (td->fsize > PREVIEW_THRESHOLD) {
        td->preview = TRUE;
        FILE* fp = g_fopen(td->full, "rb");
        if (fp != NULL) {
            contents = g_malloc0(PREVIEW_BYTES + 1);
            len = fread(contents, 1, PREVIEW_BYTES, fp);
            contents[len] = '\0';
            fclose(fp);
        }
    } else {
        g_file_get_contents(td->full, &contents, &len, NULL);
    }

    if (g_task_return_error_if_cancelled(task)) {
        g_free(contents);
        return;
    }

    if (contents != NULL) {
        td->raw_len = len;
        if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
            td->utf8_text = g_utf8_make_valid(contents, (gssize)len);
            g_free(contents);
        } else {
            td->utf8_text = contents;
        }
    } else {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
    }

    g_task_return_boolean(task, TRUE);
}

static void on_text_preview_ready(GObject* source_object, GAsyncResult* res,
                                  gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    TextPreviewTaskData* td = (TextPreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    gboolean is_rendered = td->is_md || td->is_html;
    if (is_rendered) {
        apply_viewer_language(app, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
    } else {
        gboolean code = is_code_file(td->name) || td->render_raw;
        set_view_monospace(app->text_view, code);
        apply_viewer_language(app, td->name);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
    }

    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    if (td->utf8_text != NULL) {
        if (td->is_md) {
            ensure_markdown_tags(app->buffer);
            render_markdown_to_buffer(app->buffer, td->utf8_text, td->full,
                                      td->preview, td->name, td->fsize);
        } else if (td->is_html) {
            ensure_html_tags(app->buffer);
            render_html_to_buffer(app->buffer, td->utf8_text, td->full,
                                  td->preview, td->name, td->fsize);
        } else {
            GString* text = g_string_new(NULL);
            if (td->preview) {
                g_string_append_printf(text,
                        "[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                        "Showing first %u bytes. Large file, display truncated.\n\n",
                        td->name, td->fsize, (guint)td->raw_len);
            }
            g_string_append(text, td->utf8_text);
            gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
            g_string_free(text, TRUE);
        }
    }
}

void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    if (is_audio_file(name)) {
        video_preview_stop(app);
        render_audio_preview(app, name, full);
        return;
    }

    if (is_video_file(name)) {
        audio_preview_stop(app);
        render_video_preview(app, name, full);
        return;
    }

    audio_preview_stop(app);
    video_preview_stop(app);
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }

    if (is_pdf_file(name)) {
        render_pdf_preview(app, name, full);
        return;
    }

    if (is_image_file(name)) {
        render_image_preview(app, name, full);
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    TextPreviewTaskData* td = g_new0(TextPreviewTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    td->is_md = is_markdown_file(name) && !app->render_raw;
    td->is_html = is_html_file(name) && !app->render_raw;

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_text_preview_ready, NULL);
    g_task_set_task_data(task, td, text_preview_task_data_free);
    g_task_run_in_thread(task, text_preview_thread_func);
    g_object_unref(task);
}

static gboolean deferred_refresh(gpointer data) {
    AppData* app = (AppData*)data;
    app->refresh_source = 0;

    if (app->dir_dirty) {
        app->dir_dirty = FALSE;
        refresh_listing(app);
        git_tab_update(app, app->base_dir);
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
    (void)monitor; (void)other_file;
    AppData* app = (AppData*)data;

    /* Ignore events for .git directory, git internals, and temporary lock/swap files */
    if (file != NULL) {
        gchar* name = g_file_get_basename(file);
        if (name != NULL) {
            if (g_strcmp0(name, ".git") == 0 ||
                g_str_has_suffix(name, ".lock") ||
                g_str_has_suffix(name, ".tmp") ||
                g_str_has_suffix(name, "~")) {
                g_free(name);
                return;
            }
            g_free(name);
        }
        gchar* path = g_file_get_path(file);
        if (path != NULL) {
            if (strstr(path, "/.git/") != NULL || g_str_has_suffix(path, "/.git")) {
                g_free(path);
                return;
            }
            g_free(path);
        }
    }

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
        app->dir_dirty = TRUE;
        schedule_refresh(app);
        break;
    default:
        break;
    }
}

static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                            GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)other_file;
    AppData* app = (AppData*)data;

    if (file != NULL) {
        gchar* path = g_file_get_path(file);
        if (path != NULL) {
            if (strstr(path, "/.git/") != NULL || g_str_has_suffix(path, "/.git")) {
                g_free(path);
                return;
            }
            g_free(path);
        }
    }

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_DELETED:
        app->file_dirty = TRUE;
        app->dir_dirty = TRUE;
        schedule_refresh(app);
        break;
    default:
        break;
    }
}

static void add_single_dir_monitor(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    if (app->dir_monitors == NULL) {
        app->dir_monitors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    if (g_hash_table_contains(app->dir_monitors, path)) {
        return;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return;
    }
    GFile* f = g_file_new_for_path(path);
    GError* err = NULL;
    GFileMonitor* mon = g_file_monitor_directory(f, G_FILE_MONITOR_NONE, NULL, &err);
    if (err != NULL) {
        g_error_free(err);
    } else if (mon != NULL) {
        g_signal_connect(mon, "changed", G_CALLBACK(on_dir_changed), app);
        g_hash_table_insert(app->dir_monitors, g_strdup(path), mon);
    }
    g_object_unref(f);
}

void stop_directory_monitors(AppData* app) {
    if (app == NULL || app->dir_monitors == NULL) {
        return;
    }
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, app->dir_monitors);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        if (value != NULL) {
            g_file_monitor_cancel(G_FILE_MONITOR(value));
        }
    }
    g_hash_table_destroy(app->dir_monitors);
    app->dir_monitors = NULL;
}

void sync_directory_monitors(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->dir_monitors == NULL) {
        app->dir_monitors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }

    /* Collect all paths that should currently be monitored:
     * base_dir and all existing expanded subdirectories */
    GHashTable* needed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (app->base_dir != NULL && g_file_test(app->base_dir, G_FILE_TEST_IS_DIR)) {
        g_hash_table_add(needed, g_strdup(app->base_dir));
    }

    if (app->expanded_paths != NULL) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, app->expanded_paths);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            const gchar* p = (const gchar*)key;
            if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
                g_hash_table_add(needed, g_strdup(p));
            } else {
                /* Prune non-existent directory path from expanded_paths */
                g_hash_table_iter_remove(&iter);
            }
        }
    }

    /* Remove monitors that are no longer needed */
    GHashTableIter miter;
    gpointer mkey, mvalue;
    g_hash_table_iter_init(&miter, app->dir_monitors);
    while (g_hash_table_iter_next(&miter, &mkey, &mvalue)) {
        const gchar* mp = (const gchar*)mkey;
        if (!g_hash_table_contains(needed, mp)) {
            if (mvalue != NULL) {
                g_file_monitor_cancel(G_FILE_MONITOR(mvalue));
            }
            g_hash_table_iter_remove(&miter);
        }
    }

    /* Add monitors for any needed paths not yet monitored */
    GHashTableIter niter;
    gpointer nkey, nvalue;
    g_hash_table_iter_init(&niter, needed);
    while (g_hash_table_iter_next(&niter, &nkey, &nvalue)) {
        const gchar* np = (const gchar*)nkey;
        if (!g_hash_table_contains(app->dir_monitors, np)) {
            add_single_dir_monitor(app, np);
        }
    }

    g_hash_table_destroy(needed);
}

void monitor_directory(AppData* app) {
    sync_directory_monitors(app);
}

void monitor_file(AppData* app, const char* path) {
    if (app->file_monitor != NULL) {
        g_file_monitor_cancel(app->file_monitor);
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

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean show_hidden;
    GString* listing;
} DirPreviewTaskData;

static void dir_preview_task_data_free(gpointer data) {
    DirPreviewTaskData* td = (DirPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    if (td->listing != NULL) {
        g_string_free(td->listing, TRUE);
    }
    g_free(td);
}

static gint cmp_str_ptr(gconstpointer a, gconstpointer b) {
    const gchar* sa = *(const gchar* const*)a;
    const gchar* sb = *(const gchar* const*)b;
    return g_strcmp0(sa, sb);
}

static void dir_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    DirPreviewTaskData* td = (DirPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->listing = g_string_new(NULL);
    g_string_append_printf(td->listing, "[ Directory ] %s\n\n", td->name);

    GDir* sub = g_dir_open(td->full, 0, NULL);
    if (sub != NULL) {
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        const gchar* sub_name;
        while ((sub_name = g_dir_read_name(sub)) != NULL) {
            if (g_cancellable_is_cancelled(cancellable)) {
                g_dir_close(sub);
                g_ptr_array_unref(names);
                g_task_return_error_if_cancelled(task);
                return;
            }
            if (!td->show_hidden && sub_name[0] == '.') {
                continue;
            }
            g_ptr_array_add(names, g_strdup(sub_name));
        }
        g_dir_close(sub);
        g_ptr_array_sort(names, cmp_str_ptr);

        for (guint i = 0; i < names->len; i++) {
            if (g_cancellable_is_cancelled(cancellable)) {
                g_ptr_array_unref(names);
                g_task_return_error_if_cancelled(task);
                return;
            }
            const gchar* sub_entry = g_ptr_array_index(names, i);
            gchar* sub_full = g_build_filename(td->full, sub_entry, NULL);
            gboolean sub_is_dir = g_file_test(sub_full, G_FILE_TEST_IS_DIR);
            g_free(sub_full);
            g_string_append_printf(td->listing, "  %s%s\n",
                                   sub_entry, sub_is_dir ? "/" : "");
        }
        if (names->len == 0) {
            g_string_append(td->listing, "  (empty)\n");
        }
        g_ptr_array_unref(names);
    } else {
        g_string_append(td->listing, "  (cannot read directory)\n");
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void on_dir_preview_ready(GObject* source_object, GAsyncResult* res,
                                 gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    DirPreviewTaskData* td = (DirPreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || td->listing == NULL ||
        g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_buffer_set_text(app->buffer, td->listing->str, (gint)td->listing->len);
    app->viewer_match = -1;
    apply_viewer_language(app, NULL);
}

void on_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    AppData* app_sel = (AppData*)user_data;
    if (app_sel != NULL && !app_sel->editor_dedicated && app_sel->notebook != NULL) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app_sel->notebook), 0);
    }
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

    if (g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "bookmark") == 0) {
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        if (app->preview_cancellable != NULL) {
            g_cancellable_cancel(app->preview_cancellable);
            g_clear_object(&app->preview_cancellable);
        }
        app->preview_cancellable = g_cancellable_new();

        DirPreviewTaskData* td = g_new0(DirPreviewTaskData, 1);
        td->app = app;
        td->name = g_strdup(name);
        td->full = g_strdup(full);
        td->show_hidden = app->show_hidden;

        GTask* task = g_task_new(NULL, app->preview_cancellable, on_dir_preview_ready, NULL);
        g_task_set_task_data(task, td, dir_preview_task_data_free);
        g_task_run_in_thread(task, dir_preview_thread_func);
        g_object_unref(task);
    } else {
        if (g_strcmp0(full, app->viewed_path) == 0 && !app->file_dirty &&
            (is_video_file(name) || is_audio_file(name) || gtk_text_buffer_get_char_count(app->buffer) > 0)) {
            if (!app->switching_ws) {
                workspace_save_current(app);
            }
            g_free(full);
            g_free(name);
            g_free(type);
            return;
        }
        if (!app->switching_ws && g_strcmp0(name, app->viewed_name) != 0) {
            app->render_raw = FALSE;
        }
        app->file_dirty = FALSE;
        load_file_content(app, name, full);
    }

    g_free(app->viewed_path);
    g_free(app->viewed_name);
    if (g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "bookmark") == 0) {
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);
    } else {
        app->viewed_path = g_strdup(full);
        app->viewed_name = g_strdup(name);
        monitor_file(app, full);
    }

    if (!app->switching_ws) {
        workspace_save_current(app);
    }

    update_status_bar(app);

    g_free(full);
    g_free(name);
    g_free(type);
}

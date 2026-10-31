#include "viewer.h"
#include "image_preview.h"
#include "preview_summary.h"
#include "fs_watch.h"
#include "panels.h"
#include "app_events.h"
#include "text_style.h"
#include "help.h"
#include "file_types.h"
#include "editor.h"
#include "file_list.h"
#include "markdown.h"
#include "html.h"
#include "pdf.h"
#include "video.h"
#include "audio.h"
#include "search.h"
#include "bookmarks.h"
#include "bookmark_icons.h"
#include "path_kind.h"
#include "remote.h"
#include "orgmode.h"
#include "diff_gutter.h"
#include "dir_preview.h"
#include "preview_doc_view.h"
#include "preview_formats.h"
#include "text_chunks.h"
#include "text_encoding.h"
#include "preview_cache.h"
#include "tree_nav.h"
#include "focus_route.h"

/* Text shown from a Word document before it is cut (images not counted) */


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

/* Navigation keys of the global mode (nav_keys.h) in the viewer */
static void run_viewer_nav(AppData* app, NavAction action) {
    switch (action) {
    case NAV_DOWN: viewer_scroll_lines(app, +1); break;
    case NAV_UP: viewer_scroll_lines(app, -1); break;
    case NAV_FIRST: viewer_goto(app, TRUE); break;
    case NAV_LAST: viewer_goto(app, FALSE); break;
    case NAV_PAGE_DOWN: viewer_scroll_page(app, +1); break;
    case NAV_PAGE_UP: viewer_scroll_page(app, -1); break;
    case NAV_SEARCH: show_search(app, TRUE, FALSE); break;
    case NAV_SEARCH_BACKWARD: show_search(app, TRUE, TRUE); break;
    case NAV_SEARCH_NEXT: do_viewer_search(app, +1); break;
    case NAV_SEARCH_PREV: do_viewer_search(app, -1); break;
    case NAV_PANEL_NEXT: app_emit(app, APP_EVENT_FOCUS_NEXT_PANEL); break;
    case NAV_PANEL_PREV: app_emit(app, APP_EVENT_FOCUS_PREV_PANEL); break;
    case NAV_OPEN_DEDICATED:
    case NAV_DISPLAY_DEDICATED:
        /* The previewed file, in the dedicated editor panel */
        if (app->viewed_path != NULL && app->viewed_name != NULL) {
            gchar* name = g_strdup(app->viewed_name);
            gchar* full = g_strdup(app->viewed_path);
            editor_open_dedicated(app, name, full,
                                  action == NAV_DISPLAY_DEDICATED ? app->text_view : NULL);
            g_free(name);
            g_free(full);
        }
        break;
    case NAV_TAB_NEXT: focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), 1, TRUE); break;
    case NAV_TAB_PREV: focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), -1, TRUE); break;
    default: break; /* prefixes, cancel, row scrolling (explorer only) */
    }
}

gboolean on_viewer_key_press(GtkEventControllerKey* controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;

    /* The key after C-x / Ctrl+W belongs to the prefix, whatever it is */
    if (app->nav_prefix == NAV_PREFIX_CTRL_X || app->nav_prefix == NAV_PREFIX_CTRL_W) {
        NavPrefix next = NAV_PREFIX_NONE;
        NavAction action = nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, &next);
        app->nav_prefix = next;
        run_viewer_nav(app, action);
        return TRUE;
    }

    if (orgmode_handle_viewer_key(app, keyval, state)) {
        return TRUE;
    }

    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 && image_preview_zoom_key(app, keyval)) {
        return TRUE;
    }

    /* PgDn / PgUp step through PDF pages (Ctrl+PgDn / Ctrl+PgUp switch tabs) */
    if ((keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up) &&
        (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 &&
        pdf_preview_jump(app, keyval == GDK_KEY_Page_Down ? 1 : -1)) {
        return TRUE;
    }
    /* Paged previews (mailbox messages): PgDn / PgUp step through the pages */
    if ((keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up) && app->preview_pages > 0 && !app->render_raw &&
        (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0) {
        guint page = app->preview_page;
        if (keyval == GDK_KEY_Page_Down && page < app->preview_pages) page++;
        else if (keyval == GDK_KEY_Page_Up && page > 0) page--;
        if (page != app->preview_page) {
            app->preview_page = page;
            viewer_reload_preview(app);
        }
        return TRUE;
    }

    NavPrefix prefix = (NavPrefix)app->nav_prefix;
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(app_nav_style(app), prefix, keyval, state, &next);
    app->nav_prefix = next;
    switch (action) {
    case NAV_NONE:
        break;
    case NAV_FORWARD:
    case NAV_BACKWARD:
        return FALSE; /* the text view's own Left / Right */
    default:
        run_viewer_nav(app, action);
        return TRUE;
    }

    switch (keyval) {
    case GDK_KEY_d:
    case GDK_KEY_D:
        if (state & GDK_CONTROL_MASK) {
            bookmarks_add_from_context(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a:
    case GDK_KEY_A:
        if (state & GDK_CONTROL_MASK) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_u:
    case GDK_KEY_U:
        if (state & GDK_CONTROL_MASK) {
            toggle_notes_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f:
        if (state & GDK_CONTROL_MASK) {
            return FALSE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_p:
    case GDK_KEY_P:
    case GDK_KEY_space:
        if (app->viewed_name != NULL && !app->render_raw) {
            if (app->viewed_kind == FILE_KIND_VIDEO) {
                video_preview_toggle_play(app);
                return TRUE;
            } else if (app->viewed_kind == FILE_KIND_AUDIO) {
                audio_preview_toggle_play(app);
                return TRUE;
            }
        }
        return FALSE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        if (app->viewed_name != NULL && file_kind_is_renderable(app->viewed_kind)) {
            app->render_raw = !app->render_raw;
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_e:
    case GDK_KEY_E:
        if (app->viewed_path != NULL && app->viewed_name != NULL) {
            if (g_file_test(app->viewed_path, G_FILE_TEST_IS_DIR)) {
                return FALSE;
            }
            if (file_kind_is_media(app->viewed_kind)) {
                return FALSE;
            }
            editor_open_file(app, app->viewed_name, app->viewed_path);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_question:
        if ((state & GDK_CONTROL_MASK) == 0) {
                toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}


typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;
    guint page;            /* PreviewInput.page */

    /* Results populated by worker thread */
    FileKind kind;         /* detected from the first bytes */
    gchar* content_type;   /* GIO content type of text files (syntax highlighting) */
    gint64 fsize;
    gsize raw_len;
    gboolean preview;
    gchar* utf8_text;
    PreviewDoc* doc;       /* structured preview (preview_formats) or hex dump */
    const gchar* encoding; /* text encoding detected (static string) */
    gchar* error_msg;
    gint64 start_us;       /* selection time (LOOKIN_DEBUG_PREVIEW timings) */
    FileStamp stamp;       /* file version read (valid if has_stamp) */
    gboolean has_stamp;
    FileStamp cached_stamp; /* version of the cached preview already shown (if has_cached) */
    gboolean has_cached;
    gboolean unchanged;    /* the file is still the cached version: nothing was read */
} TextPreviewTaskData;

static void text_preview_task_data_free(gpointer data) {
    TextPreviewTaskData* td = (TextPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->content_type);
    g_free(td->utf8_text);
    preview_doc_free(td->doc);
    g_free(td->error_msg);
    g_free(td);
}

/* Worker thread: read the first bytes and detect the file kind. Media
 * viewers read their files themselves; text kinds read the text (up to
 * PREVIEW_BYTES for large files); kinds with a structured preview build it
 * (preview_formats), and binary content without one gets a hex dump. */
static void text_preview_thread_func(GTask* task, gpointer source_object,
                                     gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    TextPreviewTaskData* td = (TextPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->kind = file_kind_from_name(td->name);
    td->has_stamp = file_stamp_read(td->full, &td->stamp);
    td->fsize = td->has_stamp ? td->stamp.size : -1;
    if (td->has_stamp && td->has_cached && file_stamp_equal(&td->stamp, &td->cached_stamp)) {
        td->unchanged = TRUE;
        g_task_return_boolean(task, TRUE);
        return;
    }
    FILE* fp = (td->fsize >= 0) ? g_fopen(td->full, "rb") : NULL;
    if (fp == NULL) {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    td->preview = (td->fsize > PREVIEW_THRESHOLD);
    gsize cap = td->preview ? PREVIEW_BYTES : (gsize)td->fsize + 1;
    if (cap < FILE_SNIFF_BYTES) {
        cap = FILE_SNIFF_BYTES;
    }
    gchar* contents = g_malloc(cap + 1);
    gsize len = fread(contents, 1, FILE_SNIFF_BYTES, fp);
    td->kind = file_kind_detect(td->name, (const guchar*)contents, len);
    gboolean structured = !td->render_raw && preview_format_has_builder(td->kind);
    PreviewInput input = {
        .path = td->full, .name = td->name, .size = td->fsize, .kind = td->kind,
        .head = (const guchar*)contents, .head_len = MIN(len, FILE_SNIFF_BYTES), .page = td->page,
    };

    if (file_kind_is_media(td->kind)) {
        fclose(fp);
        g_free(contents);
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* Text kinds, and text content of other kinds (PEM certificates), are
     * read as text; the rest (gzipped manual pages too) as binary */
    if (!file_content_is_text((const guchar*)contents, len)) {
        /* Binary content: its structured preview, else a hex dump of the
         * first PREVIEW_BYTES only, whatever the file size */
        GError* err = NULL;
        if (structured) {
            td->doc = preview_format_build(&input, cancellable, &err);
        }
        if (td->doc == NULL) {
            if (cap < PREVIEW_BYTES) {
                contents = g_realloc(contents, PREVIEW_BYTES + 1);
            }
            while (len < PREVIEW_BYTES && !g_cancellable_is_cancelled(cancellable)) {
                gsize n = fread(contents + len, 1, PREVIEW_BYTES - len, fp);
                if (n == 0) {
                    break;
                }
                len += n;
            }
            td->doc = preview_format_hex_dump(td->name, td->kind, td->fsize, (const guchar*)contents,
                                              len, err != NULL ? err->message : NULL);
        }
        g_clear_error(&err);
        fclose(fp);
        g_free(contents);
        if (g_task_return_error_if_cancelled(task)) {
            return;
        }
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* Rest of the text: to the end (the file may have grown), capped for large files */
    while (!g_cancellable_is_cancelled(cancellable)) {
        if (len == cap) {
            if (td->preview) {
                break;
            }
            cap *= 2;
            contents = g_realloc(contents, cap + 1);
        }
        gsize n = fread(contents + len, 1, cap - len, fp);
        if (n == 0) {
            break;
        }
        len += n;
    }
    contents[len] = '\0';
    td->content_type = g_content_type_guess(td->name, (const guchar*)contents,
                                            MIN(len, FILE_SNIFF_BYTES), NULL);
    fclose(fp);

    if (g_task_return_error_if_cancelled(task)) {
        g_free(contents);
        return;
    }

    td->raw_len = len;
    td->encoding = "UTF-8";
    if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
        /* UTF-16, legacy 8-bit text, or invalid bytes */
        td->utf8_text = text_encoding_to_utf8(contents, len, &td->encoding);
        g_free(contents);
    } else if (len >= 3 && memcmp(contents, "\xEF\xBB\xBF", 3) == 0) {
        td->encoding = "UTF-8 BOM";
        td->utf8_text = contents;
    } else {
        td->utf8_text = contents;
    }

    if (structured) {
        /* Shown as plain text when the structured preview fails (invalid JSON...) */
        gsize text_len = strlen(td->utf8_text);
        input.head = (const guchar*)td->utf8_text;
        input.head_len = MIN(text_len, FILE_SNIFF_BYTES);
        input.text = td->utf8_text;
        input.text_len = text_len;
        input.text_truncated = td->preview;
        td->doc = preview_format_build(&input, cancellable, NULL);
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

/* Show a file in a media viewer (main thread) */
static void show_media_preview(AppData* app, FileKind kind, const gchar* name, const gchar* full) {
    if (kind == FILE_KIND_AUDIO) {
        video_preview_stop(app);
        render_audio_preview(app, name, full);
        return;
    }
    if (kind == FILE_KIND_VIDEO) {
        audio_preview_stop(app);
        render_video_preview(app, name, full);
        return;
    }
    /* Leave the player page before stopping it (its controls must not be
     * redrawn unallocated) */
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    audio_preview_stop(app);
    video_preview_stop(app);
    if (kind == FILE_KIND_PDF) {
        render_pdf_preview(app, name, full);
    } else {
        render_image_preview(app, name, full);
    }
}

static void show_text_preview(TextPreviewTaskData* td);

/* Previews already built (the worker's results), shown again at once when a
 * file is selected again or a workspace comes back to it */
#define PREVIEW_CACHE_ENTRIES 24
#define PREVIEW_CACHE_BYTES   (16 * 1024 * 1024)

static PreviewCache* preview_cache(void) {
    static PreviewCache* cache = NULL;
    if (cache == NULL) {
        cache = preview_cache_new(PREVIEW_CACHE_ENTRIES, PREVIEW_CACHE_BYTES,
                                  text_preview_task_data_free);
    }
    return cache;
}

/* Cache key: the file and the settings the worker built the preview with */
static gchar* preview_cache_key(const gchar* full, gboolean render_raw, guint page) {
    return g_strdup_printf("%d:%u:%s", render_raw ? 1 : 0, page, full);
}

static gsize preview_doc_bytes(const PreviewDoc* doc) {
    if (doc == NULL) {
        return 0;
    }
    gsize bytes = sizeof(PreviewDoc) + (doc->text != NULL ? doc->text->len : 0);
    if (doc->spans != NULL) {
        bytes += doc->spans->len * sizeof(PreviewSpan);
    }
    if (doc->images != NULL) {
        for (guint i = 0; i < doc->images->len; i++) {
            GBytes* data = g_array_index(doc->images, PreviewImage, i).data;
            bytes += (data != NULL) ? g_bytes_get_size(data) : 0;
        }
    }
    return bytes;
}

/* Keep the results of a finished worker (moved out of td) */
static void cache_text_preview(TextPreviewTaskData* td) {
    if (!td->has_stamp || td->error_msg != NULL) {
        return;
    }
    TextPreviewTaskData* kept = g_memdup2(td, sizeof(TextPreviewTaskData));
    td->name = NULL;
    td->full = NULL;
    td->content_type = NULL;
    td->utf8_text = NULL;
    td->doc = NULL;
    gsize bytes = sizeof(TextPreviewTaskData) +
                  (kept->utf8_text != NULL ? strlen(kept->utf8_text) : 0) +
                  preview_doc_bytes(kept->doc);
    gchar* key = preview_cache_key(kept->full, kept->render_raw, kept->page);
    preview_cache_insert(preview_cache(), key, &kept->stamp, kept, bytes);
    g_free(key);
}

/* LOOKIN_DEBUG_PREVIEW=1: time from the selection to the preview on screen */
static gboolean debug_preview_timings(void) {
    static gint enabled = -1;
    if (enabled < 0) {
        enabled = g_getenv("LOOKIN_DEBUG_PREVIEW") != NULL;
    }
    return enabled;
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
    if (td->unchanged) {
        return; /* the cached preview shown at the selection is current */
    }
    gint64 ready_us = g_get_monotonic_time();
    show_text_preview(td);
    if (debug_preview_timings()) {
        gint64 end_us = g_get_monotonic_time();
        g_printerr("preview %s [%s]: %.1f ms (worker %.1f ms, display %.1f ms)\n", td->name,
                   file_kind_label(td->kind) != NULL ? file_kind_label(td->kind) : "Text",
                   (end_us - td->start_us) / 1000.0, (ready_us - td->start_us) / 1000.0,
                   (end_us - ready_us) / 1000.0);
    }
    cache_text_preview(td);
}

static void show_text_preview(TextPreviewTaskData* td) {
    AppData* app = td->app;
    app->preview_pages = td->doc != NULL ? td->doc->pages : 0;
    if (app->preview_page > app->preview_pages) app->preview_page = app->preview_pages;

    if (g_strcmp0(td->full, app->viewed_path) == 0 &&
        (app->viewed_kind != td->kind || g_strcmp0(app->viewed_content_type, td->content_type) != 0 ||
         app->viewed_encoding != td->encoding)) {
        app->viewed_kind = td->kind;
        app->viewed_encoding = td->encoding;
        g_free(app->viewed_content_type);
        app->viewed_content_type = g_strdup(td->content_type);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }

    if (file_kind_is_media(td->kind)) {
        show_media_preview(app, td->kind, td->name, td->full);
        return;
    }

    /* Leave the player page before stopping it (its controls must not be
     * redrawn unallocated) */
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    audio_preview_stop(app);
    video_preview_stop(app);

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        apply_viewer_language(app, NULL, NULL);
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    if (td->doc != NULL) {
        apply_viewer_language(app, NULL, NULL);
        preview_doc_show(GTK_TEXT_VIEW(app->text_view), td->doc, td->full, td->name, td->fsize);
        return;
    }

    gboolean is_md = (td->kind == FILE_KIND_MARKDOWN) && !td->render_raw;
    gboolean is_html = (td->kind == FILE_KIND_HTML) && !td->render_raw;
    gboolean is_org = (td->kind == FILE_KIND_ORG) && !td->render_raw;
    gboolean is_rendered = is_md || is_html || is_org;
    gboolean code = FALSE;
    if (is_rendered) {
        apply_viewer_language(app, NULL, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        set_view_text_padding(app->text_view);
    } else {
        gboolean has_lang = apply_viewer_language(app, td->name, td->content_type);
        code = has_lang || is_code_file(td->name) || td->render_raw ||
                        (td->utf8_text != NULL && g_str_has_prefix(td->utf8_text, "#!"));
        /* File contents (code or plain text) in the code font, as in editors;
         * rendered documents use the interface font */
        set_view_monospace(app->text_view, TRUE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        set_view_text_padding(app->text_view);
    }

    if (td->utf8_text != NULL) {
        if (is_md) {
            ensure_markdown_tags(app->buffer);
            render_markdown_to_buffer(app->buffer, GTK_TEXT_VIEW(app->text_view), td->utf8_text, td->full,
                                      td->preview, td->name, td->fsize);
        } else if (is_html) {
            ensure_html_tags(app->buffer);
            render_html_to_buffer(app->buffer, td->utf8_text, td->full,
                                  td->preview, td->name, td->fsize);
        } else if (is_org) {
            ensure_orgmode_tags(app->buffer);
            render_orgmode_to_buffer(app->buffer, td->utf8_text, td->full,
                                     td->preview, td->name, td->fsize,
                                     app->org_folded_lines, app->org_global_fold);
        } else {
            GString* text = g_string_new(NULL);
            if (td->preview) {
                g_string_append_printf(text,
                        "[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                        "Large file: %u bytes shown, the rest loads as you scroll down.\n\n",
                        td->name, td->fsize, (guint)td->raw_len);
            }
            /* Line numbers start after the truncation header */
            gint first_line = 0;
            for (const gchar* c = text->str; *c != '\0'; c++) {
                first_line += (*c == '\n');
            }
            g_string_append(text, td->utf8_text);
            gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
            g_string_free(text, TRUE);
            if (code) {
                diff_gutter_set_line_numbers(app->text_view, first_line);
            }
            /* Git change bars, for whole files only (a truncated preview has a header) */
            if (!td->preview) {
                diff_gutter_set_file(app->text_view, td->full);
            } else {
                text_chunks_start(app, td->full, (gint64)td->raw_len, td->fsize, first_line, code);
            }
        }
    }
}

/* Preview a file. Its kind is detected from its content in a worker thread
 * (one read serves detection and text), so the UI thread does no file I/O. */
void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    text_chunks_stop(app);
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
    td->page = app->render_raw ? 0 : app->preview_page;
    td->start_us = g_get_monotonic_time();
    if (g_strcmp0(full, app->viewed_path) != 0) {
        app->preview_page = 0;
        app->preview_pages = 0;
        /* Provisional kind until the content is read */
        app->viewed_kind = file_kind_from_name(name);
        g_clear_pointer(&app->viewed_content_type, g_free);
    }

    /* Built before: show it now; the worker only checks the file is unchanged.
     * Not for the file already shown (reloaded because it changed): it would
     * only draw the old content again before the new one. */
    TextPreviewTaskData* cached = NULL;
    if (g_strcmp0(full, app->viewed_path) != 0) {
        gchar* key = preview_cache_key(full, td->render_raw, td->page);
        cached = preview_cache_lookup(preview_cache(), key, &td->cached_stamp);
        g_free(key);
    }
    if (cached != NULL) {
        td->has_cached = TRUE;
        cached->app = app;
        show_text_preview(cached);
        app->viewed_kind = cached->kind;
        app->viewed_encoding = cached->encoding;
        g_free(app->viewed_content_type);
        app->viewed_content_type = g_strdup(cached->content_type);
        if (debug_preview_timings()) {
            g_printerr("preview %s [cached]: %.1f ms\n", name,
                       (g_get_monotonic_time() - td->start_us) / 1000.0);
        }
    }

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_text_preview_ready, NULL);
    g_task_set_task_data(task, td, text_preview_task_data_free);
    g_task_run_in_thread(task, text_preview_thread_func);
    g_object_unref(task);
}

/* Re-render the file currently shown in the preview (APP_EVENT_PREVIEW_STALE) */
void viewer_reload_preview(AppData* app) {
    if (app->viewed_path == NULL) {
        /* A folder: its overview again (git counts, theme); other rows
         * without a file (summaries) have nothing to read again */
        ExplorerRow* row = tree_nav_selected(app);
        if (row != NULL && explorer_row_is(row, "dir") && row->path[0] != '\0') {
            dir_preview_show(app, row->name, row->path);
        }
        return;
    }
    gchar* fallback = (app->viewed_name == NULL && app->viewed_path != NULL)
                          ? g_path_get_basename(app->viewed_path)
                          : NULL;
    load_file_content(app, app->viewed_name != NULL ? app->viewed_name : fallback, app->viewed_path);
    g_free(fallback);
}


void on_selection_changed(ExplorerTree* tree, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    ExplorerRow* row = explorer_tree_get_selected(tree);
    if (row == NULL) {
        return;
    }
    if (g_strcmp0(row->path, app->viewed_path) != 0) {
        text_chunks_stop(app); /* another entry: no more chunks of the previous file */
    }

    gchar* name = g_strdup(row->name);
    gchar* type = g_strdup(row->type);
    gchar* full = g_strdup(row->path);

    /* Rows without file content (remote machine, notebook, bookmark folder,
     * notes trash): show a text summary instead of keeping the previous preview */
    gboolean is_remote = (g_strcmp0(type, "remote") == 0);
    gchar* summary = is_remote ? preview_summary_remote(remote_find_by_id(app, full))
                               : preview_summary_container(row);
    /* A bookmark's target is never read here (a slow mount would freeze the
     * window): its kind comes from bookmark_icon_check(), which selects the
     * row again once known */
    gboolean is_bookmark = (g_strcmp0(type, "bookmark") == 0);
    PathKind bm_kind = is_bookmark ? path_kind_lookup(full) : PATH_KIND_UNKNOWN;
    if (is_bookmark && bm_kind == PATH_KIND_UNKNOWN && !bookmark_target_is_uri(full)) {
        summary = g_strdup_printf("🔖 %s\n%s\n\n(waiting for the file system…)\n", name, full);
    }
    if (is_remote || summary != NULL) {
        if (app != NULL && !app->editor_dedicated && app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
        /* Leave the player page before stopping it (its controls must not be
         * redrawn unallocated) */
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_cancellable != NULL) {
            g_cancellable_cancel(app->preview_cancellable);
            g_clear_object(&app->preview_cancellable);
        }
        if (summary != NULL && app->buffer != NULL) {
            apply_viewer_language(app, NULL, NULL);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
            set_view_text_padding(app->text_view);
            set_view_monospace(app->text_view, TRUE); /* summary columns are space-aligned */
            gtk_text_buffer_set_text(app->buffer, summary, -1);
        }
        g_free(summary);
        g_free(app->viewed_path);
        g_free(app->viewed_name);
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);

        g_free(name);
        g_free(type);
        g_free(full);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        return;
    }

    /* Placeholders, URL bookmarks and bookmarks to missing locations have no preview */
    if (name == NULL || type == NULL || g_strcmp0(type, "-") == 0 ||
        g_strcmp0(type, "buffer_create") == 0 ||
        full == NULL || (is_bookmark && bm_kind != PATH_KIND_DIR && bm_kind != PATH_KIND_FILE)) {
        g_free(name);
        g_free(type);
        g_free(full);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        return;
    }
    gboolean as_dir = (g_strcmp0(type, "dir") == 0) ||
                      (is_bookmark && bm_kind == PATH_KIND_DIR);
    if ((is_bookmark || g_strcmp0(type, "note") == 0 || g_strcmp0(type, "trash_note") == 0) && !as_dir) {
        /* Detect the file type from its real name */
        g_free(name);
        name = g_path_get_basename(full);
    }

    if (app != NULL && !app->editor_dedicated && app->notebook != NULL) {
        /* Do not return to preview tab if the user is currently editing in an editor tab,
         * unless the tree view has focus (e.g. user navigating or clicking in the tree view)
         * or a different file/note was selected in the explorer panel. */
        EditorTab* cur = editor_current_tab(app);
        gboolean is_diff_file = (cur != NULL && full != NULL && g_strcmp0(full, cur->path) != 0);
        gboolean tree_focused = (app->tree_view != NULL && gtk_widget_has_focus(GTK_WIDGET(app->tree_view)));
        if (cur == NULL || is_diff_file || tree_focused) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
    }

    if (as_dir) {
        /* Leave the player page before stopping it (its controls must not be
         * redrawn unallocated) */
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        audio_preview_stop(app);
        video_preview_stop(app);
        dir_preview_show(app, name, full);
    } else {
        /* If there's an open editor tab for this file with modified/buffered content, flush it */
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && g_strcmp0(t->path, full) == 0) {
                if (t->buffer != NULL && (t->is_buffer || gtk_text_buffer_get_modified(t->buffer))) {
                    GtkTextIter start, end;
                    gtk_text_buffer_get_bounds(t->buffer, &start, &end);
                    gchar* text = gtk_text_buffer_get_text(t->buffer, &start, &end, FALSE);
                    g_file_set_contents(t->path, text, -1, NULL);
                    g_free(text);
                    gtk_text_buffer_set_modified(t->buffer, FALSE);
                    editor_tab_label_update(t);
                    app->file_dirty = TRUE;
                }
                break;
            }
        }

        if (g_strcmp0(full, app->viewed_path) == 0 && !app->file_dirty &&
            (app->viewed_kind == FILE_KIND_VIDEO || app->viewed_kind == FILE_KIND_AUDIO ||
             gtk_text_buffer_get_char_count(app->buffer) > 0)) {
            if (!app->switching_ws) {
                app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            }
            g_free(full);
            g_free(name);
            g_free(type);
            return;
        }
        if (!app->switching_ws && g_strcmp0(name, app->viewed_name) != 0) {
            app->render_raw = FALSE;
            app->preview_zoom = 1.0;
        }
        app->file_dirty = FALSE;
        load_file_content(app, name, full);
    }

    g_free(app->viewed_path);
    g_free(app->viewed_name);
    if (as_dir) {
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);
    } else {
        app->viewed_path = g_strdup(full);
        app->viewed_name = g_strdup(name);
        monitor_file(app, full);
    }

    if (!app->switching_ws) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }

    app_emit(app, APP_EVENT_STATUS_CHANGED);

    g_free(full);
    g_free(name);
    g_free(type);
}

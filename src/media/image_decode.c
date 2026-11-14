/* Image decoding for the preview: worker, cache and prefetch (see
 * image_decode.h). */
#include "media/image_decode.h"

#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "core/file_types.h"
#include "core/i18n.h"
#include "media/exif.h"
#include "media/image_utils.h"
#include "preview/preview_cache.h"

/* Largest decoded image, in pixels (zooming in on a large photo) */
#define MAX_DECODED_PIXELS (48u * 1024 * 1024)

/* Largest image kept decoded for the zoom steps, in pixels (64 MB) */
#define MAX_KEPT_PIXELS (16u * 1024 * 1024)

/* Largest RAW file read for its embedded preview */
#define MAX_RAW_FILE (256u * 1024 * 1024)

/* Images cached at the zoom of 1: the previewed one, those seen just
 * before and those decoded ahead. Each is found by the box it was fitted to
 * (a hit reads nothing) and by the size it is shown at (the same image
 * fitted to another box of the same fit: a hit reads the header only). */
#define CACHE_ENTRIES 24
#define CACHE_BYTES   (96u * 1024 * 1024)

void image_decoded_free(ImageDecoded* decoded) {
    if (decoded == NULL) {
        return;
    }
    g_free(decoded->error_msg);
    g_free(decoded->fmt_upper);
    g_free(decoded->size_str);
    g_strfreev(decoded->details);
    g_clear_object(&decoded->texture);
    animation_frames_free(decoded->animation);
    g_free(decoded);
}

/* A copy sharing the texture (an animation is not copied) */
static ImageDecoded* decoded_copy(const ImageDecoded* d) {
    ImageDecoded* c = g_new0(ImageDecoded, 1);
    c->error_msg = g_strdup(d->error_msg);
    c->fmt_upper = g_strdup(d->fmt_upper);
    c->size_str = g_strdup(d->size_str);
    c->orig_w = d->orig_w;
    c->orig_h = d->orig_h;
    c->target_w = d->target_w;
    c->target_h = d->target_h;
    c->is_fitted = d->is_fitted;
    c->details = g_strdupv(d->details);
    c->texture = d->texture != NULL ? g_object_ref(d->texture) : NULL;
    return c;
}

/* --- Cache of the decoded images (shared by the workers) --- */

static GMutex cache_lock;
static PreviewCache* cache;

static gchar* cache_key(const gchar* path, const ImageDecodeSize* size) {
    return g_strdup_printf("%s\n%d\n%.4f\n%d\n%d", path, size->render_raw ? 1 : 0, size->zoom,
                           size->box_w, size->box_h);
}

static gchar* shown_key(const gchar* path, const ImageDecodeSize* size, const ImageDecoded* d) {
    return g_strdup_printf("%s\n%d\n=%d\n%d", path, size->render_raw ? 1 : 0, d->target_w, d->target_h);
}

static ImageDecoded* cache_lookup(const gchar* key, const FileStamp* stamp) {
    ImageDecoded* found = NULL;
    g_mutex_lock(&cache_lock);
    if (cache != NULL) {
        FileStamp cached;
        const ImageDecoded* d = preview_cache_lookup(cache, key, &cached);
        if (d != NULL && file_stamp_equal(&cached, stamp)) {
            found = decoded_copy(d);
        }
    }
    g_mutex_unlock(&cache_lock);
    return found;
}

static void cache_insert(const gchar* key, const FileStamp* stamp, const ImageDecoded* d) {
    gsize bytes = 4096;
    if (d->texture != NULL) {
        bytes += (gsize)gdk_texture_get_width(d->texture) * (gsize)gdk_texture_get_height(d->texture) * 4;
    }
    g_mutex_lock(&cache_lock);
    if (cache == NULL) {
        cache = preview_cache_new(CACHE_ENTRIES, CACHE_BYTES, (GDestroyNotify)image_decoded_free);
    }
    preview_cache_insert(cache, key, stamp, decoded_copy(d), bytes);
    g_mutex_unlock(&cache_lock);
}

/* --- Decoding --- */

/* The last image decoded whole, upright, for the zoom: a step scales it
 * instead of reading and decoding the file again. One image only, shared by
 * the workers (hence the lock), replaced when another file is zoomed. */
static GMutex kept_lock;
static gchar* kept_path;
static gint64 kept_mtime;
static gint64 kept_size;
static GdkPixbuf* kept_pix;

/* The kept image of path scaled to w x h (new reference), NULL when it is
 * another file, changed since, or smaller than asked */
static GdkPixbuf* kept_scaled(const gchar* path, const GStatBuf* st, gint w, gint h) {
    GdkPixbuf* src = NULL;
    g_mutex_lock(&kept_lock);
    if (kept_pix != NULL && g_strcmp0(kept_path, path) == 0 && kept_mtime == (gint64)st->st_mtime &&
        kept_size == (gint64)st->st_size && gdk_pixbuf_get_width(kept_pix) >= w &&
        gdk_pixbuf_get_height(kept_pix) >= h) {
        src = g_object_ref(kept_pix);
    }
    g_mutex_unlock(&kept_lock);
    if (src == NULL) {
        return NULL;
    }
    GdkPixbuf* pix = (gdk_pixbuf_get_width(src) == w && gdk_pixbuf_get_height(src) == h)
                         ? g_object_ref(src)
                         : gdk_pixbuf_scale_simple(src, w, h, GDK_INTERP_BILINEAR);
    g_object_unref(src);
    return pix;
}

static void keep_decoded(const gchar* path, const GStatBuf* st, GdkPixbuf* pix) {
    g_mutex_lock(&kept_lock);
    g_set_str(&kept_path, path);
    kept_mtime = (gint64)st->st_mtime;
    kept_size = (gint64)st->st_size;
    g_set_object(&kept_pix, pix);
    g_mutex_unlock(&kept_lock);
}

/* Turn a decoded image upright from its EXIF orientation (2..8) */
static GdkPixbuf* apply_orientation(GdkPixbuf* pix, gint orientation) {
    GdkPixbuf* a = NULL;
    GdkPixbuf* b = NULL;
    switch (orientation) {
    case 2: a = gdk_pixbuf_flip(pix, TRUE); break;
    case 3: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_UPSIDEDOWN); break;
    case 4: a = gdk_pixbuf_flip(pix, FALSE); break;
    case 5:
        b = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE);
        a = b != NULL ? gdk_pixbuf_flip(b, TRUE) : NULL;
        break;
    case 6: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE); break;
    case 7:
        b = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE);
        a = b != NULL ? gdk_pixbuf_flip(b, FALSE) : NULL;
        break;
    case 8: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE); break;
    default: break;
    }
    g_clear_object(&b);
    if (a == NULL) {
        return pix;
    }
    g_object_unref(pix);
    return a;
}

/* First bytes of a file */
static gsize read_head(const gchar* path, guchar* buf, gsize len) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    gsize n = fread(buf, 1, len, fp);
    fclose(fp);
    return n;
}

/* Embedded JPEG preview of a camera RAW file, and its EXIF details */
static GdkPixbuf* raw_preview(const gchar* path, ExifInfo** exif) {
    gchar* data = NULL;
    gsize len = 0;
    GStatBuf st;
    if (g_stat(path, &st) != 0 || (guint64)st.st_size > MAX_RAW_FILE ||
        !g_file_get_contents(path, &data, &len, NULL)) {
        return NULL;
    }
    *exif = exif_parse((const guchar*)data, len);
    gsize off = 0, n = 0;
    GdkPixbuf* pix = NULL;
    if (exif_find_preview((const guchar*)data, len, &off, &n)) {
        pix = pixbuf_from_bytes((const guchar*)data + off, n, 0);
    }
    g_free(data);
    return pix;
}

/* Why an image cannot be decoded: the loader to install for its format */
static gchar* missing_loader_message(const gchar* name, const gchar* path) {
    guchar head[32];
    gsize n = read_head(path, head, sizeof(head));
    const gchar* format = NULL;
    const gchar* package = NULL;
    if (n >= 12 && memcmp(head + 4, "ftyp", 4) == 0) {
        if (memcmp(head + 8, "avif", 4) == 0 || memcmp(head + 8, "avis", 4) == 0) {
            format = "AVIF";
            package = "libavif-gdk-pixbuf";
        } else {
            format = "HEIC";
            package = "heif-gdk-pixbuf";
        }
    } else if ((n >= 2 && head[0] == 0xFF && head[1] == 0x0A) || (n >= 12 && memcmp(head + 4, "JXL ", 4) == 0)) {
        format = "JPEG XL";
        package = "libjxl-gdk-pixbuf";
    } else if (n >= 12 && memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WEBP", 4) == 0) {
        format = "WebP";
        package = "webp-pixbuf-loader";
    }
    if (package != NULL) {
        return g_strdup_printf(_("[ %1$s: no image loader for %2$s is installed (package %3$s) ]"), name, format, package);
    }
    return g_strdup_printf(_("[ Cannot decode image: %s ]"), name);
}

/* Decode path for size; NULL when cancelled. With stamp (the file's
 * version), the image cached at the size shown is taken, or the image
 * decoded is cached. */
static ImageDecoded* decode(const gchar* path, const gchar* name, const ImageDecodeSize* size,
                            const FileStamp* stamp, GCancellable* cancellable) {
    ImageDecoded* d = g_new0(ImageDecoded, 1);

    /* Camera RAW: the JPEG preview the camera embedded (no RAW decoding) */
    ExifInfo* exif = NULL;
    GdkPixbuf* embedded = NULL;
    gint file_w = 0, file_h = 0;
    guchar head[16];
    gsize head_len = read_head(path, head, sizeof(head));
    if (is_raw_photo_file(name) || (head_len >= 15 && memcmp(head, "FUJIFILMCCD-RAW", 15) == 0)) {
        embedded = raw_preview(path, &exif);
    }
    if (embedded != NULL) {
        file_w = gdk_pixbuf_get_width(embedded);
        file_h = gdk_pixbuf_get_height(embedded);
        gchar* ext = g_ascii_strup(file_extension(name), -1);
        d->fmt_upper = g_strdup_printf(_("%s RAW (embedded preview)"), ext);
        g_free(ext);
    } else {
        exif_info_free(exif);
        exif = NULL;
        GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(path, &file_w, &file_h);
        if (fmt == NULL || file_w <= 0 || file_h <= 0) {
            d->error_msg = missing_loader_message(name, path);
            return d;
        }
        gchar* fmt_name = gdk_pixbuf_format_get_name(fmt);
        d->fmt_upper = g_ascii_strup(fmt_name, -1);
        g_free(fmt_name);
        exif = exif_read_file(path);
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        exif_info_free(exif);
        g_clear_object(&embedded);
        image_decoded_free(d);
        return NULL;
    }

    GStatBuf st;
    gboolean stat_ok = (g_stat(path, &st) == 0);
    gint64 fsize = stat_ok ? (gint64)st.st_size : 0;
    d->size_str = g_format_size((guint64)fsize);

    /* Photo details; orientations 5 to 8 swap width and height */
    gint orientation = exif != NULL ? exif->orientation : 0;
    if (exif != NULL) {
        d->details = exif_summary_lines(exif);
        if (d->details[0] == NULL) {
            g_clear_pointer(&d->details, g_strfreev);
        }
        exif_info_free(exif);
    }
    gboolean swap = orientation >= 5 && orientation <= 8;
    d->orig_w = swap ? file_h : file_w;
    d->orig_h = swap ? file_w : file_h;

    gdouble scale = 1.0;
    if (!size->render_raw) {
        gint max_w = (size->box_w > 100) ? (size->box_w - 40) : 700;
        gint max_h = (size->box_h > 140) ? (size->box_h - 120) : 500;
        if (d->orig_w > max_w || d->orig_h > max_h) {
            scale = MIN((gdouble)max_w / d->orig_w, (gdouble)max_h / d->orig_h);
            d->is_fitted = TRUE;
        }
    }
    scale *= size->zoom;
    gdouble pixels = (gdouble)d->orig_w * d->orig_h * scale * scale;
    if (pixels > MAX_DECODED_PIXELS) {
        scale *= sqrt(MAX_DECODED_PIXELS / pixels);
    }
    d->target_w = MAX(1, (gint)(d->orig_w * scale + 0.5));
    d->target_h = MAX(1, (gint)(d->orig_h * scale + 0.5));
    gint load_w = swap ? d->target_h : d->target_w;
    gint load_h = swap ? d->target_w : d->target_h;

    gchar* key = (stamp != NULL && size->zoom == 1.0) ? shown_key(path, size, d) : NULL;
    ImageDecoded* cached = key != NULL ? cache_lookup(key, stamp) : NULL;
    if (cached != NULL) {
        g_free(key);
        g_clear_object(&embedded);
        image_decoded_free(d);
        return cached;
    }

    /* Animated GIF: frames decoded and scaled here, played by the view
     * (other animated formats show their first frame) */
    if (embedded == NULL && head_len >= 6 && memcmp(head, "GIF8", 4) == 0) {
        d->animation = animation_frames_load_gif(path, d->target_w, d->target_h, cancellable);
        if (g_cancellable_is_cancelled(cancellable)) {
            g_free(key);
            image_decoded_free(d);
            return NULL;
        }
        if (d->animation != NULL) {
            g_free(key); /* the frames are not cached */
            return d;
        }
    }

    GdkPixbuf* pix;
    gboolean upright = FALSE; /* pix already has the EXIF orientation applied */
    if (embedded != NULL) {
        pix = gdk_pixbuf_scale_simple(embedded, load_w, load_h, GDK_INTERP_BILINEAR);
        g_object_unref(embedded);
    } else if (stat_ok && size->zoom != 1.0 &&
               (pix = kept_scaled(path, &st, d->target_w, d->target_h)) != NULL) {
        upright = TRUE; /* a zoom step on the image decoded before */
    } else {
        /* Zoomed: the whole image (capped) is decoded once and kept, the
         * next steps scale it; otherwise the size shown (the cache keeps it) */
        gint dec_w = load_w, dec_h = load_h;
        gboolean keep = FALSE;
        gdouble file_px = (gdouble)file_w * file_h;
        if (size->zoom != 1.0) {
            gdouble s = (file_px > MAX_KEPT_PIXELS) ? sqrt(MAX_KEPT_PIXELS / file_px) : 1.0;
            gint whole_w = MAX(1, (gint)(file_w * s));
            gint whole_h = MAX(1, (gint)(file_h * s));
            if (whole_w >= load_w && whole_h >= load_h) {
                dec_w = whole_w;
                dec_h = whole_h;
            }
            keep = stat_ok && (gdouble)dec_w * dec_h <= MAX_KEPT_PIXELS;
        }
        GError* err = NULL;
        pix = gdk_pixbuf_new_from_file_at_scale(path, dec_w, dec_h, TRUE, &err);
        if (pix != NULL) {
            pix = apply_orientation(pix, orientation);
            upright = TRUE;
            if (keep) {
                keep_decoded(path, &st, pix);
            }
            if (gdk_pixbuf_get_width(pix) != d->target_w || gdk_pixbuf_get_height(pix) != d->target_h) {
                GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pix, d->target_w, d->target_h,
                                                            GDK_INTERP_BILINEAR);
                g_object_unref(pix);
                pix = scaled;
            }
        }
        if (pix == NULL) {
            d->error_msg = g_strdup_printf(_("[ Error loading image: %1$s (%2$s) ]"),
                                           name, err ? err->message : _("unknown error"));
            if (err != NULL) {
                g_error_free(err);
            }
            g_free(key);
            return d;
        }
    }
    if (pix == NULL) {
        d->error_msg = g_strdup_printf(_("[ Cannot decode image: %s ]"), name);
        g_free(key);
        return d;
    }
    if (!upright) {
        pix = apply_orientation(pix, orientation);
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        g_free(key);
        g_object_unref(pix);
        image_decoded_free(d);
        return NULL;
    }

    d->texture = texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    if (key != NULL) {
        cache_insert(key, stamp, d);
        g_free(key);
    }
    return d;
}

/* --- Requests (main thread) --- */

/* A request waiting for a decoding */
typedef struct {
    ImageDecodeReady ready;
    gpointer user_data;
    GDestroyNotify destroy;
    GCancellable* cancellable; /* the requester's, or NULL */
    gulong cancelled_id;
    gboolean dead;             /* cancelled: not called */
    struct Job* job;
} Waiter;

/* One decoding in a worker, shared by the requests of the same image */
typedef struct Job {
    gchar* key;
    gchar* path;
    gchar* name;
    ImageDecodeSize size;
    GCancellable* cancellable; /* the decoding's own */
    GList* waiters;            /* Waiter* */
    gboolean prefetch;         /* wanted by the current prefetch */
} Job;

static GHashTable* jobs; /* key -> Job*, decodings running */

static void job_free(Job* job) {
    g_free(job->key);
    g_free(job->path);
    g_free(job->name);
    g_object_unref(job->cancellable);
    g_free(job);
}

/* Nobody wants the decoding any more: stop it */
static void job_check(Job* job) {
    if (job->prefetch) {
        return;
    }
    for (GList* l = job->waiters; l != NULL; l = l->next) {
        if (!((Waiter*)l->data)->dead) {
            return;
        }
    }
    g_cancellable_cancel(job->cancellable);
}

static void on_waiter_cancelled(GCancellable* cancellable, gpointer data) {
    (void)cancellable;
    Waiter* w = data;
    w->dead = TRUE;
    job_check(w->job);
}

static void waiter_free(Waiter* w) {
    if (w->cancellable != NULL) {
        g_cancellable_disconnect(w->cancellable, w->cancelled_id);
        g_object_unref(w->cancellable);
    }
    if (w->destroy != NULL) {
        w->destroy(w->user_data);
    }
    g_free(w);
}

static void decode_thread_func(GTask* task, gpointer source_object, gpointer task_data,
                               GCancellable* cancellable) {
    (void)source_object;
    const Job* job = task_data;
    FileStamp stamp;
    gboolean cached = job->size.zoom == 1.0 && file_stamp_read(job->path, &stamp);
    ImageDecoded* d = cached ? cache_lookup(job->key, &stamp) : NULL;
    if (d == NULL) {
        d = decode(job->path, job->name, &job->size, cached ? &stamp : NULL, cancellable);
        if (d != NULL && cached && d->texture != NULL) {
            cache_insert(job->key, &stamp, d);
        }
    }
    g_task_return_pointer(task, d, (GDestroyNotify)image_decoded_free);
}

static void on_decoded(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    (void)user_data;
    Job* job = g_task_get_task_data(G_TASK(res));
    if (jobs != NULL && g_hash_table_lookup(jobs, job->key) == job) {
        g_hash_table_remove(jobs, job->key);
    }
    ImageDecoded* d = g_task_propagate_pointer(G_TASK(res), NULL);
    GList* waiters = g_steal_pointer(&job->waiters);
    /* The first request takes the result (and the animation), the others a copy */
    for (GList* l = waiters; l != NULL; l = l->next) {
        Waiter* w = l->data;
        if (d != NULL && !w->dead && !g_cancellable_is_cancelled(w->cancellable)) {
            ImageDecoded* mine = l->next != NULL ? decoded_copy(d) : g_steal_pointer(&d);
            w->ready(mine, w->user_data);
        }
    }
    g_list_free_full(waiters, (GDestroyNotify)waiter_free);
    image_decoded_free(d);
}

/* The running decoding of key, or a new one */
static Job* job_get(const gchar* path, const gchar* name, const ImageDecodeSize* size) {
    if (jobs == NULL) {
        jobs = g_hash_table_new(g_str_hash, g_str_equal);
    }
    gchar* key = cache_key(path, size);
    Job* job = g_hash_table_lookup(jobs, key);
    if (job != NULL && !g_cancellable_is_cancelled(job->cancellable)) {
        g_free(key);
        return job;
    }
    job = g_new0(Job, 1);
    job->key = key;
    job->path = g_strdup(path);
    job->name = g_strdup(name);
    job->size = *size;
    job->cancellable = g_cancellable_new();
    g_hash_table_replace(jobs, job->key, job);

    GTask* task = g_task_new(NULL, job->cancellable, on_decoded, NULL);
    g_task_set_task_data(task, job, (GDestroyNotify)job_free);
    g_task_set_return_on_cancel(task, FALSE);
    g_task_run_in_thread(task, decode_thread_func);
    g_object_unref(task);
    return job;
}

void image_decode_async(const gchar* path, const gchar* name, const ImageDecodeSize* size,
                        GCancellable* cancellable, ImageDecodeReady ready, gpointer user_data,
                        GDestroyNotify destroy) {
    Job* job = job_get(path, name, size);
    Waiter* w = g_new0(Waiter, 1);
    w->ready = ready;
    w->user_data = user_data;
    w->destroy = destroy;
    w->job = job;
    job->waiters = g_list_append(job->waiters, w);
    if (cancellable != NULL) {
        w->cancellable = g_object_ref(cancellable);
        /* Runs at once when already cancelled */
        w->cancelled_id = g_cancellable_connect(cancellable, G_CALLBACK(on_waiter_cancelled), w, NULL);
    }
}

void image_decode_prefetch(const gchar* const* paths, const ImageDecodeSize* size) {
    GPtrArray* wanted = g_ptr_array_new();
    for (gint i = 0; paths != NULL && paths[i] != NULL; i++) {
        gchar* name = g_path_get_basename(paths[i]);
        g_ptr_array_add(wanted, job_get(paths[i], name, size));
        g_free(name);
    }
    if (jobs != NULL) {
        GHashTableIter iter;
        gpointer value;
        g_hash_table_iter_init(&iter, jobs);
        while (g_hash_table_iter_next(&iter, NULL, &value)) {
            Job* job = value;
            gboolean keep = g_ptr_array_find(wanted, job, NULL);
            if (job->prefetch && !keep) {
                job->prefetch = FALSE;
                job_check(job);
            } else if (keep) {
                job->prefetch = TRUE;
            }
        }
    }
    g_ptr_array_free(wanted, TRUE);
}

GdkTexture* image_decode_whole(const gchar* path) {
    gchar* name = g_path_get_basename(path);
    ExifInfo* exif = NULL;
    GdkPixbuf* pix = NULL;
    if (is_raw_photo_file(name)) {
        pix = raw_preview(path, &exif);
    }
    g_free(name);
    if (pix == NULL) {
        exif_info_free(exif);
        exif = exif_read_file(path);
        gint w = 0, h = 0;
        if (gdk_pixbuf_get_file_info(path, &w, &h) != NULL && w > 0 && h > 0) {
            gdouble pixels = (gdouble)w * h;
            if (pixels > MAX_DECODED_PIXELS) {
                gdouble scale = sqrt(MAX_DECODED_PIXELS / pixels);
                pix = gdk_pixbuf_new_from_file_at_scale(path, MAX(1, (gint)(w * scale)),
                                                        MAX(1, (gint)(h * scale)), TRUE, NULL);
            } else {
                pix = gdk_pixbuf_new_from_file(path, NULL);
            }
        }
    }
    if (pix == NULL) {
        exif_info_free(exif);
        return NULL;
    }
    pix = apply_orientation(pix, exif != NULL ? exif->orientation : 0);
    exif_info_free(exif);
    GdkTexture* texture = texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    return texture;
}

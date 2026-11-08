#include "media_tracks.h"
#include "i18n.h"

#include <string.h>

/* Names muxers give tracks that have none: "Audio", "Subtitle"... */
static gboolean is_generic_title(const gchar* title) {
    static const gchar* const generic[] = {"audio", "video", "subtitle", "subtitles", "text"};
    for (gsize i = 0; i < G_N_ELEMENTS(generic); i++) {
        if (g_ascii_strcasecmp(title, generic[i]) == 0) return TRUE;
    }
    return FALSE;
}

gchar* media_track_label(const gchar* language, const gchar* title, guint index) {
    gboolean has_lang = language != NULL && language[0] != '\0' && g_ascii_strcasecmp(language, "und") != 0;
    gboolean has_title = title != NULL && title[0] != '\0' && !is_generic_title(title);
    if (has_lang && has_title && g_ascii_strcasecmp(language, title) != 0) {
        return g_strdup_printf("%s - %s", language, title);
    }
    if (has_lang) return g_strdup(language);
    if (has_title) return g_strdup(title);
    return g_strdup_printf(_("Track %u"), index + 1);
}

static gboolean is_subtitle_ext(const gchar* ext) {
    static const gchar* const exts[] = {"srt", "ass", "ssa", "vtt"};
    for (gsize i = 0; i < G_N_ELEMENTS(exts); i++) {
        if (g_ascii_strcasecmp(ext, exts[i]) == 0) return TRUE;
    }
    return FALSE;
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const gchar* const*)a, *(const gchar* const*)b);
}

gchar** media_subtitle_files(const gchar* video_name, const gchar* const* names) {
    GPtrArray* exact = g_ptr_array_new();
    GPtrArray* others = g_ptr_array_new();
    const gchar* dot = video_name != NULL ? strrchr(video_name, '.') : NULL;
    gsize base_len = dot != NULL && dot != video_name ? (gsize)(dot - video_name) : (video_name ? strlen(video_name) : 0);
    for (gsize i = 0; base_len > 0 && names != NULL && names[i] != NULL; i++) {
        const gchar* n = names[i];
        if (strncmp(n, video_name, base_len) != 0 || n[base_len] != '.') continue;
        const gchar* rest = n + base_len + 1;
        const gchar* ext = strrchr(rest, '.');
        ext = ext != NULL ? ext + 1 : rest;
        if (!is_subtitle_ext(ext)) continue;
        g_ptr_array_add(ext == rest ? exact : others, g_strdup(n));
    }
    g_ptr_array_sort(exact, compare_names);
    g_ptr_array_sort(others, compare_names);
    for (guint i = 0; i < others->len; i++) g_ptr_array_add(exact, g_ptr_array_index(others, i));
    g_ptr_array_free(others, TRUE);
    g_ptr_array_add(exact, NULL);
    return (gchar**)g_ptr_array_free(exact, FALSE);
}

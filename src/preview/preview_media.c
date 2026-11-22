/* Media previews (see preview_media.h). */
#include "preview/preview_media.h"
#include "media/audio.h"
#include "media/image_preview.h"
#include "media/video.h"
#include "preview/image_prefetch.h"
#include "preview/pdf.h"

void preview_media_leave_players(AppData* app) {
    /* Leave the player page before stopping it (its controls must not be
     * redrawn unallocated) */
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    audio_preview_stop(app);
    video_preview_stop(app);
}

void preview_media_show(AppData* app, FileKind kind, const gchar* name, const gchar* full) {
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
    preview_media_leave_players(app);
    if (kind == FILE_KIND_PDF) {
        render_pdf_preview(app, name, full);
    } else {
        render_image_preview(app, name, full);
        image_prefetch_neighbours(app);
    }
}

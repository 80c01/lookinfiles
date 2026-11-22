#ifndef PREVIEW_MEDIA_H
#define PREVIEW_MEDIA_H

/* The previews of media files (images, PDF, video, audio) in the preview
 * pane, and leaving the players for a text preview. */
#include "core/app_types.h"
#include "core/file_types.h"

/* Preview the media file at full (kind: file_kind_is_media()); full may be
 * a remote file's local copy (preview_source_local()) */
void preview_media_show(AppData* app, FileKind kind, const gchar* name, const gchar* full);
/* Leave the player page and stop the players (before a text preview) */
void preview_media_leave_players(AppData* app);

#endif /* PREVIEW_MEDIA_H */

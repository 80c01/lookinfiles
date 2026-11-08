#ifndef AUDIO_H
#define AUDIO_H

#include "app_types.h"

/* Return TRUE if filename indicates an audio file */
gboolean is_audio_file(const gchar* name);

/* Initialize audio preview widgets inside app->preview_stack */
void audio_preview_init(AppData* app);

/* Render audio preview (or raw info / hex dump if render_raw is active) */
void render_audio_preview(AppData* app, const gchar* name, const gchar* full);

/* Stop playback and clear the active audio stream */
void audio_preview_stop(AppData* app);

/* Stop playback and free the player state (at exit) */
void audio_preview_free(AppData* app);

/* Pause audio playback if currently playing */
void audio_preview_pause(AppData* app);

/* Toggle play/pause state of the active audio stream */
void audio_preview_toggle_play(AppData* app);

#endif /* AUDIO_H */

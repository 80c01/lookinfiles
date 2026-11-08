#ifndef TEXT_CHUNKS_H
#define TEXT_CHUNKS_H

#include "core/app_types.h"

/* Large text files in the preview: the viewer shows the first PREVIEW_BYTES;
 * scrolling near the end loads the next chunk in a worker thread and appends
 * it, up to TEXT_CHUNKS_MAX_BYTES in all. Any new preview stops it. */

#define TEXT_CHUNKS_MAX_BYTES (8 * 1024 * 1024)

/* Watch the preview's scrolling (once, at startup) */
void text_chunks_attach(AppData* app);

/* The preview shows path up to byte offset (of size bytes): load the rest on
 * scroll. line_numbers: re-show the line numbers (first numbered line
 * first_line) after each chunk. */
void text_chunks_start(AppData* app, const gchar* path, gint64 offset, gint64 size, gint first_line,
                       gboolean line_numbers);

/* Stop loading chunks (a new preview) */
void text_chunks_stop(AppData* app);

#endif /* TEXT_CHUNKS_H */

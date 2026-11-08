#ifndef TRACK_MENU_H
#define TRACK_MENU_H

#include <gtk/gtk.h>

/* The "Audio: English" and "Subtitles: Off" buttons of a video player
 * (preview, Video tabs): each opens a popover listing the tracks of stream
 * (a media_player.h stream), the current one marked. A button shows only
 * when there is a choice: two audio tracks or more, one subtitle track or
 * more. key_hints: the tooltips name the keys of the Video tab (a, s). */
GtkWidget* track_menu_new(GtkMediaStream* stream, gboolean key_hints);

#endif /* TRACK_MENU_H */

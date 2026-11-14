#ifndef IMAGE_PREFETCH_H
#define IMAGE_PREFETCH_H

#include "core/app_types.h"

/* While an image is previewed, the images next to it in the explorer (the
 * two below, the one above) are decoded ahead, so moving to them with the
 * arrow keys shows them at once. Animated GIFs and archive entries are not. */
void image_prefetch_neighbours(AppData* app);

#endif /* IMAGE_PREFETCH_H */

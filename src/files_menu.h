#ifndef FILES_MENU_H
#define FILES_MENU_H

/* Right-click menu of the files view: open, open in the side panel (the
 * dedicated editor panel), open with the default application, rename,
 * move to the trash, copy the path, bookmark, open a terminal in a folder;
 * on an empty area, new file / folder and undo. Same actions as the keys. */
#include "app_types.h"

void files_menu_attach(AppData* app);

#endif /* FILES_MENU_H */

#ifndef EDITOR_REQUEST_H
#define EDITOR_REQUEST_H

/* Files opened in an editor tab at the request of modules that do not
 * depend on the editor, through the event bus (APP_EVENT_EDIT_PATH). */
#include "core/app_types.h"

/* APP_EVENT_EDIT_PATH: open app->edit_request_path (taken) in an editor tab,
 * or switch to its tab; a notice when it cannot be edited */
void editor_edit_requested(AppData* app);

#endif /* EDITOR_REQUEST_H */

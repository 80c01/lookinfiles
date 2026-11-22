#ifndef EDITOR_REMOTE_H
#define EDITOR_REMOTE_H

/* Opening the file of a remote machine (vfs_is_remote()) in an editor tab:
 * read over SFTP on a worker thread, the tab opened once the text is there
 * (from the sshfs mount when the SFTP session cannot serve it). */
#include "core/app_types.h"

void editor_remote_open(AppData* app, const gchar* name, const gchar* full);

#endif /* EDITOR_REMOTE_H */

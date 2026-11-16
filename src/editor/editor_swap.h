#ifndef EDITOR_SWAP_H
#define EDITOR_SWAP_H

/* Side of the dedicated editor panel: right of the preview (the default) or,
 * swapped, left of it. Each workspace keeps its side (VirtualLayout
 * editor_first, saved with layouts and the session); editor_frac stays the
 * preview's share either way. */
#include "core/app_types.h"
#include "editor/editor.h"

/* The editor panel is left of the preview in the current workspace */
gboolean editor_swap_active(AppData* app);

/* Put the preview and the editor panel in the order the current workspace
 * records (called when a workspace or a layout is shown) */
void editor_swap_place(AppData* app);

/* Swap the preview and the editor panel of the current workspace */
void editor_swap_toggle(AppData* app);

/* Direction from the editor panel towards the preview: a tab moved that way
 * past the last editor pane docks back into the preview notebook */
EditorMoveDirection editor_swap_preview_side(AppData* app);

#endif /* EDITOR_SWAP_H */

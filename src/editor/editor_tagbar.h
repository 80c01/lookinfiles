#ifndef EDITOR_TAGBAR_H
#define EDITOR_TAGBAR_H

/* The tagbar of an editor tab (docs/code-nav.md): a resizable pane beside
 * the view listing the symbols of a code file (grouped by kind or in file
 * order, nested in their container) or the headings of a Markdown file
 * (code_outline.h). It follows the cursor, refreshes after a pause in the
 * edits, and a click or Enter goes to the symbol (remembered in the code
 * navigation history). Shown in every code and Markdown tab or in none
 * (settings.ini: tagbar), with one width for all. */
#include "core/app_types.h"
#include "code_nav.h"

/* Put content (the view and its minimap) and the tagbar of tab side by
 * side; returns the GtkPaned to add to the tab's container */
GtkWidget* editor_tagbar_wrap(AppData* app, EditorTab* tab, GtkWidget* content);
/* Stop following tab->buffer (the tab is closing) */
void editor_tagbar_release(EditorTab* tab);
/* Show or hide the tagbar in every code and Markdown tab (not saved,
 * focus unchanged) */
void editor_tagbar_set_shown(AppData* app, gboolean shown);
/* Set the tagbar width (pixels, 0 = default) shared by the tabs and the
 * preview, and move the handles of the editor tabs to it (not saved) */
void editor_tagbar_set_width(AppData* app, gint width);
/* Show the tagbar in every code and Markdown tab, the focus in the one of
 * tab, or hide it everywhere, the focus back in the view; saved in
 * settings.ini. A message (through message) when tab has no outline. */
void editor_tagbar_toggle(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* EDITOR_TAGBAR_H */

#ifndef EDITOR_TAGBAR_H
#define EDITOR_TAGBAR_H

/* The tagbar of an editor tab (docs/code-nav.md): a resizable pane beside
 * the view listing the symbols of a code file (grouped by kind or in file
 * order, nested in their container) or the headings of a Markdown file
 * (code_outline.h). It follows the cursor, refreshes after a pause in the
 * edits, and a click or Enter goes to the symbol (remembered in the code
 * navigation history). Shown or hidden in each tab on its own; a new tab
 * follows Preferences → Editor (settings.ini: tagbar). One width for all. */
#include "core/app_types.h"
#include "code/code_nav.h"

/* Put content (the view and its minimap) and the tagbar of tab side by
 * side; returns the GtkPaned to add to the tab's container */
GtkWidget* editor_tagbar_wrap(AppData* app, EditorTab* tab, GtkWidget* content);
/* Stop following tab->buffer (the tab is closing) */
void editor_tagbar_release(EditorTab* tab);
/* Show or hide the tagbar in every code and Markdown tab, and in the tabs
 * opened next (not saved, focus unchanged) */
void editor_tagbar_set_shown(AppData* app, gboolean shown);
/* Set the tagbar width (pixels, 0 = default) shared by the tabs and the
 * preview, and move the handles of the editor tabs to it (not saved) */
void editor_tagbar_set_width(AppData* app, gint width);
/* Show the tagbar of tab, the focus in it, or hide it, the focus back in
 * the view; the other tabs and the tabs opened next keep the preference
 * (not saved). A message (through message) when tab has no outline. */
void editor_tagbar_toggle(AppData* app, EditorTab* tab, CodeNavMessageFunc message);
/* Whether tab has a tagbar to show (a code or Markdown file) */
gboolean editor_tagbar_available(EditorTab* tab);
/* Whether the tagbar of tab is shown */
gboolean editor_tagbar_shown(EditorTab* tab);
/* The list of the tagbar of tab, to focus (Alt+Left / Alt+Right), or NULL */
GtkWidget* editor_tagbar_list(EditorTab* tab);

/* The tagbar of every editor tab, shown or hidden, and the preference */
typedef struct EditorTagbarStates EditorTagbarStates;
EditorTagbarStates* editor_tagbar_states_take(AppData* app);
/* Put them back (tabs closed meanwhile are skipped; not saved) */
void editor_tagbar_states_restore(AppData* app, const EditorTagbarStates* states);
void editor_tagbar_states_free(EditorTagbarStates* states);

#endif /* EDITOR_TAGBAR_H */

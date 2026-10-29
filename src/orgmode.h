#ifndef ORGMODE_H
#define ORGMODE_H

#include "app_types.h"

/* Orgmode global fold states */
enum {
    ORG_GLOBAL_SHOW_ALL = 0, /* All headings and subtrees expanded */
    ORG_GLOBAL_OVERVIEW,     /* Top-level headings only (Level 1) */
    ORG_GLOBAL_CONTENTS      /* All headings visible (Levels 1-6), body text hidden */
};

/* Register tags for rich Orgmode rendering in GtkTextBuffer */
void ensure_orgmode_tags(GtkTextBuffer* buffer);

/* Render Orgmode document to GtkTextBuffer with interactive headings, TODOs, checkboxes, tables, code, and images */
void render_orgmode_to_buffer(GtkTextBuffer* buffer,
                              const gchar* org_text,
                              const gchar* doc_path,
                              gboolean is_preview,
                              const gchar* file_name,
                              gint64 total_size,
                              GHashTable* folded_lines,
                              gint global_fold);

/* Handle clicking on an Orgmode document in the preview viewer (cycles TODOs, folds/unfolds headings, toggles checkboxes, opens links) */
gboolean orgmode_handle_click(AppData* app, gdouble x, gdouble y);

/* Handle Org-mode key shortcuts in the viewer panel (Tab: fold heading, Shift+Tab: global fold cycle, t: cycle TODO, Space: toggle checkbox) */
gboolean orgmode_handle_viewer_key(AppData* app, guint keyval, GdkModifierType state);

/* Heading level of an Org line ("** Title" -> 2), 0 if not a heading */
gint orgmode_heading_level(const gchar* line);

/* Cycle the TODO keyword / toggle the checkbox on file_line of an Org file
 * on disk (same rules as the editor: #+TODO keywords, cookies, logdone) */
gboolean orgmode_cycle_todo_at_line(const gchar* file_path, gint file_line);
gboolean orgmode_toggle_checkbox_at_line(const gchar* file_path, gint file_line);

/* Cycle global folding state (Show All -> Overview -> Contents -> Show All) */
void orgmode_cycle_global_fold(AppData* app);

/* Helper to generate and install GtkSourceView 5 org.lang syntax spec for Dark+ theme */
void orgmode_install_gtksourceview_spec(void);

#endif /* ORGMODE_H */

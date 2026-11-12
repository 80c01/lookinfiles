#ifndef ORGMODE_H
#define ORGMODE_H

#include "core/app_types.h"
#include "markup/md_out.h"
#include "org/org_render.h"

/* Register tags for rich Orgmode rendering in GtkTextBuffer */
void ensure_orgmode_tags(GtkTextBuffer* buffer);

/* Show a parsed Org document (org_render_parse()) in buffer; line_maps: its
 * lines (kept by the buffer, for clicks and keys) */
void orgmode_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* out, GPtrArray* line_maps);

/* Render Orgmode document to GtkTextBuffer with interactive headings, TODOs, checkboxes, tables, code, and images */
/* view: where images fit their width (NULL: natural size) */
void render_orgmode_to_buffer(GtkTextBuffer* buffer,
                              GtkTextView* view,
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


/* Helper to generate and install GtkSourceView 5 org.lang syntax spec for Dark+ theme */
void orgmode_install_gtksourceview_spec(void);

#endif /* ORGMODE_H */

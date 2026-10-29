#ifndef LAYOUT_STORE_H
#define LAYOUT_STORE_H

/* Saved workspace layouts: named panel arrangements the user saves and picks
 * from a list (layouts.c). A layout holds panel visibility, split sizes, the
 * preview/terminal orientation, the dedicated editor panel and the terminal
 * pane tree; not the folder, view or open files. Applying one never changes
 * the default view of new workspaces.
 *
 * Pure data and file I/O (GKeyFile), no widgets: unit-tested in tests/. */
#include <glib.h>

typedef struct {
    gchar* name;
    gboolean files_vis;        /* explorer panel shown */
    gboolean preview_vis;      /* preview panel shown */
    gboolean term_vis;         /* terminal panel shown */
    gboolean editor_dedicated; /* editor tabs in the dedicated panel */
    gboolean stacked;          /* terminal below the preview (else beside it) */
    gdouble main_frac;         /* explorer width / window width */
    gdouble right_frac;        /* preview share of the preview/terminal pair */
    gdouble editor_frac;       /* preview share of the preview/editor pair */
    gchar* terminals;          /* terminal pane tree (see below), "" if none */
} SavedLayout;

SavedLayout* saved_layout_new(const gchar* name);
SavedLayout* saved_layout_copy(const SavedLayout* layout);
void saved_layout_free(gpointer layout);

/* Short description for the list, e.g. "Explorer · Preview · Terminal (2 panes)". Caller frees. */
gchar* saved_layout_summary(const SavedLayout* layout);

/* ~/.config/explorateur_fichiers/layouts.ini. Caller frees. */
gchar* layout_store_default_path(void);

/* Layouts in file order (GPtrArray of SavedLayout*, frees its items); empty
 * if the file is missing or unreadable */
GPtrArray* layout_store_load(const gchar* path);
gboolean layout_store_save(const gchar* path, GPtrArray* layouts, GError** error);

SavedLayout* layout_store_find(GPtrArray* layouts, const gchar* name);
/* Add a layout (taking ownership), replacing one with the same name in place */
void layout_store_put(GPtrArray* layouts, SavedLayout* layout);
gboolean layout_store_remove(GPtrArray* layouts, const gchar* name);

/* Terminal pane tree: "T" is one terminal; "H:<frac>(<a>,<b>)" splits side by
 * side and "V:<frac>(<a>,<b>)" one above the other, <frac> being the share of
 * the first child, e.g. "H:0.500(T,V:0.400(T,T))". */
typedef struct TermLayoutNode {
    gboolean is_terminal;
    gboolean vertical;
    gdouble frac;
    struct TermLayoutNode* first;
    struct TermLayoutNode* second;
} TermLayoutNode;

/* NULL if text is empty or invalid */
TermLayoutNode* term_layout_parse(const gchar* text);
gchar* term_layout_to_string(const TermLayoutNode* node);
guint term_layout_count(const TermLayoutNode* node);
void term_layout_free(TermLayoutNode* node);

#endif /* LAYOUT_STORE_H */

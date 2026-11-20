/* The notes searched with Ctrl+Shift+F (see notes_search.h) */
#include "notes/notes_search.h"
#include "notes/notes_store.h"
#include "notes/note_types.h"
#include "canvas/canvas_doc.h"
#include "canvas/canvas_view.h"
#include "editor/editor.h"

static gboolean describe_note(AppData* app, const gchar* path, gchar** title, gchar** detail, gchar** icon_name) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    if (!notes_store_find_path(app->notes_mgr, path, &nb, &note)) return FALSE;
    *title = g_strdup(note->title);
    *detail = g_strdup(nb->name);
    *icon_name = g_strdup(note->filename != NULL ? note->filename : path);
    return TRUE;
}

static gboolean open_note(AppData* app, const gchar* path, gint line, gint column) {
    NoteItem* note = NULL;
    if (!notes_store_find_path(app->notes_mgr, path, NULL, &note)) return FALSE;
    if (note->type == NOTE_TYPE_CANVAS) {
        canvas_view_open(app, note->path, note->title);
    } else {
        editor_open_at(app, path, line, column);
    }
    return TRUE;
}

static const SearchArea NOTES_AREA = {
    NOTES_STORE_TRASH "/, " NOTES_STORE_MANIFEST,
    CANVAS_PAGE_FILE,
    describe_note,
    open_note,
};

const SearchArea* notes_search_area(void) {
    return &NOTES_AREA;
}

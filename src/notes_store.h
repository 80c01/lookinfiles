#ifndef NOTES_STORE_H
#define NOTES_STORE_H

/* The notes on disk: notebooks (directories of a root directory), their
 * notes in user order, the trash, and the notes.xml manifest that keeps the
 * order, icons, titles and expansion. Pure data and file I/O (GMarkup), no
 * widgets: unit-tested in tests/. The Notes view of the explorer is notes.c.
 *
 *   <root>/notes.xml          manifest
 *   <root>/<Notebook>/<file>  a note (a Canvas note is a directory)
 *   <root>/.trash/<file>      a trashed note */
#include <glib.h>
#include "note_types.h"

#define NOTES_STORE_MANIFEST "notes.xml"
#define NOTES_STORE_TRASH ".trash"

typedef struct {
    gchar* filename;     /* e.g. "Meeting.txt", "Spec.md", "Tasks.org" */
    gchar* title;        /* e.g. "Meeting" */
    gchar* path;         /* full path on disk */
    NoteType type;       /* NOTE_TYPE_TEXT, NOTE_TYPE_MARKDOWN, NOTE_TYPE_ORG */
} NoteItem;

typedef struct {
    gchar* name;         /* e.g. "General", "Work" */
    gchar* icon;         /* glyph name from NOTEBOOK_ICONS (notebook_icons.h) */
    gchar* path;         /* full directory path on disk */
    gboolean expanded;
    GList* notes;        /* list of NoteItem* in custom user order */
} NotebookItem;

typedef struct {
    gchar* filename;     /* filename in .trash */
    gchar* title;        /* original title */
    gchar* path;         /* path in .trash */
    gchar* original_nb;  /* name of notebook it came from */
    gint64 trashed_at;   /* timestamp */
    NoteType type;       /* note type */
} TrashNoteItem;

typedef struct NotesManager {
    gchar* root;         /* root directory of the notebooks */
    GList* notebooks;    /* list of NotebookItem* in custom user order */
    GList* trash;        /* list of TrashNoteItem* */
    gboolean trash_expanded;
    gint sort_mode;      /* 0 = manual, 1 = A-Z, 2 = Z-A */
    gint next_note_id;   /* for Untitled-N */
} NotesManager;

/* Items own copies of their strings; the icon goes through notebook_icon_normalize() */
NoteItem* note_item_new(const gchar* filename, const gchar* title, const gchar* path, NoteType type);
void note_item_free(NoteItem* item);
NotebookItem* notebook_item_new(const gchar* name, const gchar* icon, const gchar* path);
void notebook_item_free(NotebookItem* nb);
TrashNoteItem* trash_item_new(const gchar* filename, const gchar* title, const gchar* path,
                              const gchar* original_nb, gint64 trashed_at, NoteType type);
void trash_item_free(TrashNoteItem* item);

/* The notes under root (created if missing): the manifest, if readable, gives
 * the order and the trash (entries whose file is gone are dropped); notebooks
 * and notes found on disk but not in it are appended. With no notebook at
 * all, a "General" notebook with a welcome note is created. Free with
 * notes_store_free(). */
NotesManager* notes_store_load(const gchar* root);

/* Write the manifest of mgr into its root */
gboolean notes_store_save(const NotesManager* mgr, GError** error);

/* <root>/.trash, created if missing. Caller frees. */
gchar* notes_store_trash_dir(const NotesManager* mgr);

void notes_store_free(NotesManager* mgr);

#endif /* NOTES_STORE_H */

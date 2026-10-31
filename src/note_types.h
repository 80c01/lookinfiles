#ifndef NOTE_TYPES_H
#define NOTE_TYPES_H

#include <glib.h>

/* Note types of the notes view: file extension, names, icons and the strings
 * stored in notes.xml. Pure helpers, no widgets. */
typedef enum {
    NOTE_TYPE_TEXT = 0,     /* Plain text note (.txt) */
    NOTE_TYPE_MARKDOWN = 1, /* Markdown note (.md: headings, lists, tables, images) */
    NOTE_TYPE_ORG = 2,      /* Emacs Org-mode note (.org: tasks, checkboxes, folding) */
    NOTE_TYPE_CANVAS = 3,   /* Canvas note (.canvas directory: text boxes, pictures; canvas_doc.h) */
    NOTE_TYPE_COUNT
} NoteType;

/* Type from a file name's extension (.org, .md / .markdown..., .canvas, else text) */
NoteType note_type_from_filename(const gchar* filename);

/* TRUE when the file name ends with an extension of a note type
 * (.txt, .md, .markdown, .mdown, .mkd, .org, .org_archive, .canvas) */
gboolean note_type_has_note_ext(const gchar* filename);

/* Stored name in notes.xml: "text", "markdown", "org", "canvas" */
const gchar* note_type_to_str(NoteType type);

/* Type from its stored name ("rich", the former name of Markdown, too);
 * unknown or NULL: from the file name */
NoteType note_type_from_str(const gchar* str, const gchar* filename);

/* Extension of a new note, dot included: ".txt", ".md", ".org", ".canvas" */
const gchar* note_type_ext(NoteType type);

/* Emoji shown before the note title */
const gchar* note_type_icon(NoteType type);

/* Display name: "Plain Text", "Markdown", "Org-mode", "Canvas" */
const gchar* note_type_name(NoteType type);

/* Short badge of the note row: "", "md", "org", "canvas" */
const gchar* note_type_badge(NoteType type);

/* Type from a badge text (unknown or empty: plain text) */
NoteType note_type_from_badge(const gchar* badge);

#endif /* NOTE_TYPES_H */

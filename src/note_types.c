#include "note_types.h"

#include <string.h>

typedef struct {
    const gchar* str;
    const gchar* ext;
    const gchar* icon;
    const gchar* name;
    const gchar* badge;
} NoteTypeInfo;

static const NoteTypeInfo TYPES[NOTE_TYPE_COUNT] = {
    [NOTE_TYPE_TEXT] = {"text", ".txt", "📄", "Plain Text", ""},
    [NOTE_TYPE_MARKDOWN] = {"markdown", ".md", "✨", "Markdown", "md"},
    [NOTE_TYPE_ORG] = {"org", ".org", "🦄", "Org-mode", "org"},
    [NOTE_TYPE_CANVAS] = {"canvas", ".canvas", "🎨", "Canvas", "canvas"},
};

static const NoteTypeInfo* info(NoteType type) {
    if ((gint)type < 0 || type >= NOTE_TYPE_COUNT) {
        type = NOTE_TYPE_TEXT;
    }
    return &TYPES[type];
}

/* Extension after the last dot of the base name, "" if none */
static const gchar* extension(const gchar* filename) {
    if (filename == NULL) return "";
    const gchar* base = strrchr(filename, '/');
    base = (base != NULL) ? base + 1 : filename;
    const gchar* dot = strrchr(base, '.');
    return (dot != NULL && dot != base) ? dot + 1 : "";
}

static gboolean ext_is(const gchar* ext, const gchar* const* list) {
    for (gsize i = 0; list[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, list[i]) == 0) return TRUE;
    }
    return FALSE;
}

static const gchar* const ORG_EXTS[] = {"org", "org_archive", NULL};
static const gchar* const MARKDOWN_EXTS[] = {"md", "markdown", "mdown", "mkd", NULL};

NoteType note_type_from_filename(const gchar* filename) {
    const gchar* ext = extension(filename);
    if (ext_is(ext, ORG_EXTS)) return NOTE_TYPE_ORG;
    if (ext_is(ext, MARKDOWN_EXTS)) return NOTE_TYPE_MARKDOWN;
    if (g_ascii_strcasecmp(ext, "canvas") == 0) return NOTE_TYPE_CANVAS;
    return NOTE_TYPE_TEXT;
}

gboolean note_type_has_note_ext(const gchar* filename) {
    const gchar* ext = extension(filename);
    return g_ascii_strcasecmp(ext, "txt") == 0 || g_ascii_strcasecmp(ext, "canvas") == 0 || ext_is(ext, ORG_EXTS) ||
           ext_is(ext, MARKDOWN_EXTS);
}

const gchar* note_type_to_str(NoteType type) {
    return info(type)->str;
}

NoteType note_type_from_str(const gchar* str, const gchar* filename) {
    if (str != NULL) {
        for (gint t = 0; t < NOTE_TYPE_COUNT; t++) {
            if (g_ascii_strcasecmp(str, TYPES[t].str) == 0) return (NoteType)t;
        }
        if (g_ascii_strcasecmp(str, "rich") == 0) return NOTE_TYPE_MARKDOWN;
    }
    return note_type_from_filename(filename);
}

const gchar* note_type_ext(NoteType type) {
    return info(type)->ext;
}

const gchar* note_type_icon(NoteType type) {
    return info(type)->icon;
}

const gchar* note_type_name(NoteType type) {
    return info(type)->name;
}

const gchar* note_type_badge(NoteType type) {
    return info(type)->badge;
}

NoteType note_type_from_badge(const gchar* badge) {
    if (badge != NULL && badge[0] != '\0') {
        for (gint t = 0; t < NOTE_TYPE_COUNT; t++) {
            if (g_strcmp0(badge, TYPES[t].badge) == 0) return (NoteType)t;
        }
    }
    return NOTE_TYPE_TEXT;
}

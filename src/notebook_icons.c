/* Notebook icons (see notebook_icons.h) */
#include "notebook_icons.h"

const gchar* const NOTEBOOK_ICONS[] = {
    "notebook", "folder-documents", "emoji-objects", "applications-science", "starred",
    "applications-engineering", "applications-graphics", "applications-games", "emoji-travel",
    "document-edit", "folder", "emoji-flags", "emoji-food", "system-lock-screen",
    "emoji-nature", "emoji-people", "emoji-activities", "emoji-symbols", "x-office-calendar",
    "folder-music", "folder-pictures", "weather-clear", NULL
};

/* Emoji presets of older versions -> icon */
static const struct {
    const gchar* emoji;
    const gchar* icon;
} LEGACY[] = {
    {"📓", "notebook"},           {"💼", "folder-documents"},
    {"💡", "emoji-objects"},      {"📚", "folder-documents"},
    {"🔬", "applications-science"}, {"🎯", "starred"},
    {"🛠", "applications-engineering"}, {"🎨", "applications-graphics"},
    {"⭐", "starred"},             {"🚀", "emoji-travel"},
    {"📝", "document-edit"},      {"📁", "folder"},
    {"🏷️", "emoji-flags"},        {"☕", "emoji-food"},
    {"🔒", "system-lock-screen"}, {"🌍", "emoji-nature"},
    {"❤️", "emoji-people"},       {"📅", "x-office-calendar"},
};

const gchar* notebook_icon_normalize(const gchar* stored) {
    if (stored == NULL) return NOTEBOOK_ICON_DEFAULT;
    for (gsize i = 0; NOTEBOOK_ICONS[i] != NULL; i++) {
        if (g_strcmp0(stored, NOTEBOOK_ICONS[i]) == 0) return NOTEBOOK_ICONS[i];
    }
    for (gsize i = 0; i < G_N_ELEMENTS(LEGACY); i++) {
        if (g_strcmp0(stored, LEGACY[i].emoji) == 0) return LEGACY[i].icon;
    }
    return NOTEBOOK_ICON_DEFAULT;
}

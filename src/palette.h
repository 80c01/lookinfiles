#ifndef PALETTE_H
#define PALETTE_H

/* The application's colours, by role: every widget, CSS rule, text tag,
 * markup span and drawing takes its colours from here, never from a
 * literal, so that a theme only has to change these values.
 *
 * - C code: palette_color() ("#rrggbb" for text tags and Pango markup),
 *   palette_rgb() for cairo.
 * - CSS: each role is a named colour (@bg_window, alpha(@fg, 0.5)...)
 *   defined by palette_css_defines() (theme_add_css() prepends them).
 * - Text templates (GtkSourceView scheme): palette_expand() replaces
 *   @role tokens.
 *
 * Default values: VS Code Dark Modern / Dark+. Pure GLib, unit-tested. */
#include <glib.h>

/* X(ENUM, "css_name", "#default", "description") */
#define PALETTE_ROLES(X)                                                                        \
    /* Surfaces */                                                                              \
    X(BG_WINDOW, "bg_window", "#191a1b", "Window, explorer and side panels")                    \
    X(BG_BAR, "bg_bar", "#18191a", "Top bar, tab strips, status bars")                          \
    X(BG_VIEW, "bg_view", "#121314", "Preview, editors and other content views")                \
    X(BG_TERMINAL, "bg_terminal", "#1f2021", "Terminal panes")                                  \
    X(BG_RAISED, "bg_raised", "#252526", "Dialogs, menus, tooltips and help cards")             \
    X(BG_INPUT, "bg_input", "#313131", "Text entries")                                          \
    X(BG_BLOCK, "bg_block", "#1c1d1f", "Code blocks, quotes and alternate table rows")          \
    X(BG_CODE_INLINE, "bg_code_inline", "#2a2b2d", "Inline code in documents")                  \
    X(BG_HOVER, "bg_hover", "#2a2d2e", "Row under the mouse")                                   \
    X(BG_SELECTED, "bg_selected", "#04395e", "Selected row in the focused list")                \
    X(BG_SELECTED_INACTIVE, "bg_selected_inactive", "#37373d", "Selected row elsewhere")        \
    X(HOVER_OVERLAY, "hover_overlay", "#5a5d5e", "Translucent hover of flat buttons")           \
    X(SHADOW, "shadow", "#000000", "Shadows and the dimmed layer behind dialogs")               \
    X(SCROLLBAR, "scrollbar", "#797979", "Scrollbar and minimap sliders (translucent)")         \
    /* Lines */                                                                                 \
    X(BORDER, "border", "#2b2b2b", "Panel dividers and bar borders")                            \
    X(BORDER_STRONG, "border_strong", "#454545", "Borders of dialogs, entries and lists")       \
    X(RULE, "rule", "#3a3b3d", "Rules and table grid lines in documents")                       \
    /* Text */                                                                                  \
    X(FG, "fg", "#cccccc", "Interface text")                                                    \
    X(FG_TEXT, "fg_text", "#d4d4d4", "File and document text")                                  \
    X(FG_STRONG, "fg_strong", "#e6e6e6", "Titles and headings")                                 \
    X(FG_BRIGHT, "fg_bright", "#ffffff", "Text on selected or active elements")                 \
    X(FG_MUTED, "fg_muted", "#9d9d9d", "Secondary text: inactive tabs, quotes")                 \
    X(FG_SUBTLE, "fg_subtle", "#858585", "Labels, metadata, line numbers")                      \
    X(FG_FAINT, "fg_faint", "#6e7681", "Counters, separators, the least important text")        \
    /* Accent and states */                                                                     \
    X(ACCENT, "accent", "#0078d4", "Accent: focus lines, primary buttons, branch")              \
    X(ACCENT_HOVER, "accent_hover", "#026ec1", "Accent under the mouse")                        \
    X(ACCENT_FG, "accent_fg", "#4fc1ff", "Accent text and active icons")                        \
    X(FOCUS_RING, "focus_ring", "#2b79d1", "Keyboard focus outline")                            \
    X(LINK, "link", "#3794ff", "Links and information")                                         \
    X(ERROR, "error", "#f14c4c", "Errors")                                                      \
    X(WARNING, "warning", "#cca700", "Warnings")                                                \
    X(SUCCESS, "success", "#89d185", "Success")                                                 \
    X(HIGHLIGHT, "highlight", "#e5c100", "Highlighted (marked) text background")                \
    /* Badges (Org keywords, Vim modes) */                                                      \
    X(BADGE_RED, "badge_red", "#c74e39", "Red badge: TODO, Replace mode")                       \
    X(BADGE_GREEN, "badge_green", "#388a34", "Green badge: DONE, Insert mode")                  \
    X(BADGE_BLUE, "badge_blue", "#0e639c", "Blue badge: Normal mode, other keywords")           \
    X(BADGE_ORANGE, "badge_orange", "#b8860b", "Orange badge: Visual mode, waiting")            \
    X(BADGE_FG, "badge_fg", "#ffffff", "Text on badges")                                        \
    /* Git */                                                                                   \
    X(GIT_MODIFIED, "git_modified", "#e2c08d", "Modified file")                                 \
    X(GIT_ADDED, "git_added", "#73c991", "Added or untracked file")                             \
    X(GIT_DELETED, "git_deleted", "#c74e39", "Deleted file")                                    \
    X(GIT_CONFLICT, "git_conflict", "#e51400", "File with conflicts")                           \
    X(GIT_RENAMED, "git_renamed", "#4fc1ff", "Renamed file")                                    \
    X(GUTTER_ADDED, "gutter_added", "#2ea043", "Change bar of added lines")                     \
    X(GUTTER_MODIFIED, "gutter_modified", "#0078d4", "Change bar of modified lines")            \
    X(GUTTER_DELETED, "gutter_deleted", "#f85149", "Mark of deleted lines")                     \
    X(DIFF_ADDED_FG, "diff_added_fg", "#b5d6a3", "Added line of a diff")                        \
    X(DIFF_ADDED_BG, "diff_added_bg", "#1b2b1e", "Background of an added line")                 \
    X(DIFF_REMOVED_FG, "diff_removed_fg", "#f0a8a8", "Removed line of a diff")                  \
    X(DIFF_REMOVED_BG, "diff_removed_bg", "#33191b", "Background of a removed line")            \
    X(DIFF_HUNK_FG, "diff_hunk_fg", "#4ec9b0", "Hunk header of a diff")                         \
    X(DIFF_HUNK_BG, "diff_hunk_bg", "#16222a", "Background of a hunk header")                   \
    /* Syntax */                                                                                \
    X(SYN_COMMENT, "syn_comment", "#6a9955", "Comments")                                        \
    X(SYN_KEYWORD, "syn_keyword", "#569cd6", "Keywords and constants")                          \
    X(SYN_CONTROL, "syn_control", "#c586c0", "Preprocessor and control keywords")               \
    X(SYN_STRING, "syn_string", "#ce9178", "Strings")                                           \
    X(SYN_NUMBER, "syn_number", "#b5cea8", "Numbers")                                           \
    X(SYN_TYPE, "syn_type", "#4ec9b0", "Types")                                                 \
    X(SYN_FUNCTION, "syn_function", "#dcdcaa", "Functions")                                     \
    X(SYN_VARIABLE, "syn_variable", "#9cdcfe", "Variables and keys")                            \
    X(SYN_ESCAPE, "syn_escape", "#d7ba7d", "Escapes, special characters, timestamps")           \
    X(SYN_CONSTANT, "syn_constant", "#4fc1ff", "Special constants")                             \
    X(SYN_ERROR, "syn_error", "#f44747", "Invalid code")                                        \
    /* Editors */                                                                               \
    X(EDITOR_SELECTION, "editor_selection", "#264f78", "Selected text")                         \
    X(EDITOR_CURRENT_LINE, "editor_current_line", "#1e2022", "Line of the cursor")              \
    X(EDITOR_CURSOR, "editor_cursor", "#aeafad", "Block cursor")                                \
    X(EDITOR_BRACKET, "editor_bracket", "#515553", "Matching bracket")                          \
    X(EDITOR_OCCURRENCE, "editor_occurrence", "#3a3d41", "Other occurrences of the word")       \
    X(EDITOR_MATCH, "editor_match", "#613214", "Search matches")                                \
    X(EDITOR_MATCH_CURRENT, "editor_match_current", "#9e6a03", "Current search match")         \
    /* Terminal */                                                                              \
    X(TERM_FG, "term_fg", "#cccccc", "Terminal text")                                           \
    X(TERM_CURSOR, "term_cursor", "#ffffff", "Terminal cursor")                                 \
    X(ANSI_BLACK, "ansi_black", "#000000", "ANSI black")                                        \
    X(ANSI_RED, "ansi_red", "#cd3131", "ANSI red")                                              \
    X(ANSI_GREEN, "ansi_green", "#0dbc79", "ANSI green")                                        \
    X(ANSI_YELLOW, "ansi_yellow", "#e5e510", "ANSI yellow")                                     \
    X(ANSI_BLUE, "ansi_blue", "#2472c8", "ANSI blue")                                           \
    X(ANSI_MAGENTA, "ansi_magenta", "#bc3fbc", "ANSI magenta")                                  \
    X(ANSI_CYAN, "ansi_cyan", "#11a8cd", "ANSI cyan")                                           \
    X(ANSI_WHITE, "ansi_white", "#e5e5e5", "ANSI white")                                        \
    X(ANSI_BRIGHT_BLACK, "ansi_bright_black", "#666666", "ANSI bright black")                   \
    X(ANSI_BRIGHT_RED, "ansi_bright_red", "#f14c4c", "ANSI bright red")                         \
    X(ANSI_BRIGHT_GREEN, "ansi_bright_green", "#23d18b", "ANSI bright green")                   \
    X(ANSI_BRIGHT_YELLOW, "ansi_bright_yellow", "#f5f543", "ANSI bright yellow")                \
    X(ANSI_BRIGHT_BLUE, "ansi_bright_blue", "#3b8eea", "ANSI bright blue")                      \
    X(ANSI_BRIGHT_MAGENTA, "ansi_bright_magenta", "#d670d6", "ANSI bright magenta")             \
    X(ANSI_BRIGHT_CYAN, "ansi_bright_cyan", "#29b8db", "ANSI bright cyan")                      \
    X(ANSI_BRIGHT_WHITE, "ansi_bright_white", "#e5e5e5", "ANSI bright white")

#define PALETTE_ENUM_(id, name, value, desc) PAL_##id,
typedef enum { PALETTE_ROLES(PALETTE_ENUM_) PAL_COUNT } PaletteRole;
#undef PALETTE_ENUM_

/* Current value of a role: "#rrggbb" (static, valid until the next
 * palette_set() / palette_reset() of that role) */
const gchar* palette_color(PaletteRole role);
/* Same colour as cairo components in 0..1 */
void palette_rgb(PaletteRole role, double* r, double* g, double* b);

/* Role metadata: CSS / theme file name ("bg_window"), default value and
 * a short description for a theme editor */
const gchar* palette_name(PaletteRole role);
const gchar* palette_default(PaletteRole role);
const gchar* palette_description(PaletteRole role);
/* Role of a name; FALSE if unknown */
gboolean palette_lookup(const gchar* name, PaletteRole* role);

/* Normalize "#rgb" / "#rrggbb" (any case) to "#rrggbb" in out; FALSE if
 * value is not a colour of that form */
gboolean palette_parse_color(const gchar* value, gchar out[8]);

/* Change a role (for themes): "#rgb" or "#rrggbb", any case. FALSE (and no
 * change) if the value is not a colour of that form. */
gboolean palette_set(PaletteRole role, const gchar* value);
/* Back to the default values */
void palette_reset(void);

/* "@define-color bg_window #191a1b;\n..." for every role */
gchar* palette_css_defines(void);
/* Copy of text with each "@name" of a role replaced by its value; other
 * '@' are kept */
gchar* palette_expand(const gchar* text);

#endif /* PALETTE_H */

# Translations

LookinFiles uses gettext. The source strings are English; each translation is
a catalog `po/<lang>.po` (`fr`, `de`, `pt_BR`...). Building needs `msgfmt`
(package `gettext`); without it the application builds and runs in English.

## Marking strings (`src/i18n.h`)

| Case | Write |
|------|-------|
| Text shown to the user | `_("Copy path")` |
| Entry of a static table | `N_("Name")` in the table, `_(entry)` where it is shown |
| Same English word, two meanings | `C_("sort order", "Name")` |
| A count | `g_strdup_printf(ngettext("%d file", "%d files", n), n)` |
| Note for translators | `/* TRANSLATORS: ... */` on the line above |
| Shortcut table (`help.c`) | `N_()` on sections and descriptions; keys stay as written, except mouse actions and words: `NC_("key", "Right-click")` |

Rules:

- Translate whole sentences with placeholders, never pieces glued together:
  `g_strdup_printf(_("Delete %s?"), name)`, not `"Delete " + name + "?"`.
  Use numbered placeholders (`%1$s`, `%2$d`) when a sentence has more than one,
  so a translation can reorder them.
- Never translate identifiers, config keys, file names, command-line
  options, key names (`Ctrl+Q`), event names or log messages meant for bug
  reports.
- In Pango markup, translate first and escape the translation:
  `g_markup_printf_escaped("<b>%s</b>", _("Name"))`.
- Leave room: German and French are about 30% longer than English.

## Commands

```bash
make pot          # po/lookinfiles.pot from the marked strings (xgettext)
make po-update    # merge the template into every po/<lang>.po (msgmerge)
make              # also compiles po/*.po into build/locale/
LC_ALL=fr_FR.UTF-8 ./lookinfiles   # run in French (the locale must be installed)
UI_LANGUAGE=fr make ui-shots       # UI scenario in French (default: English)
```

The template `po/lookinfiles.pot` is generated, not committed.

## Status

| Language | Catalog | Covered |
|----------|---------|---------|
| French | `po/fr.po` | command line, top bar, status bar, explorer context menu, workspace menu, layout switcher, new / rename / trash dialogs, Preferences, keyboard shortcuts panel (`help.c`), sidebar and its views (bookmarks, notes, remote, games list), editor tab menu and Notepad mode, find bar, Search in Files, References and Search tabs, Git tab (commit list, relative dates), preview core (sidebar row summaries, directory preview, images, PDF, audio, video, archive rows) |

Not marked yet: the user guide (docs/user-guide/, Help tab), the format cards of
the preview (`preview_formats` and the `*_info` / `*_view` modules). Left in English on purpose: file contents created for the user
(welcome note, note templates, the default "General" notebook), the game
description sheets and the games themselves, and the Emacs, Org-mode and Vim
minibuffer and echo messages (they mimic those editors, whose words the users
of these modes know), and what git itself prints in the Git tab (the commit
diff and its `--stat` summary, the detached HEAD name).

## Adding a language

1. `make pot`
2. `msginit -i po/lookinfiles.pot -o po/<lang>.po -l <lang>.UTF-8 --no-translator`
3. Translate the `msgstr` entries; `make` checks the catalog (`msgfmt --check`).

## Where the catalogs are found

`i18n_locale_dir()` takes the first existing directory among:
`$LOOKINFILES_LOCALEDIR`, `<exe dir>/build/locale` (source tree),
`<exe dir>/share/locale` (Windows portable folder),
`<exe dir>/../share/locale` (installed, AppImage), then `$(PREFIX)/share/locale`.
`make install` installs them in `$(PREFIX)/share/locale/<lang>/LC_MESSAGES/`.

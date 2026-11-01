# Themes and fonts

## Choosing a theme: `Alt+T`

`Alt+T` opens the **Preferences** dialog on its **Appearance** section (also
`Ctrl+,` or the gear button of the top bar).
Selecting a theme applies it at once to the whole window: panels, editors,
previews and terminals. **Apply** keeps it, **Cancel** goes back to the
previous one.

Three themes are built in:

- **Dark Modern** (the default);
- **Light Modern**;
- **High Contrast Dark**.

The `themes/` folder of the source tree has six more, one
per decade from the 1970s to the 2020s. Copy them into
`~/.config/lookinfiles/themes/` to use them.

## Fonts

The Appearance section also sets two fonts, used over any theme:

- the **interface font**: menus, panels and rendered documents;
- the **code font**: file contents, code and terminals.

Each is a list of font names separated by commas (the first one installed
is used) and a size.

| Key | Action |
|---|---|
| `Ctrl+=` | Larger fonts (interface and code together) |
| `Ctrl+-` | Smaller fonts |
| `Ctrl+0` | Back to the theme's sizes |

## Making your own theme

1. Press `Alt+T` and pick the theme closest to what you want.
2. Click **Save As…** and give it a name. LookinFiles writes a complete
   theme file, with a comment describing each colour.
3. Click **Edit**: the file opens in an editor tab.
4. Change the colours and save: the theme is applied again at once, or a
   message says what is wrong.

A theme can inherit from another one and set only what differs:

```ini
[Theme]
name=Solar Light
dark=false
inherits=Light Modern

[Colors]
bg_view=#fdf6e3
accent=#268bd2
```

The theme format and the list of colour names are in
[Configuration](../configuration.md#themes).

The chosen theme and your fonts are saved in
`~/.config/lookinfiles/theme.ini`.

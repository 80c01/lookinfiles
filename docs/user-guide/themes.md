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

Seven more are built in from the `themes/` folder of the source tree: one
per decade from the 1970s to the 2020s, and **Dark LightRed**. Copy one into
`~/.config/lookinfiles/themes/` to start your own from it.

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

### A theme from an image

**From Image…** builds a theme from the colours of a picture (a wallpaper,
a photo, a screenshot):

1. Select the image in the explorer, then press `Alt+T`.
2. Click **From Image…**: the path of the selected image is filled in
   (type another path if you want), and the name of the file is proposed
   as the name of the theme.
3. Click **Create**: the theme is saved with your themes and applied.

A mostly dark image gives a dark theme, a mostly light one a light theme.
The backgrounds take the main colour of the image, lightly tinted; the
accent is its most vivid colour; code colours use its other hues; errors
stay red and additions green, tuned toward the image. Text always keeps a
readable contrast. Use **Edit** afterwards to adjust any colour.

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

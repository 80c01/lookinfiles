# Reference themes

These themes are **built into LookinFiles** (bundled from this folder at build time). They are complete theme files
(every colour and both fonts, no `inherits`): one per decade of computing,
and a few more. They also serve as examples for writing your own theme.

| File | Theme | The era |
|---|---|---|
| `1970s-phosphor.ini` | 1970s Phosphor | Video terminals on a green phosphor CRT: one colour at a few intensities, reverse video |
| `1980s-text-mode.ini` | 1980s Text Mode | The PC in colour text mode: blue screens, cyan and yellow text, the 16-colour CGA palette |
| `1990s-desktop.ini` | 1990s Desktop | The grey 3D desktop: silver bevels, navy selections, keywords blue and comments green |
| `2000s-glossy.ini` | 2000s Glossy | Glossy desktops and Web 2.0: beige surfaces, saturated blue, a white terminal |
| `2010s-flat-dark.ini` | 2010s Flat Dark | Flat design and the dark editor: charcoal, vivid accents, Monokai syntax colours |
| `2020s-pastel.ini` | 2020s Pastel | Soft pastel dark themes: cool purple surfaces, pastel accents, a code font with ligatures |
| `dark-lightred.ini` | Dark LightRed | A dark grey theme with a light red accent |

Pick one with `Alt+T`. To start your own from one, copy it to your themes folder:

```bash
mkdir -p ~/.config/lookinfiles/themes
cp themes/1980s-text-mode.ini ~/.config/lookinfiles/themes/
```

The fonts list period fonts first (VT323, Px437 IBM VGA 8x16, MS Sans
Serif...) and fall back to common ones when they are not installed.

`make test` checks every file here: it parses, sets every colour and font,
and keeps its main text readable (the same contrast rules as the built-in
themes). When a palette role is added to `src/palette.h`, add it to each
file.

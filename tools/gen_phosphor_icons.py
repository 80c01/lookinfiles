#!/usr/bin/env python3
"""Generate the "phosphor" icon set (icons/sets/phosphor/): the pixel drawings
of the textmode set shown on a green P1 phosphor tube, i.e. in one hue at four
intensities (bright, normal, dim, faint) instead of the 16 CGA colours. Run
from the repository root:

    python3 tools/gen_phosphor_icons.py

It also rewrites the phosphor block of icons/lookin.gresource.xml.
Files are named lookin-set-<icon>[-symbolic].svg (see docs/configuration.md,
"Icon sets"). Symbolic icons stay in one ink colour, which GTK replaces by the
text colour; the others keep their own greens.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_textmode_icons as tm  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "icons", "sets", "phosphor")
MANIFEST = os.path.join(ROOT, "icons", "lookin.gresource.xml")

BRIGHT, NORMAL, DIM, FAINT, GLASS = "#99ff99", "#33e633", "#1c991c", "#0f5a0f", "#050b05"


def intensity(color):
    """Map a colour to the phosphor intensity of the same luminance."""
    if color == "#":
        return color
    r, g, b = (int(color[i:i + 2], 16) / 255 for i in (1, 3, 5))
    lum = 0.3 * r + 0.59 * g + 0.11 * b
    if lum < 0.04:
        return GLASS
    if lum > 0.8:
        return BRIGHT
    if lum > 0.5:
        return NORMAL
    if lum > 0.25:
        return DIM
    return FAINT


def manifest_block(names):
    lines = ['  <gresource prefix="/org/lookin/iconsets/phosphor">']
    for n in names:
        lines.append('    <file alias="scalable/actions/%s">sets/phosphor/%s</file>' % (n, n))
    lines.append("  </gresource>\n")
    return "\n".join(lines)


def update_manifest(names):
    with open(MANIFEST) as f:
        text = f.read()
    block = manifest_block(names)
    pat = re.compile(r'  <gresource prefix="/org/lookin/iconsets/phosphor">.*?</gresource>\n', re.S)
    if pat.search(text):
        text = pat.sub(lambda m: block, text)
    else:
        text = text.replace("</gresources>", block + "</gresources>")
    with open(MANIFEST, "w") as f:
        f.write(text)


def main():
    os.makedirs(OUT, exist_ok=True)
    names = []
    for name, canvas in sorted(tm.ICONS.items()):
        canvas.px = {p: intensity(c) for p, c in canvas.px.items()}
        with open(os.path.join(OUT, name + ".svg"), "w") as f:
            f.write(canvas.svg())
        names.append(name + ".svg")
    update_manifest(names)
    print("wrote %d icons to %s" % (len(tm.ICONS), os.path.normpath(OUT)))


if __name__ == "__main__":
    main()

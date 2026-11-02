#!/usr/bin/env python3
"""Generate the "textmode" icon set (icons/sets/textmode/): 16x16 icons drawn
like the characters of a 1980s PC in colour text mode (code page 437: double
lines, block and shade characters, arrows) in the 16 CGA colours. Run from
the repository root:

    python3 tools/gen_textmode_icons.py

It also rewrites the textmode block of icons/lookin.gresource.xml.
Files are named lookin-set-<icon>[-symbolic].svg (see docs/configuration.md,
"Icon sets"). Symbolic icons are drawn in one ink colour, which GTK replaces
by the text colour; the others keep their own colours.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gen_desktop_icons import Canvas  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "icons", "sets", "textmode")
MANIFEST = os.path.join(ROOT, "icons", "lookin.gresource.xml")

# CGA colours, used as pixel codes (any "#rrggbb" also works as a code)
PAL = {
    "B": "#0000aa",  # blue
    "b": "#5555ff",  # light blue
    "C": "#00aaaa",  # cyan
    "c": "#55ffff",  # light cyan
    "G": "#00aa00",  # green
    "g": "#55ff55",  # light green
    "R": "#aa0000",  # red
    "r": "#ff5555",  # light red
    "N": "#aa5500",  # brown
    "y": "#ffff55",  # yellow
    "A": "#aaaaaa",  # light grey
    "D": "#555555",  # dark grey
    "w": "#ffffff",  # white
    "k": "#000000",  # black
}

ICONS = {}  # file stem -> Canvas


def icon(name, symbolic=True):
    def deco(fn):
        c = Canvas()
        fn(c)
        c.px = {p: PAL.get(v, v) for p, v in c.px.items() if v != "."}  # "." erases
        ICONS["lookin-set-%s%s" % (name, "-symbolic" if symbolic else "")] = c
        return fn
    return deco


def dither(c, x0, y0, x1, y1, a, b):
    """Shade character: a checkerboard of two colours (like the light shade)."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            c.set(x, y, a if (x + y) % 2 == 0 else b)


def double_frame(c, x0, y0, x1, y1, col="#"):
    """Double-line box (the CP437 box-drawing characters)."""
    c.frame(x0, y0, x1, y1, col)
    c.frame(x0 + 2, y0 + 2, x1 - 2, y1 - 2, col)


# ---- Symbolic: toolbar and buttons -------------------------------------------

@icon("window-close")
def _(c):
    c.rect(0, 2, 1, 13)
    c.rect(0, 2, 3, 3)
    c.rect(0, 12, 3, 13)
    c.rect(14, 2, 15, 13)
    c.rect(12, 2, 15, 3)
    c.rect(12, 12, 15, 13)
    for d in (0, 1):
        c.line(4 + d, 4, 10, 11 - d)
        c.line(11 - d, 4, 5, 11 - d)


@icon("list-add")
def _(c):
    c.rect(6, 2, 9, 13)
    c.rect(2, 6, 13, 9)


@icon("pan-up")
def _(c):
    c.art(3, 4, ["...##...", "..####..", ".######.", "########"])


@icon("pan-down")
def _(c):
    c.art(4, 6, ["########", ".######.", "..####..", "...##..."])


@icon("go-next")
def _(c):
    c.art(2, 3, ["......##....", "......####..", "......######", "############",
                 "############", "......######", "......####..", "......##...."])


@icon("go-previous")
def _(c):
    c.art(2, 3, ["....##......", "..####......", "######......", "############",
                 "############", "######......", "..####......", "....##......"])


@icon("go-home")
def _(c):
    c.art(1, 2, ["......##......", "....######....", "..##########..", ".############.",
                 "##############", ".############.", ".####......###", ".####..##..###",
                 ".####..##..###", ".####..##..###"])
    c.rect(2, 10, 3, 11, ".")


@icon("edit-delete")
def _(c):
    c.rect(6, 1, 9, 2)
    c.rect(2, 3, 13, 4)
    for x in (3, 7, 11):
        c.rect(x, 6, x + 1, 13)
    c.rect(3, 13, 12, 14)
    c.rect(2, 6, 2, 6, ".")


@icon("edit-find")
def _(c):
    c.art(1, 1, ["..#####.....", ".##...##....", "##.....##...", "##.....##...",
                 "##.....##...", "##.....##...", ".##...##....", "..#####.....",
                 "......###...", ".......###..", "........###.", ".........###"])


@icon("view-refresh")
def _(c):
    c.art(2, 2, ["...######...", "..##....##..", ".##......##.", ".##....#####",
                 ".##.....###.", ".##......#..", ".##.........", ".##......##.",
                 "..##....##..", "...######..."])
    c.rect(10, 6, 11, 6, "#")


@icon("view-reveal")
def _(c):
    c.art(0, 3, ["....########....", "..##........##..", ".#....####....#.",
                 "#....######....#", "#....######....#", ".#....####....#.",
                 "..##........##..", "....########...."])


@icon("view-dual")
def _(c):
    double_frame(c, 1, 2, 14, 13)
    c.rect(7, 2, 8, 13)


@icon("view-grid")
def _(c):
    for x, y in ((1, 1), (9, 1), (1, 9), (9, 9)):
        c.rect(x, y, x + 5, y + 5)


@icon("view-paged")
def _(c):
    double_frame(c, 2, 1, 13, 14)
    for y in (5, 8, 11):
        c.rect(5, y, 10, y)
    c.rect(5, 5, 5, 5, ".")


@icon("document-new")
def _(c):
    c.rect(2, 0, 9, 1)
    c.rect(2, 0, 3, 15)
    c.rect(2, 14, 13, 15)
    c.rect(12, 4, 13, 15)
    c.rect(9, 0, 10, 3)
    c.rect(9, 2, 13, 3)
    c.rect(11, 3, 12, 4)
    c.rect(7, 6, 8, 12)
    c.rect(5, 8, 10, 9)


@icon("document-edit")
def _(c):
    c.art(2, 2, ["..........##", ".........####", "........####.", ".......####..",
                 "......####...", ".....####....", "....####.....", "...####......",
                 "..####.......", ".####........", "####........."])
    c.rect(1, 13, 13, 14)


@icon("document-print-preview")
def _(c):
    double_frame(c, 2, 1, 13, 14)
    c.rect(6, 5, 9, 5)
    c.rect(5, 6, 5, 9)
    c.rect(10, 6, 10, 9)
    c.rect(6, 10, 9, 10)
    c.rect(10, 10, 12, 12)


@icon("document-open-recent")
def _(c):
    c.art(2, 1, ["...######...", "..##....##..", ".##......##.", "##...##....#",
                 "##...##....#", "##...#####.#", "##.........#", ".##.......##",
                 "..##.....##.", "...#######.."])


@icon("user-bookmarks")
def _(c):
    c.rect(3, 1, 12, 13)
    c.rect(3, 12, 12, 14, ".")
    c.art(3, 12, ["####..####", "###....###"])
    c.rect(3, 12, 4, 14, "#")
    c.rect(11, 12, 12, 14, "#")
    c.rect(3, 12, 12, 12, "#")
    c.rect(7, 12, 8, 12, ".")
    c.rect(7, 13, 8, 14, ".")
    c.rect(5, 13, 6, 13, "#")
    c.rect(9, 13, 10, 13, "#")
    c.rect(3, 14, 4, 14, "#")
    c.rect(11, 14, 12, 14, "#")


@icon("bookmark-new")
def _(c):
    c.rect(3, 1, 12, 14)
    c.rect(3, 12, 12, 14, ".")
    c.art(3, 12, ["###....###", "##......##", "#........#"])
    c.rect(7, 4, 8, 9, ".")
    c.rect(5, 6, 10, 7, ".")


@icon("input-keyboard")
def _(c):
    double_frame(c, 0, 3, 15, 13)
    for x in (4, 7, 10):
        c.rect(x, 6, x + 1, 6)
    c.rect(5, 9, 10, 9)


@icon("utilities-terminal")
def _(c):
    c.frame(0, 1, 15, 14)
    c.rect(0, 1, 15, 3)
    c.art(2, 6, ["##....", "####..", "..####", "####..", "##...."])
    c.rect(8, 11, 12, 12)


@icon("network-transmit-receive")
def _(c):
    c.art(1, 1, ["...##.....", "..####....", ".######...", "########..", "...##.....",
                 "...##.....", "...##.....", "...##....."])
    c.art(7, 7, ["....##..", "....##..", "....##..", "....##..", "########", ".######.",
                 "..####..", "...##..."])


@icon("process-stop")
def _(c):
    c.rect(2, 2, 13, 13)


@icon("applications-system")
def _(c):
    c.art(1, 1, ["......##......", "..##..##..##..", ".####.##.####.", "..##########..",
                 "..###....###..", "####......####", "####......####", "..###....###..",
                 "..##########..", ".####.##.####.", "..##..##..##..", "......##......"])


@icon("folder-new")
def _(c):
    c.rect(1, 2, 6, 3)
    c.rect(1, 2, 2, 14)
    c.rect(1, 13, 14, 14)
    c.rect(13, 5, 14, 14)
    c.rect(6, 4, 14, 5)
    c.rect(7, 7, 8, 12)
    c.rect(5, 9, 10, 10)


# ---- Coloured: folders, files, devices ---------------------------------------

def folder(c):
    """The folder: a yellow block with a tab, a brown shade under it."""
    c.rect(1, 2, 6, 3, "y")
    c.rect(1, 4, 14, 12, "y")
    c.rect(7, 3, 7, 3, "y")
    c.rect(2, 5, 13, 5, "w")
    c.rect(1, 13, 14, 14, "N")
    c.rect(14, 5, 14, 12, "N")


for _n in ("folder", "ft-folder"):
    icon(_n, symbolic=False)(folder)


@icon("ft-notebook", symbolic=False)
def _(c):
    folder(c)
    for y in (7, 9, 11):
        c.rect(4, y, 11, y, "b")


def page(c, lines="c"):
    """A sheet in a white frame with a folded corner, its text as shade characters."""
    c.frame(2, 0, 13, 15, "w")
    c.rect(10, 0, 13, 3, ".")
    c.rect(10, 0, 10, 3, "w")
    c.rect(10, 3, 13, 3, "w")
    c.rect(11, 1, 12, 2, "A")
    c.rect(13, 3, 13, 3, "w")
    for y in (5, 7, 9, 11, 13):
        for x in range(4, 12):
            if (x + y) % 2 == 0:
                c.set(x, y, lines)


for _n in ("ft-default", "text-x-generic"):
    icon(_n, symbolic=False)(page)


@icon("ft-image", symbolic=False)
def _(c):
    c.frame(0, 2, 15, 13, "w")
    c.frame(2, 4, 13, 11, "w")
    dither(c, 3, 5, 12, 7, "c", "b")
    dither(c, 3, 8, 12, 10, "g", "G")
    c.rect(10, 5, 11, 6, "y")


@icon("user-trash", symbolic=False)
def _(c):
    c.rect(5, 0, 10, 1, "A")
    c.rect(1, 2, 14, 3, "w")
    c.rect(2, 4, 13, 14, "A")
    c.frame(2, 4, 13, 14, "D")
    for x in (5, 7, 9, 11):
        c.rect(x, 6, x, 12, "D")
    c.rect(2, 4, 13, 4, "w")


@icon("network-server", symbolic=False)
def _(c):
    for y0 in (1, 8):
        c.rect(1, y0, 14, y0 + 5, "A")
        c.frame(1, y0, 14, y0 + 5, "w")
        c.rect(3, y0 + 2, 8, y0 + 3, "D")
        c.rect(11, y0 + 2, 12, y0 + 3, "g")
    c.rect(6, 14, 9, 15, "D")


@icon("drive-harddisk", symbolic=False)
def _(c):
    c.rect(0, 4, 15, 12, "A")
    c.frame(0, 4, 15, 12, "w")
    c.rect(1, 12, 15, 12, "D")
    c.rect(15, 5, 15, 12, "D")
    c.rect(2, 8, 9, 9, "D")
    c.rect(12, 8, 13, 9, "g")


def box(c, edge, fill):
    """A dialog box: a filled cell block in a double-line frame."""
    c.rect(0, 0, 15, 15, fill)
    c.frame(0, 0, 15, 15, edge)
    c.frame(2, 2, 13, 13, edge)


@icon("dialog-information", symbolic=False)
def _(c):
    box(c, "c", "B")
    c.rect(7, 4, 8, 5, "w")
    c.rect(6, 7, 8, 7, "w")
    c.rect(7, 7, 8, 11, "w")
    c.rect(5, 11, 10, 11, "w")


@icon("dialog-question", symbolic=False)
def _(c):
    box(c, "c", "B")
    c.art(4, 3, ["..####..", ".##..##.", ".##..##.", ".....##.", "....##..", "...##...",
                 "...##...", "........", "...##...", "...##..."], ink="y")


@icon("dialog-warning", symbolic=False)
def _(c):
    for y in range(0, 15):
        half = y * 0.5 + 0.5
        for x in range(16):
            if abs(x - 7.5) <= half:
                c.set(x, y, "y")
    c.rect(0, 14, 15, 15, "y")
    c.rect(7, 4, 8, 9, "k")
    c.rect(7, 11, 8, 12, "k")


@icon("dialog-error", symbolic=False)
def _(c):
    box(c, "r", "R")
    for d in (0, 1):
        c.line(4 + d, 4, 11, 11 - d, "w")
        c.line(11 - d, 4, 4, 11 - d, "w")


# ---- Output -------------------------------------------------------------------

def manifest_block(names):
    lines = ['  <gresource prefix="/org/lookin/iconsets/textmode">']
    for n in names:
        lines.append('    <file alias="scalable/actions/%s">sets/textmode/%s</file>' % (n, n))
    lines.append("  </gresource>\n")
    return "\n".join(lines)


def update_manifest(names):
    with open(MANIFEST) as f:
        text = f.read()
    block = manifest_block(names)
    pat = re.compile(r'  <gresource prefix="/org/lookin/iconsets/textmode">.*?</gresource>\n', re.S)
    if pat.search(text):
        text = pat.sub(lambda m: block, text)
    else:
        text = text.replace("</gresources>", block + "</gresources>")
    with open(MANIFEST, "w") as f:
        f.write(text)


def main():
    os.makedirs(OUT, exist_ok=True)
    names = []
    for name, canvas in sorted(ICONS.items()):
        with open(os.path.join(OUT, name + ".svg"), "w") as f:
            f.write(canvas.svg())
        names.append(name + ".svg")
    update_manifest(names)
    print("wrote %d icons to %s" % (len(ICONS), os.path.normpath(OUT)))


if __name__ == "__main__":
    main()

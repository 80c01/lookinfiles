#!/usr/bin/env python3
"""Generate the "desktop" icon set (icons/sets/desktop/): 16x16 pixel icons in
the style of a 1990s grey desktop. Run from the repository root:

    python3 tools/gen_desktop_icons.py

Files are named lookin-set-<icon>[-symbolic].svg (see docs/configuration.md,
"Icon sets"). Symbolic icons are drawn in one ink colour, which GTK replaces
by the text colour; the others keep their own colours.
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "icons", "sets", "desktop")

COLORS = {
    "k": "#000000",  # outline
    "w": "#ffffff",  # white
    "g": "#c0c0c0",  # silver
    "d": "#808080",  # dark grey
    "y": "#ffff80",  # folder
    "o": "#c0a000",  # folder shade
    "b": "#000080",  # navy
    "l": "#0000ff",  # blue
    "c": "#00c0c0",  # cyan
    "r": "#ff0000",
    "R": "#800000",
    "n": "#00a000",  # green
    "e": "#ffff00",  # yellow
    "#": "#2e3436",  # symbolic ink
}


class Canvas:
    def __init__(self):
        self.px = {}

    def set(self, x, y, c="#"):
        if 0 <= x < 16 and 0 <= y < 16:
            self.px[(x, y)] = c

    def rect(self, x0, y0, x1, y1, c="#"):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def frame(self, x0, y0, x1, y1, c="#"):
        self.rect(x0, y0, x1, y0, c)
        self.rect(x0, y1, x1, y1, c)
        self.rect(x0, y0, x0, y1, c)
        self.rect(x1, y0, x1, y1, c)

    def line(self, x0, y0, x1, y1, c="#"):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def art(self, x, y, rows, ink="#"):
        """Rows of text at (x, y): '.' transparent, '#' ink, a letter of COLORS."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch != ".":
                    self.set(x + i, y + j, ink if ch == "#" else ch)

    def svg(self):
        by_color = {}
        for (x, y), c in sorted(self.px.items(), key=lambda p: (p[0][1], p[0][0])):
            by_color.setdefault(c, []).append((x, y))
        paths = []
        for c, pts in by_color.items():
            d = []
            rows = {}
            for x, y in pts:
                rows.setdefault(y, []).append(x)
            for y, xs in sorted(rows.items()):
                xs.sort()
                start = prev = xs[0]
                for x in xs[1:] + [None]:
                    if x is not None and x == prev + 1:
                        prev = x
                        continue
                    d.append("M%d %dh%dv1h-%dz" % (start, y, prev - start + 1, prev - start + 1))
                    if x is not None:
                        start = prev = x
            fill = COLORS.get(c, c)
            paths.append('<path fill="%s" d="%s"/>' % (fill, "".join(d)))
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" '
                'viewBox="0 0 16 16" shape-rendering="crispEdges">%s</svg>\n' % "".join(paths))


ICONS = {}  # file stem -> Canvas


def icon(name, symbolic=True):
    def deco(fn):
        c = Canvas()
        fn(c)
        ICONS["lookin-set-%s%s" % (name, "-symbolic" if symbolic else "")] = c
        return fn
    return deco


# ---- Symbolic: toolbar and buttons -------------------------------------------

@icon("window-close")
def _(c):
    for d in (0, 1):
        c.line(3 + d, 3, 12, 12 - d)
        c.line(12 - d, 3, 3, 12 - d)
    for x, y in ((3, 3), (11, 3), (3, 12), (11, 12)):
        c.rect(x, y, x + 1, y)


@icon("list-add")
def _(c):
    c.rect(7, 3, 8, 12)
    c.rect(3, 7, 12, 8)


@icon("pan-up")
def _(c):
    c.art(4, 5, ["...##...", "..####..", ".######.", "########"])


@icon("pan-down")
def _(c):
    c.art(4, 6, ["########", ".######.", "..####..", "...##..."])


@icon("go-next")
def _(c):
    c.art(3, 3, ["......#.....", "......##....", "......###...", "############",
                 "############", "......###...", "......##....", "......#....."])


@icon("go-previous")
def _(c):
    c.art(1, 3, [".....#......", "....##......", "...###......", "############",
                 "############", "...###......", "....##......", ".....#......"])


@icon("go-home")
def _(c):
    c.art(1, 2, ["......##......", ".....####.....", "....######....", "...########...",
                 "..##########..", ".############.", "..##......##..", "..##..##..##..",
                 "..##..##..##..", "..##..##..##.."])


@icon("edit-delete")
def _(c):
    c.art(3, 2, ["....####....", "############", "............", ".##########.",
                 "..#.#..#.#..", "..#.#..#.#..", "..#.#..#.#..", "..#.#..#.#..",
                 "..#.#..#.#..", "..#.#..#.#..", "..########.."])
    c.rect(3, 3, 14, 3)


@icon("edit-find")
def _(c):
    c.art(2, 2, ["..####....", ".#....#...", "#......#..", "#......#..", "#......#..",
                 "#......#..", ".#....#...", "..####....", "......##..", ".......##."])
    c.rect(9, 9, 10, 10)
    c.rect(11, 11, 13, 13)


@icon("view-refresh")
def _(c):
    c.art(2, 2, ["...######...", "..#......#..", ".#........#.", ".#.....#####", ".#......###.",
                 ".#.......#..", ".#..........", ".#........#.", "..#......#..", "...######..."])
    c.rect(11, 6, 14, 6)


@icon("view-reveal")
def _(c):
    c.art(1, 4, ["....######....", "..##......##..", ".#...####...#.", "#...##..##...#",
                 "#...##..##...#", ".#...####...#.", "..##......##..", "....######...."])


@icon("view-dual")
def _(c):
    c.frame(2, 3, 13, 12)
    c.rect(7, 3, 8, 12)


@icon("view-grid")
def _(c):
    for x, y in ((2, 2), (9, 2), (2, 9), (9, 9)):
        c.rect(x, y, x + 4, y + 4)


@icon("view-paged")
def _(c):
    c.frame(3, 2, 12, 13)
    for y in (5, 7, 9, 11):
        c.rect(5, y, 10, y)


@icon("document-new")
def _(c):
    c.art(2, 1, ["#######....", "#.....##...", "#.....#.#..", "#.....####.",
                 "#.........#", "#.........#", "#.........#", "#.........#",
                 "#.........#", "#.........#", "#.........#", "#.........#", "###########"])


@icon("document-edit")
def _(c):
    c.art(2, 2, ["..........##", ".........####", "........##..#", ".......##..#.",
                 "......##..#..", ".....##..#...", "....##..#....", "...##..#.....",
                 "..##..#......", ".####........", ".###........."])
    c.rect(1, 13, 12, 13)


@icon("document-print-preview")
def _(c):
    c.art(2, 1, ["#######....", "#.....##...", "#.....#.#..", "#.....####.",
                 "#.........#", "#..###....#", "#.#...#...#", "#.#...#...#",
                 "#..###....#", "#....##...#", "#.....##..#", "#.........#", "###########"])


@icon("document-open-recent")
def _(c):
    c.art(2, 2, ["...######...", "..#......#..", ".#...#....#.", "#....#.....#", "#....#.....#",
                 "#....#####.#", "#..........#", "#..........#", ".#........#.", "..#......#..", "...######..."])


@icon("user-bookmarks")
def _(c):
    c.art(4, 2, ["########", "#......#", "#......#", "#......#", "#......#", "#......#",
                 "#......#", "#..##..#", "#.#..#.#", "##....##"])


@icon("bookmark-new")
def _(c):
    c.art(3, 2, ["########", "########", "########", "########", "########", "########",
                 "########", "###..###", "##....##", "#......#"])


@icon("input-keyboard")
def _(c):
    c.frame(1, 4, 14, 12)
    for x in (3, 5, 7, 9, 11):
        c.set(x, 6)
        c.set(x, 8)
    c.rect(4, 10, 11, 10)


@icon("utilities-terminal")
def _(c):
    c.frame(1, 2, 14, 13)
    c.rect(1, 2, 14, 4)
    c.art(3, 6, ["#.....", ".#....", "..#...", ".#....", "#....."])
    c.rect(7, 10, 10, 10)


@icon("network-transmit-receive")
def _(c):
    c.art(2, 2, ["...##.....", "..####....", ".######...", "...##.....", "...##.....",
                 "...##.....", "...##....."])
    c.art(8, 7, ["....##..", "....##..", "....##..", "....##..", ".######.", "..####..", "...##..."])


@icon("process-stop")
def _(c):
    c.rect(3, 3, 12, 12)


@icon("applications-system")
def _(c):
    c.art(1, 1, ["......##......", "..#...##...#..", ".###.####.###.", "..##########..",
                 "..###....###..", "###........###", "###........###", "..###....###..",
                 "..##########..", ".###.####.###.", "..#...##...#..", "......##......"])


@icon("folder-new")
def _(c):
    c.art(1, 3, [".#####........", "#.....#######.", "#............#", "#............#",
                 "#............#", "#............#", "#............#", "#............#",
                 "#............#", "##############"])
    c.rect(7, 6, 8, 11)
    c.rect(5, 8, 10, 9)


# ---- Coloured: folders, files, devices ---------------------------------------

FOLDER = [
    ".kkkkk..........",
    "kyyyyyk.........",
    "kyyyyyykkkkkkkk.",
    "kywwwwyyyyyyyyyk",
    "kywyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    "kyyyyyyyyyyyyyok",
    ".kooooooooooooo.",
]


def folder(c):
    c.art(0, 2, [r[:16].ljust(16, ".") for r in FOLDER])
    c.rect(1, 14, 14, 14, "k")
    c.rect(15, 5, 15, 13, "k")


for _n in ("folder", "ft-folder"):
    icon(_n, symbolic=False)(folder)


@icon("ft-notebook", symbolic=False)
def _(c):
    folder(c)
    for y in (6, 8, 10):
        c.rect(4, y, 11, y, "l")


PAGE = [
    "kkkkkkkkk.......",
    "kwwwwwwwkk......",
    "kwwwwwwwkwk.....",
    "kwwwwwwwkwwk....",
    "kwwwwwwwkkkkk...",
    "kwwwwwwwwwwwk...",
]


def page(c, lines=True):
    c.rect(2, 1, 12, 14, "w")
    c.frame(2, 1, 12, 14, "k")
    c.rect(9, 1, 12, 4, "g")
    c.line(9, 1, 12, 4, "k")
    c.rect(9, 2, 9, 4, "k")
    c.rect(9, 4, 12, 4, "k")
    c.set(9, 1, "k")
    if lines:
        for y in (6, 8, 10, 12):
            c.rect(4, y, 10, y, "d")


for _n in ("ft-default", "text-x-generic"):
    icon(_n, symbolic=False)(page)


@icon("ft-image", symbolic=False)
def _(c):
    c.rect(1, 3, 14, 12, "w")
    c.frame(1, 3, 14, 12, "k")
    c.rect(2, 4, 13, 8, "c")
    c.rect(2, 9, 13, 11, "n")
    c.rect(10, 5, 11, 6, "e")


@icon("user-trash", symbolic=False)
def _(c):
    c.rect(3, 4, 12, 14, "g")
    c.frame(3, 4, 12, 14, "k")
    c.rect(2, 3, 13, 3, "k")
    c.rect(6, 2, 9, 2, "k")
    for x in (5, 7, 9, 11):
        c.rect(x, 6, x, 12, "d")


@icon("network-server", symbolic=False)
def _(c):
    c.rect(2, 2, 13, 6, "g")
    c.frame(2, 2, 13, 6, "k")
    c.rect(2, 8, 13, 12, "g")
    c.frame(2, 8, 13, 12, "k")
    c.rect(4, 4, 8, 4, "d")
    c.rect(4, 10, 8, 10, "d")
    c.set(11, 4, "n")
    c.set(11, 10, "n")
    c.rect(6, 13, 9, 14, "k")


@icon("drive-harddisk", symbolic=False)
def _(c):
    c.rect(1, 6, 14, 12, "g")
    c.frame(1, 6, 14, 12, "k")
    c.rect(3, 10, 8, 10, "d")
    c.set(12, 10, "n")
    c.rect(2, 7, 13, 7, "w")


@icon("dialog-information", symbolic=False)
def _(c):
    for y in range(16):
        for x in range(16):
            if (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 7.3 ** 2:
                c.set(x, y, "l")
            if 6.2 ** 2 < (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 7.3 ** 2:
                c.set(x, y, "b")
    c.rect(7, 4, 8, 5, "w")
    c.rect(7, 7, 8, 12, "w")


@icon("dialog-question", symbolic=False)
def _(c):
    for y in range(16):
        for x in range(16):
            d = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if d <= 7.3 ** 2:
                c.set(x, y, "b" if d > 6.2 ** 2 else "l")
    c.art(4, 3, ["..####..", ".##..##.", ".##..##.", "....##..", "...##...", "...##...", "........",
                 "...##...", "...##..."], ink="w")


@icon("dialog-warning", symbolic=False)
def _(c):
    for y in range(1, 15):
        half = (y - 1) * 0.55 + 0.5
        for x in range(16):
            if abs(x - 7.5) <= half:
                c.set(x, y, "e")
                if abs(x - 7.5) > half - 1.1 or y == 14:
                    c.set(x, y, "k")
    c.rect(7, 5, 8, 9, "k")
    c.rect(7, 11, 8, 12, "k")


@icon("dialog-error", symbolic=False)
def _(c):
    for y in range(16):
        for x in range(16):
            d = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if d <= 7.3 ** 2:
                c.set(x, y, "R" if d > 6.2 ** 2 else "r")
    for d in (0, 1):
        c.line(4 + d, 4, 11, 11 - d, "w")
        c.line(11 - d, 4, 4, 11 - d, "w")


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, canvas in sorted(ICONS.items()):
        with open(os.path.join(OUT, name + ".svg"), "w") as f:
            f.write(canvas.svg())
    print("wrote %d icons to %s" % (len(ICONS), os.path.normpath(OUT)))


if __name__ == "__main__":
    main()

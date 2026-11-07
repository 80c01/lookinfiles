#!/usr/bin/env python3
"""Generate the VS Code (Seti) file-type icons and their lookup table.

Usage: tools/gen_file_icons.py <vs-seti-icon-theme.json> <seti.woff>

Both inputs come from the VS Code repository,
extensions/theme-seti/icons/ (MIT, see icons/COPYING). Needs fontTools
(and brotli for .woff2). Writes:
  - icons/scalable/actions/lookin-ft-<icon>-symbolic.svg: one glyph of the
    Seti font as a single-colour path (no transform)
  - src/file_type_icons_table.h: file name / extension -> icon + colour
  - the <file> lines of icons/lookin.gresource.xml for those icons
The HANDMADE icons (formats Seti has no glyph for) are drawn by hand in
the same grid: their SVGs are kept, only their table entries are written.
"""
import json
import os
import re
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "icons", "scalable", "actions")
TABLE = os.path.join(ROOT, "src", "file_type_icons_table.h")
GRESOURCE = os.path.join(ROOT, "icons", "lookin.gresource.xml")

# Seti colours (dark variants) -> palette role suffix (icon_<name>)
COLORS = {
    "#519aba": "blue",
    "#4d5a5e": "grey",
    "#6d8086": "grey_light",
    "#8dc149": "green",
    "#e37933": "orange",
    "#f55385": "pink",
    "#a074c4": "purple",
    "#cc3e44": "red",
    "#d4d7d6": "white",
    "#cbcb41": "yellow",
    "#41535b": "ignore",
}

# VS Code decides these through the language of the file: its extensions
# and file names (from the built-in language extensions)
LANGUAGES = {
    "bat": ["bat", "cmd"],
    "clojure": ["clj", "cljs", "cljc", "cljx", "clojure"],
    "coffeescript": ["coffee", "cake"],
    "jsonc": ["jsonc", "code-workspace", "code-snippets"],
    "json": ["json", "webmanifest", "har", "geojson", "topojson", "jsonld"],
    "c": ["c", "i"],
    "cpp": ["cpp", "cc", "cxx", "c++", "ino", "ipp", "inl", "tpp", "ixx"],
    "cuda-cpp": ["cu"],
    "csharp": ["cs", "csx"],
    "css": ["css"],
    "dart": ["dart"],
    "dockerfile": ["dockerfile", "containerfile",
                   "=dockerfile", "=containerfile"],
    "dotenv": ["env", "=.env"],
    "ignore": ["gitignore", "gitignore_global", "git-blame-ignore-revs"],
    "fsharp": ["fs", "fsi", "fsx", "fsscript"],
    "git-commit": ["=commit_editmsg", "=merge_msg"],
    "go": ["go"],
    "groovy": ["groovy", "gvy"],
    "handlebars": ["handlebars", "hbs", "hjs"],
    "html": ["html", "htm", "shtml", "xhtml", "xht", "mdoc", "jsp", "asp", "jshtm"],
    "properties": ["properties", "cfg", "conf", "directory", "ini", "editorconfig"],
    "java": ["java", "jav"],
    "javascriptreact": ["jsx"],
    "javascript": ["js", "mjs", "cjs", "pac"],
    "julia": ["jl"],
    "tex": ["sty", "bbx", "cbx"],
    "latex": ["tex", "ltx", "ctx"],
    "less": ["less"],
    "lua": ["lua"],
    "makefile": ["mk", "mak", "=makefile", "=gnumakefile", "=ocamlmakefile"],
    "markdown": ["md", "mkd", "mdwn", "mdown", "markdown", "markdn", "mdtxt", "mdtext"],
    "objective-c": ["m"],
    "objective-cpp": ["mm"],
    "perl": ["pl", "pm", "pod", "t", "psgi"],
    "php": ["php", "php4", "php5", "phtml"],
    "powershell": ["ps1", "psm1", "psd1", "pssc", "psrc"],
    "jade": ["pug"],
    "python": ["py", "rpy", "pyw", "cpy", "gyp", "gypi", "pyi", "ipy", "pyt"],
    "r": ["rhistory", "rprofile", "rt"],
    "razor": ["cshtml", "razor"],
    "ruby": ["rb", "rbx", "rjs", "gemspec", "rake", "ru", "podspec", "rbi",
             "=gemfile", "=rakefile", "=podfile", "=guardfile", "=vagrantfile", "=brewfile"],
    "rust": ["rs"],
    "scss": ["scss"],
    "shellscript": ["sh", "bash", "bashrc", "bash_aliases", "bash_profile", "bash_login",
                    "bash_logout", "ebuild", "profile", "xprofile", "xsession", "xsessionrc",
                    "zsh", "zshrc", "zprofile", "zlogin", "zlogout", "zshenv", "zsh-theme",
                    "ksh", "csh", "cshrc", "tcshrc", "yashrc", "yash_profile"],
    "sql": ["sql", "dsql"],
    "swift": ["swift"],
    "typescript": ["ts", "cts", "mts"],
    "typescriptreact": ["tsx"],
    "xml": ["xml", "xsd", "atom", "axml", "bpmn", "csl", "csproj", "dita", "ditamap", "dtd",
            "ent", "fxml", "iml", "jmx", "launch", "mxml", "nuspec", "opml", "owl", "proj",
            "props", "publishsettings", "pubxml", "rdf", "rng", "rss", "shproj", "storyboard",
            "targets", "tld", "tmx", "vbproj", "vcxproj", "wsdl", "wxi", "wxl", "wxs", "xaml",
            "xbl", "xib", "xlf", "xliff", "xpdl", "xul", "xoml", "plist", "xsl", "xslt",
            "gresource.xml", "ui", "metainfo.xml"],
    "dockercompose": ["=compose.yml", "=compose.yaml",
                      "=docker-compose.yml", "=docker-compose.yaml"],
    "yaml": ["yml", "yaml", "eyaml", "eyml", "cff"],
    "jsonl": ["jsonl", "ndjson"],
    "postcss": ["pcss", "postcss"],
    "blade": ["blade.php"],
}

# Hand-drawn lookin-ft-<icon>-symbolic.svg: icon -> (colour, extensions)
HANDMADE = {
    "canvas": ("purple", ["canvas"]),
    "org": ("green", ["org", "org_archive"]),
}


def icon_base(def_id):
    """'_c_1' -> 'c', '_cake_php' -> 'cake-php', '_R' -> 'r'"""
    base = re.sub(r"_\d+$", "", def_id.lstrip("_"))
    return base.replace("_", "-").lower()


def glyph_svg(font, codepoint):
    glyphs = font.getGlyphSet()
    name = font.getBestCmap()[codepoint]
    pen = SVGPathPen(glyphs)
    # font units are y-up with the em box 0..1000: flip into SVG coordinates
    glyphs[name].draw(TransformPen(pen, (1, 0, 0, -1, 0, 1000)))
    # VS Code draws the glyphs at 150% of the font size in a 16 px box:
    # keep the middle of the em box
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" '
            'viewBox="60 60 880 880"><path d="%s"/></svg>\n' % pen.getCommands())


def main():
    theme_path, font_path = sys.argv[1], sys.argv[2]
    theme = json.load(open(theme_path, encoding="utf-8"))
    font = TTFont(font_path)
    defs = theme["iconDefinitions"]

    def resolve(def_id):
        d = defs[def_id]
        # no colour (todo): the text colour of the default icon
        color = COLORS[d.get("fontColor", defs[theme["file"]]["fontColor"]).lower()]
        return icon_base(def_id), color, int(d["fontCharacter"][1:], 16)

    names = {}  # lower-case file name -> def id
    exts = {}   # lower-case extension (without the first dot) -> def id
    for lang, keys in LANGUAGES.items():
        def_id = theme["languageIds"].get(lang)
        if def_id is None:
            continue
        for k in keys:
            if k.startswith("="):
                names.setdefault(k[1:], def_id)
            else:
                exts.setdefault(k, def_id)
    # The theme's own file names and extensions win over languages
    names.update({k.lower(): v for k, v in theme["fileNames"].items()})
    exts.update({k.lower(): v for k, v in theme["fileExtensions"].items()})

    for icon, (color, keys) in HANDMADE.items():
        exts.update({k: (icon, color) for k in keys})

    used = {}  # icon -> codepoint
    entries = []
    for table, items in (("names", names), ("exts", exts)):
        for key, def_id in sorted(items.items()):
            if isinstance(def_id, tuple):  # HANDMADE
                icon, color = def_id
            else:
                icon, color, cp = resolve(def_id)
                used.setdefault(icon, cp)
            entries.append((table, key, icon, color))
    default = resolve(theme["file"])
    used.setdefault(default[0], default[2])
    used.setdefault("folder", int(defs["_folder"]["fontCharacter"][1:], 16)
                    if "_folder" in defs else
                    next(cp for cp, n in font.getBestCmap().items() if n == "folder"))

    for f in os.listdir(ICON_DIR):
        if f.startswith("lookin-ft-") and f[10:-13] not in HANDMADE:
            os.remove(os.path.join(ICON_DIR, f))
    for icon, cp in sorted(used.items()):
        path = os.path.join(ICON_DIR, "lookin-ft-%s-symbolic.svg" % icon)
        with open(path, "w", encoding="utf-8") as out:
            out.write(glyph_svg(font, cp))

    with open(TABLE, "w", encoding="utf-8") as out:
        out.write("/* Generated by tools/gen_file_icons.py from the VS Code Seti icon theme:\n"
                  " * do not edit. File name or extension (lower case) -> icon, colour. */\n")
        out.write('#define FILE_TYPE_ICON_DEFAULT {"%s", "%s"}\n\n' % default[:2])
        for table in ("names", "exts"):
            out.write("static const FileTypeIconEntry FILE_TYPE_ICON_%s[] = {\n" % table.upper())
            for t, key, icon, color in entries:
                if t == table:
                    out.write('    {"%s", {"%s", "%s"}},\n' % (key, icon, color))
            out.write("};\n\n")

    # GResource manifest: replace the file-type icon lines
    lines = [l for l in open(GRESOURCE, encoding="utf-8").read().splitlines()
             if "lookin-ft-" not in l]
    end = max(i for i, l in enumerate(lines) if "<file>" in l) + 1
    ft = ["    <file>scalable/actions/lookin-ft-%s-symbolic.svg</file>" % i
          for i in sorted(set(used) | set(HANDMADE))]
    lines[end:end] = ft
    with open(GRESOURCE, "w", encoding="utf-8") as out:
        out.write("\n".join(lines) + "\n")
    print("%d icons, %d names, %d extensions" % (len(used), len(names), len(exts)))


if __name__ == "__main__":
    main()

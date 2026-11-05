#!/bin/sh
# Lists the libraries LookinFiles builds with (see the README requirements):
# GTK 4 and libgit2 are required, the others enable optional features. Exits 1 when a
# required library is missing. Used by `make deps` and after each build.

PKG_CONFIG=${PKG_CONFIG:-pkg-config}
status=0

# check <pkg-config name> <minimum version or -> <feature> <Debian / Ubuntu package> <required|optional>
check() {
    if [ "$2" != "-" ]; then
        found=$($PKG_CONFIG --atleast-version="$2" "$1" 2>/dev/null && echo yes)
    else
        found=$($PKG_CONFIG --exists "$1" 2>/dev/null && echo yes)
    fi
    if [ "$found" = yes ]; then
        printf '  %-34s yes  (%s %s)\n' "$3" "$1" "$($PKG_CONFIG --modversion "$1")"
    else
        need=""
        [ "$2" != "-" ] && need=" >= $2"
        printf '  %-34s no   -> apt install %s  (%s%s)\n' "$3" "$4" "$1" "$need"
        [ "$5" = required ] && status=1
    fi
}

echo "Required:"
check gtk4            4.12 "GTK 4"                  libgtk-4-dev            required
check libgit2         1.1  "Git (libgit2)"          libgit2-dev             required
echo "Optional features:"
if [ "${OS:-}" = Windows_NT ]; then
    printf '  %-34s yes  (built in, ConPTY)\n' "Embedded terminal"
else
    check vte-2.91-gtk4 - "Embedded terminal"      libvte-2.91-gtk4-dev    optional
fi
check gtksourceview-5 -    "Syntax highlighting"    libgtksourceview-5-dev  optional
check poppler-glib    -    "PDF preview"            libpoppler-glib-dev     optional
check sqlite3         -    "Database preview"       libsqlite3-dev          optional
check libarchive      -    "7z, RAR, xz, zstd, ISO" libarchive-dev          optional
check webkitgtk-6.0   -    "Browser tabs"           libwebkitgtk-6.0-dev    optional
echo "Run time (not needed to build):"
for tool in sshfs; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '  %-34s yes\n' "$tool"
    else
        printf '  %-34s no   -> apt install %s\n' "$tool" "$tool"
    fi
done
[ $status -ne 0 ] && echo "A required library is missing: the build cannot succeed."
exit $status

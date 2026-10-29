#!/bin/sh
# Preview benchmark (make bench-preview): times every structured preview on
# the fixture corpus (tests/fixtures/preview) plus large generated files, and
# fails if one exceeds its budget (TASK.md, rule 1).
#
# usage: tools/bench_preview.sh build/preview_tool [extra dirs...]
set -eu
cd "$(dirname "$0")/.."
TOOL=$1
shift
DIR=build/bench
rm -rf "$DIR"
mkdir -p "$DIR"
cp -r tests/fixtures/preview/. "$DIR/"
cp tests/ui/fixtures/report.docx "$DIR/report.docx"

# Large files, the worst case of each text format the viewer reads (512 KB)
awk 'BEGIN { print "id,name,city,amount,date"; for (i = 1; i <= 12000; i++)
     printf "%d,Name %d,\"City, %d\",%d.%02d,2026-01-%02d\n", i, i, i % 97, i * 7, i % 100, i % 28 + 1 }' \
    > "$DIR/large.csv"
awk 'BEGIN { print "["; for (i = 1; i <= 4000; i++)
     printf "  {\"id\": %d, \"name\": \"item %d\", \"tags\": [\"a\", \"b\"], \"ok\": %s, \"price\": %d.5}%s\n",
            i, i, (i % 2 ? "true" : "false"), i, (i < 4000 ? "," : ""); print "]" }' > "$DIR/large.json"
awk 'BEGIN { for (i = 1; i <= 8000; i++)
     printf "2026-10-29 10:%02d:%02d %s worker[%d]: request %d handled in %d ms\n",
            i % 60, i % 60, (i % 50 == 0 ? "ERROR" : (i % 10 == 0 ? "WARN" : "INFO")), i % 8, i, i % 300 }' \
    > "$DIR/large.log"
awk 'BEGIN { for (f = 1; f <= 60; f++) { printf "diff --git a/f%d.c b/f%d.c\n--- a/f%d.c\n+++ b/f%d.c\n", f, f, f, f;
     for (h = 1; h <= 10; h++) { printf "@@ -%d,6 +%d,6 @@\n", h * 20, h * 20;
       for (l = 1; l <= 6; l++) printf "%s line %d of hunk %d\n", (l == 3 ? "-" : (l == 4 ? "+" : " ")), l, h } } }' \
    > "$DIR/large.diff"
# Large shared library: ELF summary of a big symbol table
for lib in /usr/lib/x86_64-linux-gnu/libgtk-4.so.1 /usr/lib/libgtk-4.so.1; do
    if [ -f "$lib" ]; then cp "$lib" "$DIR/libgtk-4.so.1"; break; fi
done

"$TOOL" bench "$DIR" "$@"

# Fixtures and scenario of the file format session of run_ui_test.sh (sourced:
# uses $FMT, $ROOT, key and shot). One file per format, named so that they
# sort in scenario order; the preview of each one is shot in turn.

# Each fixture is created here; keep the NN- prefixes in sorted order.
(
    cd "$FMT" || exit 1
    # ELF executable: architecture, type, linked libraries
    cp /bin/true 01-program
    # Unknown binary format: hex dump (all 256 byte values, twice)
    for i in $(seq 0 511); do printf "\\$(printf %03o $((i % 256)))"; done > 02-unknown.bin
    # Word document: headings, lists, table, image, link
    cp "$ROOT/tests/ui/fixtures/report.docx" 03-report.docx
    cp "$ROOT/tests/fixtures/preview/sample.csv" 04-orders.csv
    cp "$ROOT/tests/fixtures/preview/sample.json" 05-package.json
    cp "$ROOT/tests/fixtures/preview/fix-parser.patch" 06-fix-parser.patch
    cp "$ROOT/tests/fixtures/preview/sample.zip" 07-sources.zip
    cp "$ROOT/tests/fixtures/preview/sample.tar.gz" 08-sources.tar.gz
    cp "$ROOT/tests/fixtures/preview/server.log.gz" 09-server.log.gz
    cp "$ROOT/tests/fixtures/preview/server.crt" 10-server.crt
    cp "$ROOT/tests/fixtures/preview/meetings.ics" 11-meetings.ics
    cp "$ROOT/tests/fixtures/preview/contacts.vcf" 12-contacts.vcf
    cp "$ROOT/tests/fixtures/preview/editor.desktop" 13-lookin.desktop
    cp "$ROOT/tests/fixtures/preview/app.log" 14-app.log
    cp "$ROOT/tests/fixtures/preview/photo.jpg" 15-photo.jpg
    # Font: a system font (the step shows the hex dump of a stand-in without it)
    for f in /usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf; do
        [ -f "$f" ] && { cp "$f" 16-font.ttf; break; }
    done
    [ -f 16-font.ttf ] || printf 'not a font' > 16-font.ttf
    gzip -c -n "$ROOT/tests/fixtures/preview/lookin.1" > 17-lookin.1.gz
    cp "$ROOT/tests/fixtures/preview/analysis.ipynb" 18-analysis.ipynb
    cp "$ROOT/tests/fixtures/preview/budget.xlsx" 19-budget.xlsx
    cp "$ROOT/tests/fixtures/preview/stock.ods" 20-stock.ods
    cp "$ROOT/tests/fixtures/preview/report.odt" 21-report.odt
    cp "$ROOT/tests/fixtures/preview/roadmap.pptx" 22-roadmap.pptx
    cp "$ROOT/tests/fixtures/preview/roadmap.odp" 23-roadmap.odp
    cp "$ROOT/tests/fixtures/preview/story.epub" 24-story.epub
) || { echo "ui-test: format fixtures failed"; exit 1; }

format_scenario() {
    key g g;            shot fmt_elf_summary
    key m;              shot fmt_elf_raw_hex_dump
    key m
    key j;              shot fmt_hex_dump
    key j;              shot fmt_word_document
    key j;              shot fmt_csv_table
    key m;              shot fmt_csv_raw
    key m
    key j;              shot fmt_json_pretty
    key j;              shot fmt_diff
    key j;              shot fmt_zip
    key j;              shot fmt_tar_gz
    key j;              shot fmt_gzip_single
    key j;              shot fmt_certificate
    key j;              shot fmt_calendar
    key j;              shot fmt_contacts
    key j;              shot fmt_desktop_entry
    key j;              shot fmt_log
    key j;              shot fmt_photo_exif
    key plus plus;      shot fmt_photo_zoomed
    key 0
    key j;              shot fmt_font
    key j;              shot fmt_manpage
    key j;              shot fmt_notebook
    key j;              shot fmt_xlsx
    key j;              shot fmt_ods
    key j;              shot fmt_odt
    key j;              shot fmt_pptx
    key j;              shot fmt_odp
    key j;              shot fmt_epub
}

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
}

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
) || { echo "ui-test: format fixtures failed"; exit 1; }

format_scenario() {
    key g g;            shot fmt_elf_summary
    key m;              shot fmt_elf_raw_hex_dump
    key m
    key j;              shot fmt_hex_dump
    key j;              shot fmt_word_document
}

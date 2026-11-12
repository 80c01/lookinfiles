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
    cp "$ROOT/tests/fixtures/preview/notes.rtf" 25-notes.rtf
    cp "$ROOT/tests/fixtures/preview/library.sqlite" 26-library.sqlite
    cp "$ROOT/tests/fixtures/preview/fr.mo" 27-lookin-fr.mo
    cp "$ROOT/tests/fixtures/preview/song.mp3" 28-song.mp3
    cp "$ROOT/tests/fixtures/preview/photo.dng" 29-photo.dng
    # Legacy encodings: UTF-16 without a mark, Windows-1252
    printf 'Caf\351 cr\350me \200 5\n\223quoted\224\n' > 30-latin1.txt
    printf 'h\0e\0l\0l\0o\0 \0w\0o\0r\0l\0d\0\n\0' > 31-utf16.txt
    cp "$ROOT/tests/fixtures/preview/blink.gif" 32-blink.gif
    cp "$ROOT/tests/fixtures/preview/pages.pdf" 33-pages.pdf
    # Office 97-2003 (saved by LibreOffice); a copy marked as encrypted
    cp "$ROOT/tests/fixtures/preview/report.doc" 34-report.doc
    cp "$ROOT/tests/fixtures/preview/encrypted.doc" 35-encrypted.doc
    cp "$ROOT/tests/fixtures/preview/budget.xls" 36-budget.xls
    cp "$ROOT/tests/fixtures/preview/roadmap.ppt" 37-roadmap.ppt
    # Mail: a text message with quotes and signature, an HTML one with an inline image
    cp "$ROOT/tests/fixtures/mail/alternative.eml" 38-agenda.eml
    cp "$ROOT/tests/fixtures/mail/html-inline.eml" 39-news.eml
    cp "$ROOT/tests/fixtures/mail/Inbox" 40-Inbox     # Thunderbird mbox: no extension
    cp "$ROOT/tests/fixtures/mail/sample.msg" 41-figures.msg   # Outlook
    cp "$ROOT/tests/fixtures/mail/winmail.dat" 42-winmail.dat  # TNEF
    cp "$ROOT/tests/fixtures/mail/archive.pst" 43-archive.pst  # header only
    cp "$ROOT/tests/fixtures/preview/deck.pptx" 44-deck.pptx   # theme, master, layouts
    cp "$ROOT/tests/fixtures/preview/clip.mp4" 45-clip.mp4     # Enter: Video tab
    # Maildir folder (a directory: listed first)
    cp -r "$ROOT/tests/fixtures/mail/Maildir" 00-Maildir
    find . -exec touch -h -d '2000-01-01 00:00:00' {} +   # dates shown do not vary
) || { echo "ui-shots: format fixtures failed"; exit 1; }

format_scenario() {
    key Home; sleep 0.5; shot fmt_maildir_folder
    key Down;           shot fmt_elf_summary
    key m;              shot fmt_elf_raw_hex_dump
    key m
    key Down;           shot fmt_hex_dump
    key Down;           shot fmt_word_document
    key Down;           shot fmt_csv_table
    key m;              shot fmt_csv_raw
    key m
    key Down;           shot fmt_json_pretty
    key Down;           shot fmt_diff
    key Down;           shot fmt_zip
    key Down;           shot fmt_tar_gz
    key Down;           shot fmt_gzip_single
    key Down;           shot fmt_certificate
    key Down;           shot fmt_calendar
    key Down;           shot fmt_contacts
    key Down;           shot fmt_desktop_entry
    key Down;           shot fmt_log
    key Down;           shot fmt_photo_exif
    key plus plus;      shot fmt_photo_zoomed
    key 0
    key Down;           shot fmt_font
    key Down;           shot fmt_manpage
    key Down;           shot fmt_notebook
    key Down;           shot fmt_xlsx
    key Down;           shot fmt_ods
    key Down;           shot fmt_odt
    key Down;           shot fmt_pptx
    key Down;           shot fmt_odp
    key Down;           shot fmt_epub
    key Down;           shot fmt_rtf
    key Down;           shot fmt_sqlite
    key Down;           shot fmt_translations
    key Down;           shot fmt_audio_tags
    key Down;           shot fmt_raw_photo
    key Down;           shot fmt_windows_1252
    key Down;           shot fmt_utf16
    key Down;           shot fmt_gif_animated   # frame varies: not compared with a baseline
    key Down;           shot fmt_pdf_pages      # 3 pages first, the others added after
    key Down;           shot fmt_word97
    key Down;           shot fmt_word97_encrypted
    key Down;           shot fmt_excel97
    key Down;           shot fmt_powerpoint97
    key Down;           shot fmt_mail_text
    key Down;           shot fmt_mail_html
    key Down;           shot fmt_mailbox_list
    key alt+Right                               # focus the preview
    key Page_Down Page_Down; shot fmt_mailbox_message_2
    key alt+Left
    key Down;           shot fmt_outlook_msg
    key Down;           shot fmt_tnef
    key Down;           shot fmt_outlook_pst
    key Down;           shot fmt_pptx_theme
    key Up Up Up Up Up Up Up Up Up Up Up
    key alt+Right                               # focus the preview
    key Page_Down Page_Down; shot fmt_pdf_page_down
    # Video: Space plays the preview, Right seeks in it; Enter opens a Video
    # tab in the editor panel (playing: the frame varies, so these shots are
    # not compared with a baseline), Space pauses it, a / s switch the audio
    # track / subtitles (the clip has none: nothing changes), Ctrl+Alt+Left moves it
    # to the preview panel, Ctrl+W closes it
    key alt+Left End
    key space Right;    shot fmt_video_preview_seek_animated
    key space
    key Return;         shot fmt_video_tab_animated
    key space Home;     shot fmt_video_tab_paused_animated
    key a s;            shot fmt_video_tab_tracks_animated
    key ctrl+alt+Left;  shot fmt_video_tab_moved_animated
    key ctrl+w;         shot fmt_video_tab_closed
    # Jupyter notebook: Enter opens the notebook tab (cells, outputs);
    # Ctrl+Down goes to the next cell, Alt+Up moves it up, Alt+Enter adds a
    # code cell below, Alt+Delete deletes it, Ctrl+S saves, Ctrl+W closes
    key alt+Left Home
    for i in $(seq 18); do key Down; done
    key Return; sleep 0.5; shot fmt_ipynb_tab
    key ctrl+Down;      shot fmt_ipynb_next_cell
    key alt+Up;         shot fmt_ipynb_cell_moved_up
    key alt+Return;     shot fmt_ipynb_cell_added
    key alt+Delete;     shot fmt_ipynb_cell_deleted
    key ctrl+s;         shot fmt_ipynb_saved
    key ctrl+w;         shot fmt_ipynb_tab_closed
}

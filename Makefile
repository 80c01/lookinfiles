# Makefile for LookinFiles, a GTK 4 file explorer
#
# Each source file is compiled to its own object in build/ with automatic
# header dependency tracking (-MMD -MP): editing a header rebuilds every
# file that includes it, and only those files.

CC = gcc
PKG_CONFIG ?= pkg-config

# CFLAGS and LDFLAGS are left to the user and to packagers (make CFLAGS=-O0,
# or the environment); CFLAGS_EXTRA adds to them (make CFLAGS_EXTRA=-Werror).
# The flags the build needs are in APP_CFLAGS and LIBS, always added.
CFLAGS ?= -O2 -g
LDFLAGS ?=
CFLAGS_EXTRA ?=

# Required: GTK 4 and libgit2 (git status, history and file lists, src/git_utils.c)
APP_CFLAGS = -Wall -Wextra -Isrc `$(PKG_CONFIG) --cflags gtk4 libgit2`
LIBS = `$(PKG_CONFIG) --libs gtk4 libgit2` -lm
DEPFLAGS = -MMD -MP

# Optional embedded terminal: enabled automatically when vte-2.91-gtk4 is installed.
# On Windows (no VTE) the built-in terminal is used instead (src/term_widget.c
# on ConPTY, under the VTE API: HAVE_VTE means "a terminal is available");
# BUILTIN_TERM=yes uses it on other systems too.
HAVE_VTE != $(PKG_CONFIG) --exists vte-2.91-gtk4 2>/dev/null && echo yes || echo no
ifeq ($(OS),Windows_NT)
BUILTIN_TERM ?= yes
else
BUILTIN_TERM ?= no
endif
ifeq ($(BUILTIN_TERM),yes)
APP_CFLAGS += -DHAVE_VTE -DHAVE_BUILTIN_TERM
else ifeq ($(HAVE_VTE),yes)
APP_CFLAGS += -DHAVE_VTE `$(PKG_CONFIG) --cflags vte-2.91-gtk4`
LIBS += `$(PKG_CONFIG) --libs vte-2.91-gtk4`
endif

# Optional syntax highlighting: enabled automatically when gtksourceview-5 is installed
HAVE_GSV != $(PKG_CONFIG) --exists gtksourceview-5 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_GSV),yes)
APP_CFLAGS += -DHAVE_GSV `$(PKG_CONFIG) --cflags gtksourceview-5`
LIBS += `$(PKG_CONFIG) --libs gtksourceview-5`
endif

# Optional PDF rendering: enabled automatically when poppler-glib is installed
HAVE_POPPLER != $(PKG_CONFIG) --exists poppler-glib 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_POPPLER),yes)
APP_CFLAGS += -DHAVE_POPPLER `$(PKG_CONFIG) --cflags poppler-glib`
LIBS += `$(PKG_CONFIG) --libs poppler-glib`
endif

# Optional SQLite database preview: enabled automatically when sqlite3 is installed
HAVE_SQLITE != $(PKG_CONFIG) --exists sqlite3 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_SQLITE),yes)
APP_CFLAGS += -DHAVE_SQLITE `$(PKG_CONFIG) --cflags sqlite3`
LIBS += `$(PKG_CONFIG) --libs sqlite3`
endif

# Optional browser tabs: enabled automatically when webkitgtk-6.0 is installed
HAVE_WEBKIT != $(PKG_CONFIG) --exists webkitgtk-6.0 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_WEBKIT),yes)
APP_CFLAGS += -DHAVE_WEBKIT `$(PKG_CONFIG) --cflags webkitgtk-6.0`
LIBS += `$(PKG_CONFIG) --libs webkitgtk-6.0`
endif

# Optional archive formats (7z, RAR, xz, bzip2, zstd, ISO...): enabled automatically with libarchive
HAVE_LIBARCHIVE != $(PKG_CONFIG) --exists libarchive 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_LIBARCHIVE),yes)
APP_CFLAGS += -DHAVE_LIBARCHIVE `$(PKG_CONFIG) --cflags libarchive`
LIBS += `$(PKG_CONFIG) --libs libarchive`
endif

# Optional GStreamer video player (choice of audio and subtitle tracks):
# enabled automatically when the GStreamer base libraries are installed
GST_PKGS = gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-tag-1.0
HAVE_GSTREAMER != $(PKG_CONFIG) --exists $(GST_PKGS) 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_GSTREAMER),yes)
APP_CFLAGS += -DHAVE_GSTREAMER `$(PKG_CONFIG) --cflags $(GST_PKGS)`
LIBS += `$(PKG_CONFIG) --libs $(GST_PKGS)`
endif

# Installation prefix (make install PREFIX=/usr): also tells the binary where
# the translations are installed (src/i18n.c looks next to it first)
PREFIX ?= /usr/local
DATADIR = $(PREFIX)/share
LOCALEDIR = $(DATADIR)/locale
APP_CFLAGS += -DLOCALEDIR=\"$(LOCALEDIR)\"

# Release version: can be overridden via make VERSION=... or environment.
# Defaults to git tags (e.g. v0.4.2 -> 0.4.2), falling back to src/app_paths.h.
VERSION != sh tools/version.sh 2>/dev/null || sed -n 's/^\#define APP_VERSION *"\(.*\)"/\1/p' src/app_paths.h 2>/dev/null || echo "0.6.0"
APP_CFLAGS += -DAPP_VERSION=\"$(VERSION)\"

# Windows platform detection
ifeq ($(OS),Windows_NT)
EXE = .exe
# -mwindows suppresses the background cmd terminal window in release builds
WINDOWS_SUBSYSTEM ?= -mwindows
ALL_LDFLAGS += $(WINDOWS_SUBSYSTEM)
else
EXE =
endif

TARGET = lookinfiles$(EXE)
BUILD_DIR = build

ALL_CFLAGS = $(APP_CFLAGS) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_EXTRA)
ALL_LDFLAGS = $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS)

# Everything is rebuilt when the compiler flags change: the stamp file is
# rewritten (and gets newer than the objects) only when they differ.
FLAGS_STAMP = $(BUILD_DIR)/flags
FLAGS_NOW = $(CC) $(ALL_CFLAGS) | $(ALL_LDFLAGS) $(LIBS)
$(shell mkdir -p $(BUILD_DIR); if command -v cmp >/dev/null 2>&1; then printf '%s\n' '$(subst ','\'',$(FLAGS_NOW))' | cmp -s - $(FLAGS_STAMP) || printf '%s\n' '$(subst ','\'',$(FLAGS_NOW))' > $(FLAGS_STAMP); else printf '%s\n' '$(subst ','\'',$(FLAGS_NOW))' > $(FLAGS_STAMP); fi)

# Every .c file in src/ is part of the application: adding a module only
# requires creating src/<module>.c and src/<module>.h.
SRCS = $(sort $(wildcard src/*.c))
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o) $(BUILD_DIR)/icon_resources.o $(BUILD_DIR)/guide_resources.o $(BUILD_DIR)/theme_resources.o

# Toolbar icons bundled into the binary (icons/, see icons/COPYING and src/icons.c)
GLIB_COMPILE_RESOURCES != $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0
ICON_FILES = $(sort $(wildcard icons/scalable/actions/*.svg icons/sets/*/*.svg))
DEPS = $(OBJS:.o=.d)

all: $(TARGET) translations

$(TARGET): $(OBJS) $(FLAGS_STAMP)
	$(CC) $(ALL_LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)
	@PKG_CONFIG=$(PKG_CONFIG) sh tools/check_deps.sh || true

# Lists the required and optional libraries found (tools/check_deps.sh)
deps:
	@PKG_CONFIG=$(PKG_CONFIG) sh tools/check_deps.sh

$(BUILD_DIR)/%.o: src/%.c $(FLAGS_STAMP) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/icon_resources.c: icons/lookin.gresource.xml $(ICON_FILES) | $(BUILD_DIR)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=icons --generate-source --target=$@ $<

$(BUILD_DIR)/icon_resources.o: $(BUILD_DIR)/icon_resources.c $(FLAGS_STAMP)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# User guide bundled into the binary (src/user_guide.c, the Help tab): every
# page of docs/user-guide/ and the reference pages it links to. The resource
# list is generated, so a new page only needs its .md file.
GUIDE_FILES = $(sort $(wildcard docs/user-guide/*.md)) docs/keyboard.md docs/previews.md docs/configuration.md

$(BUILD_DIR)/guide.gresource.xml: $(GUIDE_FILES) Makefile | $(BUILD_DIR)
	@{ echo '<?xml version="1.0" encoding="UTF-8"?>'; \
	  echo '<gresources>'; \
	  echo '  <gresource prefix="/org/lookin/guide">'; \
	  for f in $(GUIDE_FILES:docs/%=%); do echo "    <file>$$f</file>"; done; \
	  echo '  </gresource>'; \
	  echo '</gresources>'; } > $@

$(BUILD_DIR)/guide_resources.c: $(BUILD_DIR)/guide.gresource.xml $(GUIDE_FILES)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=docs --c-name guide --generate-source --target=$@ $<

$(BUILD_DIR)/guide_resources.o: $(BUILD_DIR)/guide_resources.c $(FLAGS_STAMP)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# Themes of themes/ bundled into the binary (theme_store.c): one resource
# per .ini file, the list generated like the guide's.
THEME_FILES = $(sort $(wildcard themes/*.ini))

$(BUILD_DIR)/theme.gresource.xml: $(THEME_FILES) Makefile | $(BUILD_DIR)
	@{ echo '<?xml version="1.0" encoding="UTF-8"?>'; \
	  echo '<gresources>'; \
	  echo '  <gresource prefix="/org/lookin/themes">'; \
	  for f in $(THEME_FILES:themes/%=%); do echo "    <file>$$f</file>"; done; \
	  echo '  </gresource>'; \
	  echo '</gresources>'; } > $@

$(BUILD_DIR)/theme_resources.c: $(BUILD_DIR)/theme.gresource.xml $(THEME_FILES)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=themes --c-name themes --generate-source --target=$@ $<

$(BUILD_DIR)/theme_resources.o: $(BUILD_DIR)/theme_resources.c $(FLAGS_STAMP)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

# Translations (src/i18n.h, po/): po/<lang>.po compiled to
# build/locale/<lang>/LC_MESSAGES/lookinfiles.mo, where ./lookinfiles finds
# them. Without msgfmt (gettext tools) the build goes on in English.
# make pot rebuilds the template from the marked strings; make po-update merges
# it into every po/<lang>.po.
XGETTEXT ?= xgettext
MSGFMT ?= msgfmt
MSGMERGE ?= msgmerge
GETTEXT_DOMAIN = lookinfiles
PO_FILES = $(sort $(wildcard po/*.po))
LINGUAS = $(PO_FILES:po/%.po=%)
MO_FILES = $(LINGUAS:%=$(BUILD_DIR)/locale/%/LC_MESSAGES/$(GETTEXT_DOMAIN).mo)
HAVE_MSGFMT != command -v $(MSGFMT) >/dev/null 2>&1 && echo yes || echo no
XGETTEXT_FLAGS = --from-code=UTF-8 --language=C --add-comments=TRANSLATORS: \
	--keyword=_ --keyword=N_ --keyword=C_:1c,2 --keyword=NC_:1c,2 \
	--keyword=ngettext:1,2 --keyword=g_dngettext:2,3 \
	--package-name=$(TARGET) --package-version=$(VERSION) --msgid-bugs-address=https://github.com/80c01/lookinfiles/issues

ifeq ($(HAVE_MSGFMT),yes)
translations: $(MO_FILES)
else
translations:
	@[ -z "$(PO_FILES)" ] || echo "msgfmt not found (gettext): translations not built"
endif

$(BUILD_DIR)/locale/%/LC_MESSAGES/$(GETTEXT_DOMAIN).mo: po/%.po
	@mkdir -p $(dir $@)
	$(MSGFMT) --check -o $@ $<

pot:
	$(XGETTEXT) $(XGETTEXT_FLAGS) -o po/$(GETTEXT_DOMAIN).pot $(sort $(wildcard src/*.c src/*.h))

po-update: pot
	@for po in $(PO_FILES); do echo "== $$po"; $(MSGMERGE) --update --backup=none $$po po/$(GETTEXT_DOMAIN).pot || exit 1; done

# Unit tests (GLib g_test, no display needed): tests/test_<module>.c links
# against the listed objects only. Modules under test must not create widgets.
# Tests use the application CFLAGS so that AppData has the same layout as in
# the objects under test. Register new tests in TESTS and TEST_DEPS_<name>.
TEST_CFLAGS = $(ALL_CFLAGS)
TEST_LDFLAGS = $(ALL_LDFLAGS) $(LIBS)
TESTS = test_app_paths test_i18n test_file_types test_file_type_icons test_notebook_icons test_git_utils test_remote_store test_preview_summary test_explorer_row test_layout_store test_line_diff test_pane_geometry test_minimap_geometry test_dir_summary test_dir_scan test_file_save test_hex_dump test_elf_info test_zip_reader test_docx test_doc_writer test_preview_doc test_csv test_json_view test_diff_view test_archive_listing test_archive_extract test_cert_view test_card_view test_log_view test_exif test_font_info test_man_view test_notebook test_spreadsheet test_odf test_pptx test_slide_render test_epub test_rtf test_sqlite_view test_gettext_view test_class_info test_wasm_info test_torrent_info test_pyc_info test_tfm_info test_pcap_info test_media_tags test_media_tracks test_text_encoding test_preview_formats test_org_time test_org_doc test_org_table test_dunnet test_vim_engine test_vim_parse test_vim_regex test_vim_ex test_code_scan test_code_outline_tree test_code_index test_code_tags test_jump_list test_lsp_protocol test_lsp_client test_lsp_config test_ole_reader test_office97 test_mail test_gif_decoder test_palette test_image_theme test_fonts test_theme_store test_text_tags test_preview_cache test_path_kind test_nav_keys test_trash_restore test_fuzzy_match test_text_search test_path_globs test_session_store test_tab_tree test_settings_store test_note_types test_canvas_doc test_canvas_geometry test_canvas_history test_user_guide test_web_address test_ps_convert test_vt_screen test_vt_pty test_ui_probe
TEST_BINS = $(TESTS:%=$(BUILD_DIR)/tests/%)
TEST_DEPS_test_app_paths = $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_i18n = $(BUILD_DIR)/i18n.o
TEST_DEPS_test_file_types = $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_file_type_icons = $(BUILD_DIR)/file_type_icons.o
TEST_DEPS_test_notebook_icons = $(BUILD_DIR)/notebook_icons.o
TEST_DEPS_test_git_utils = $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_remote_store = $(BUILD_DIR)/remote_store.o $(BUILD_DIR)/app_paths.o $(BUILD_DIR)/notebook_icons.o
TEST_DEPS_test_preview_summary = $(BUILD_DIR)/preview_summary.o $(BUILD_DIR)/explorer_row.o $(BUILD_DIR)/note_types.o $(BUILD_DIR)/canvas_doc.o
TEST_DEPS_test_note_types = $(BUILD_DIR)/note_types.o
TEST_DEPS_test_canvas_doc = $(BUILD_DIR)/canvas_doc.o
TEST_DEPS_test_canvas_geometry = $(BUILD_DIR)/canvas_geometry.o
TEST_DEPS_test_canvas_history = $(BUILD_DIR)/canvas_history.o
TEST_DEPS_test_user_guide = $(BUILD_DIR)/user_guide.o $(BUILD_DIR)/guide_resources.o
TEST_DEPS_test_explorer_row = $(BUILD_DIR)/explorer_row.o
TEST_DEPS_test_layout_store = $(BUILD_DIR)/layout_store.o $(BUILD_DIR)/tab_tree.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_line_diff = $(BUILD_DIR)/line_diff.o
TEST_DEPS_test_preview_cache = $(BUILD_DIR)/preview_cache.o
TEST_DEPS_test_path_kind = $(BUILD_DIR)/path_kind.o
TEST_DEPS_test_nav_keys = $(BUILD_DIR)/nav_keys.o
TEST_DEPS_test_pane_geometry = $(BUILD_DIR)/pane_geometry.o
TEST_DEPS_test_minimap_geometry = $(BUILD_DIR)/minimap_geometry.o
TEST_DEPS_test_ui_probe = $(BUILD_DIR)/ui_probe.o
TEST_DEPS_test_dir_summary = $(BUILD_DIR)/dir_summary.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_file_save = $(BUILD_DIR)/file_save.o
TEST_DEPS_test_dir_scan = $(BUILD_DIR)/dir_scan.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_ps_convert = $(BUILD_DIR)/ps_convert.o
TEST_DEPS_test_vt_screen = $(BUILD_DIR)/vt_screen.o
TEST_DEPS_test_vt_pty = $(BUILD_DIR)/vt_pty.o
TEST_DEPS_test_hex_dump = $(BUILD_DIR)/hex_dump.o
TEST_DEPS_test_elf_info = $(BUILD_DIR)/elf_info.o
TEST_DEPS_test_zip_reader = $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_docx = $(BUILD_DIR)/docx.o $(BUILD_DIR)/docx_styles.o $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_preview_doc = $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_doc_writer = $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_json_view = $(BUILD_DIR)/json_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_diff_view = $(BUILD_DIR)/diff_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_archive_listing = $(BUILD_DIR)/archive_listing.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_archive_extract = $(BUILD_DIR)/archive_extract.o $(BUILD_DIR)/zip_reader.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_cert_view = $(BUILD_DIR)/cert_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_card_view = $(BUILD_DIR)/card_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_log_view = $(BUILD_DIR)/log_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_exif = $(BUILD_DIR)/exif.o
TEST_DEPS_test_font_info = $(BUILD_DIR)/font_info.o
TEST_DEPS_test_man_view = $(BUILD_DIR)/man_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_notebook = $(BUILD_DIR)/notebook.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_spreadsheet = $(BUILD_DIR)/spreadsheet.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_odf = $(BUILD_DIR)/odf.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_pptx = $(BUILD_DIR)/pptx.o $(BUILD_DIR)/pptx_shapes.o $(BUILD_DIR)/pptx_build.o $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_slide_render = $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_epub = $(BUILD_DIR)/epub.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_rtf = $(BUILD_DIR)/rtf.o
TEST_DEPS_test_sqlite_view = $(BUILD_DIR)/sqlite_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_gettext_view = $(BUILD_DIR)/gettext_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_class_info = $(BUILD_DIR)/class_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_wasm_info = $(BUILD_DIR)/wasm_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_torrent_info = $(BUILD_DIR)/torrent_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_pyc_info = $(BUILD_DIR)/pyc_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_tfm_info = $(BUILD_DIR)/tfm_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_pcap_info = $(BUILD_DIR)/pcap_info.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_media_tags = $(BUILD_DIR)/media_tags.o
TEST_DEPS_test_media_tracks = $(BUILD_DIR)/media_tracks.o
TEST_DEPS_test_text_encoding = $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_csv = $(BUILD_DIR)/csv.o $(BUILD_DIR)/preview_doc.o
# Modules of the structured previews (preview_formats and the parsers it calls)
PREVIEW_MODULES = preview_formats preview_doc doc_writer file_types text_encoding hex_dump elf_info zip_reader docx docx_styles csv json_view diff_view archive_listing cert_view card_view log_view font_info man_view notebook zip_xml spreadsheet odf pptx pptx_shapes pptx_build slide_render epub rtf sqlite_view gettext_view class_info wasm_info torrent_info pyc_info tfm_info pcap_info ole_reader ole_props office97 doc_reader xls_reader ppt_reader mail_message mail_view mail_box mail_dir mail_msg mail_tnef mail_store
PREVIEW_FORMAT_OBJS = $(PREVIEW_MODULES:%=$(BUILD_DIR)/%.o)
TEST_DEPS_test_preview_formats = $(PREVIEW_FORMAT_OBJS)
TEST_DEPS_test_org_time = $(BUILD_DIR)/org_time.o
TEST_DEPS_test_org_doc = $(BUILD_DIR)/org_doc.o $(BUILD_DIR)/org_time.o
TEST_DEPS_test_org_table = $(BUILD_DIR)/org_table.o $(BUILD_DIR)/org_doc.o $(BUILD_DIR)/org_time.o
TEST_DEPS_test_dunnet = $(BUILD_DIR)/dunnet.o
VIM_OBJS = $(BUILD_DIR)/vim_engine.o $(BUILD_DIR)/vim_engine_ex.o $(BUILD_DIR)/vim_parse.o $(BUILD_DIR)/vim_motion.o $(BUILD_DIR)/vim_regex.o $(BUILD_DIR)/vim_ex.o $(BUILD_DIR)/vim_keys.o
TEST_DEPS_test_vim_engine = $(VIM_OBJS) $(BUILD_DIR)/text_tags.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o
TEST_DEPS_test_vim_parse = $(BUILD_DIR)/vim_parse.o $(BUILD_DIR)/vim_keys.o
TEST_DEPS_test_vim_regex = $(BUILD_DIR)/vim_regex.o $(BUILD_DIR)/vim_motion.o
TEST_DEPS_test_vim_ex = $(BUILD_DIR)/vim_ex.o
CODE_SCAN_OBJS = $(BUILD_DIR)/code_scan.o $(BUILD_DIR)/code_scan_c.o $(BUILD_DIR)/code_symbol.o
TEST_DEPS_test_code_scan = $(CODE_SCAN_OBJS)
TEST_DEPS_test_code_outline_tree = $(BUILD_DIR)/code_outline_tree.o
CODE_TAGS_OBJS = $(BUILD_DIR)/code_tags.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_code_index = $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_code_tags = $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_jump_list = $(BUILD_DIR)/jump_list.o
TEST_DEPS_test_trash_restore = $(BUILD_DIR)/trash_restore.o
TEST_DEPS_test_fuzzy_match = $(BUILD_DIR)/fuzzy_match.o
TEST_DEPS_test_web_address = $(BUILD_DIR)/web_address.o
TEST_DEPS_test_path_globs = $(BUILD_DIR)/path_globs.o
TEST_DEPS_test_session_store = $(BUILD_DIR)/session_store.o $(BUILD_DIR)/layout_store.o $(BUILD_DIR)/tab_tree.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_tab_tree = $(BUILD_DIR)/tab_tree.o
TEST_DEPS_test_settings_store = $(BUILD_DIR)/settings_store.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_text_search = $(BUILD_DIR)/text_search.o $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_palette = $(BUILD_DIR)/palette.o
TEST_DEPS_test_image_theme = $(BUILD_DIR)/image_theme.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_fonts = $(BUILD_DIR)/fonts.o
TEST_DEPS_test_theme_store = $(BUILD_DIR)/theme_store.o $(BUILD_DIR)/theme_resources.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_text_tags = $(BUILD_DIR)/text_tags.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o
LSP_PROTOCOL_OBJS = $(BUILD_DIR)/lsp_protocol.o $(BUILD_DIR)/code_symbol.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_lsp_protocol = $(LSP_PROTOCOL_OBJS)
TEST_DEPS_test_ole_reader = $(BUILD_DIR)/ole_reader.o
TEST_DEPS_test_gif_decoder = $(BUILD_DIR)/gif_decoder.o
TEST_DEPS_test_office97 = $(PREVIEW_FORMAT_OBJS)
TEST_DEPS_test_mail = $(PREVIEW_FORMAT_OBJS)
TEST_DEPS_test_lsp_config = $(BUILD_DIR)/lsp_config.o $(BUILD_DIR)/code_symbol.o
TEST_DEPS_test_lsp_client = $(BUILD_DIR)/lsp_client.o $(LSP_PROTOCOL_OBJS)

# Preview benchmark and coverage report (tools/preview_tool.c, docs/previews.md)
$(BUILD_DIR)/preview_tool: tools/preview_tool.c $(PREVIEW_FORMAT_OBJS) $(FLAGS_STAMP) | $(BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(DEPFLAGS) -o $@ $< $(PREVIEW_FORMAT_OBJS) $(TEST_LDFLAGS)

# Times every structured preview on the fixtures and large generated files
bench-preview: $(BUILD_DIR)/preview_tool
	@sh tools/bench_preview.sh $(BUILD_DIR)/preview_tool

# Share of files that end at the hex dump: make preview-coverage DIR=~/
preview-coverage: $(BUILD_DIR)/preview_tool
	@$(BUILD_DIR)/preview_tool coverage $${DIR:-$$HOME}

.SECONDEXPANSION:
$(BUILD_DIR)/tests/%: tests/%.c $$(TEST_DEPS_%) $(FLAGS_STAMP) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) $(DEPFLAGS) -o $@ $< $(TEST_DEPS_$*) $(TEST_LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; $$t || exit 1; done
	@echo "All tests passed."

# Full validation for agents: zero-warning build, unit tests, module layering
check:
	@$(MAKE) --no-print-directory all > $(BUILD_DIR).log 2>&1; rc=$$?; cat $(BUILD_DIR).log; \
	if [ $$rc -ne 0 ]; then rm -f $(BUILD_DIR).log; echo "check: build failed"; exit 1; fi; \
	if grep -q "warning:" $(BUILD_DIR).log; then rm -f $(BUILD_DIR).log; echo "check: compiler warnings found"; exit 1; fi; \
	rm -f $(BUILD_DIR).log
	@$(MAKE) --no-print-directory test
	@sh tools/check_layers.sh

# Fast headless UI smoke test (about a second; needs Xvfb, xdotool)
ui-test: $(TARGET)
	@sh tests/ui/smoke.sh ./$(TARGET)

# Full screenshot scenario (needs Xvfb, xdotool, ImageMagick). Screenshots go to
# build/ui/shots; set UI_BASELINE=<dir of shots> to compare with a reference run.
ui-shots: $(TARGET)
	@sh tests/ui/run_ui_test.sh ./$(TARGET)

# Instrumented build (AddressSanitizer + UBSan) in build-asan/: a crash or a
# memory error prints its stack. Run build-asan/lookinfiles and keep its output.
ASAN_DIR = build-asan
asan:
	@$(MAKE) --no-print-directory BUILD_DIR=$(ASAN_DIR) TARGET=$(ASAN_DIR)/lookinfiles$(EXE) \
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" $(ASAN_DIR)/lookinfiles$(EXE)

clean:
	rm -rf $(TARGET) lookinfiles lookinfiles.exe $(BUILD_DIR) $(ASAN_DIR)

# Installation (make install PREFIX=/usr DESTDIR=pkgroot for packages):
# the binary, its desktop entry, AppStream metainfo and icon (data/)
APP_ID = io.github._80c01.LookinFiles
BINDIR = $(PREFIX)/bin
INSTALLED_DATA = applications/$(APP_ID).desktop metainfo/$(APP_ID).metainfo.xml icons/hicolor/scalable/apps/$(APP_ID).svg icons/hicolor/256x256/apps/$(APP_ID).png

install: $(TARGET) translations
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@for mo in $(wildcard $(MO_FILES)); do \
	  lang=$${mo#$(BUILD_DIR)/locale/}; lang=$${lang%%/*}; \
	  echo install -Dm644 $$mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/$(GETTEXT_DOMAIN).mo; \
	  install -Dm644 $$mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/$(GETTEXT_DOMAIN).mo || exit 1; \
	done
	install -Dm644 data/$(APP_ID).desktop $(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop
	install -Dm644 data/$(APP_ID).metainfo.xml $(DESTDIR)$(DATADIR)/metainfo/$(APP_ID).metainfo.xml
	install -Dm644 data/icons/hicolor/scalable/apps/$(APP_ID).svg $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/$(APP_ID).svg
	install -Dm644 data/icons/hicolor/256x256/apps/$(APP_ID).png $(DESTDIR)$(DATADIR)/icons/hicolor/256x256/apps/$(APP_ID).png

# Source archive of the last commit (uncommitted changes are left out)
DIST = $(TARGET)-$(VERSION)
dist:
	git archive --format=tar.gz --prefix=$(DIST)/ -o $(DIST).tar.gz HEAD
	@echo "Created $(DIST).tar.gz"

# Package shortcuts (packaging/): builds a .deb or AppImage in build/
deb:
	@sh packaging/debian/build_deb.sh build

appimage:
	@sh packaging/appimage/build_appimage.sh build

windows-zip:
	@sh packaging/windows/build_windows_zip.sh build

# A single .exe holding the whole portable zip (packaging/windows/launcher.c)
windows-exe:
	@bash packaging/windows/build_windows_exe.sh build

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET) $(INSTALLED_DATA:%=$(DESTDIR)$(DATADIR)/%)
	rm -f $(LINGUAS:%=$(DESTDIR)$(LOCALEDIR)/%/LC_MESSAGES/$(GETTEXT_DOMAIN).mo)

-include $(DEPS) $(TEST_BINS:=.d) $(BUILD_DIR)/preview_tool.d

.PHONY: all translations pot po-update asan deps clean dist install uninstall test check ui-test ui-shots bench-preview preview-coverage deb appimage windows-zip windows-exe

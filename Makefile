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

# Required: GTK 4 and libgit2 (git status, history and file lists, src/git/git_utils.c)
APP_CFLAGS = -Wall -Wextra -Isrc `$(PKG_CONFIG) --cflags gtk4 libgit2`
LIBS = `$(PKG_CONFIG) --libs gtk4 libgit2` -lm
DEPFLAGS = -MMD -MP

# Optional embedded terminal: enabled automatically when vte-2.91-gtk4 is installed.
# On Windows (no VTE) the built-in terminal is used instead (src/terminal/term_widget.c
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

# Optional ZSTD-compressed Parquet pages: enabled automatically when libzstd is installed
HAVE_ZSTD != $(PKG_CONFIG) --exists libzstd 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_ZSTD),yes)
APP_CFLAGS += -DHAVE_ZSTD `$(PKG_CONFIG) --cflags libzstd`
LIBS += `$(PKG_CONFIG) --libs libzstd`
endif

# Optional fast JPEG decoding at the size shown (src/media/jpeg_decode.c):
# enabled automatically when libjpeg (libjpeg-turbo) is installed
HAVE_LIBJPEG != $(PKG_CONFIG) --exists libjpeg 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_LIBJPEG),yes)
APP_CFLAGS += -DHAVE_LIBJPEG `$(PKG_CONFIG) --cflags libjpeg`
LIBS += `$(PKG_CONFIG) --libs libjpeg`
endif

# Optional browser tabs: enabled automatically when webkitgtk-6.0 is installed.
# WebKit is not linked into the program: src/preview/web_engine_webkit.c is
# built as a module, build/lookinfiles-webkit.so, opened at the first browser
# tab (src/preview/web_engine.c; about 30 MB and 30 ms of start-up saved).
HAVE_WEBKIT != $(PKG_CONFIG) --exists webkitgtk-6.0 2>/dev/null && echo yes || echo no
MODULE_SRCS = src/preview/web_engine_webkit.c
WEBKIT_MODULE =
ifeq ($(HAVE_WEBKIT),yes)
APP_CFLAGS += -DHAVE_WEBKIT `$(PKG_CONFIG) --cflags gmodule-2.0`
LIBS += `$(PKG_CONFIG) --libs gmodule-2.0`
WEBKIT_CFLAGS = `$(PKG_CONFIG) --cflags webkitgtk-6.0`
WEBKIT_LIBS = `$(PKG_CONFIG) --libs gtk4 webkitgtk-6.0`
WEBKIT_MODULE = $(BUILD_DIR)/lookinfiles-webkit.so
endif

# Browser tabs on Windows (no WebKitGTK there): Microsoft WebView2, enabled
# automatically when its SDK is in WEBVIEW2_DIR (packaging/windows/fetch_webview2.sh).
# Only WebView2.h is used to build; WebView2Loader.dll is loaded at run time from
# the executable's folder (make windows-zip copies it there).
WEBVIEW2_DIR ?= deps/webview2
HAVE_WEBVIEW2 = no
ifeq ($(OS),Windows_NT)
ifneq ($(HAVE_WEBKIT),yes)
ifneq ($(wildcard $(WEBVIEW2_DIR)/build/native/include/WebView2.h),)
HAVE_WEBVIEW2 = yes
APP_CFLAGS += -DHAVE_WEBVIEW2 -isystem $(WEBVIEW2_DIR)/build/native/include
LIBS += -lole32
endif
endif
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
# the translations are installed (src/core/i18n.c looks next to it first)
PREFIX ?= /usr/local
DATADIR = $(PREFIX)/share
LOCALEDIR = $(DATADIR)/locale
APP_CFLAGS += -DLOCALEDIR=\"$(LOCALEDIR)\"
# Modules opened at run time (src/core/module_path.c)
MODULEDIR = $(PREFIX)/lib/lookinfiles
APP_CFLAGS += -DMODULEDIR=\"$(MODULEDIR)\"

# Release version: can be overridden via make VERSION=... or environment.
# Defaults to git tags (e.g. v0.4.2 -> 0.4.2), falling back to src/core/app_paths.h.
VERSION != sh tools/version.sh 2>/dev/null || sed -n 's/^\#define APP_VERSION *"\(.*\)"/\1/p' src/core/app_paths.h 2>/dev/null || echo "0.6.0"
APP_CFLAGS += -DAPP_VERSION=\"$(VERSION)\"

# Windows platform detection
ifeq ($(OS),Windows_NT)
EXE = .exe
# -mwindows suppresses the background cmd terminal window in release builds
WINDOWS_SUBSYSTEM ?= -mwindows
ALL_LDFLAGS += $(WINDOWS_SUBSYSTEM)
# System Monitor (sysstat_host.c): network table, process memory, accounts
LIBS += -liphlpapi -lpsapi -ladvapi32
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

# Every .c file in src/<dir>/ is part of the application: adding a module only
# requires creating src/<dir>/<module>.c and its .h. Module names are unique,
# so the objects stay flat in build/ (build/<module>.o).
SRCS = $(filter-out $(MODULE_SRCS),$(sort $(wildcard src/*.c src/*/*.c)))
OBJS = $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.c=.o))) $(BUILD_DIR)/icon_resources.o $(BUILD_DIR)/guide_resources.o $(BUILD_DIR)/theme_resources.o $(BUILD_DIR)/jupyter_resources.o

# Toolbar icons bundled into the binary (icons/, see icons/COPYING and src/theme/icons.c)
GLIB_COMPILE_RESOURCES != $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0
ICON_FILES = $(sort $(wildcard icons/scalable/actions/*.svg icons/sets/*/*.svg))
DEPS = $(OBJS:.o=.d)

all: $(TARGET) $(WEBKIT_MODULE) translations

$(TARGET): $(OBJS) $(FLAGS_STAMP)
	$(CC) $(ALL_LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)
	@PKG_CONFIG=$(PKG_CONFIG) WEBVIEW2_DIR=$(WEBVIEW2_DIR) sh tools/check_deps.sh || true

# The WebKit engine module: its own position-independent objects, only the
# table of web_engine_module.h exported (src/preview/web_engine_module.h)
WEBKIT_MODULE_SRCS = src/preview/web_engine_webkit.c src/preview/web_address.c
WEBKIT_MODULE_HDRS = src/preview/web_engine_module.h src/preview/web_engine.h src/preview/web_address.h src/core/i18n.h
$(BUILD_DIR)/lookinfiles-webkit.so: $(WEBKIT_MODULE_SRCS) $(WEBKIT_MODULE_HDRS) $(FLAGS_STAMP) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $(WEBKIT_CFLAGS) -fPIC -fvisibility=hidden -shared -o $@ $(WEBKIT_MODULE_SRCS) $(ALL_LDFLAGS) $(WEBKIT_LIBS)

# Lists the required and optional libraries found (tools/check_deps.sh)
deps:
	@PKG_CONFIG=$(PKG_CONFIG) WEBVIEW2_DIR=$(WEBVIEW2_DIR) sh tools/check_deps.sh

# One static pattern rule per directory of src/: each object is bound to its
# own source (no search path, so an old path in build/*.d cannot win)
define module_dir_rule
$(patsubst $(1)%.c,$(BUILD_DIR)/%.o,$(wildcard $(1)*.c)): $(BUILD_DIR)/%.o: $(1)%.c $(FLAGS_STAMP) | $(BUILD_DIR)
	$$(CC) $$(ALL_CFLAGS) $$(DEPFLAGS) -c -o $$@ $$<
endef
$(foreach d,$(sort $(dir $(SRCS))),$(eval $(call module_dir_rule,$(d))))

# A source moved to another directory of src/ keeps its old path in build/*.d
# until it is rebuilt: the old path is not an error (-MP does the same for headers)
src/%.c: ;

$(BUILD_DIR)/icon_resources.c: icons/lookin.gresource.xml $(ICON_FILES) | $(BUILD_DIR)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=icons --generate-source --target=$@ $<

$(BUILD_DIR)/icon_resources.o: $(BUILD_DIR)/icon_resources.c $(FLAGS_STAMP)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# User guide bundled into the binary (src/chrome/user_guide.c, the Help tab): every
# page of docs/user-guide/ and the reference pages it links to. The resource
# list is generated, so a new page only needs its .md file.
GUIDE_FILES = $(sort $(wildcard docs/user-guide/*.md)) docs/keyboard.md docs/previews.md docs/configuration.md
# Translated pages (docs/l10n/<lang>/user-guide/), served as <lang>/user-guide/...
GUIDE_L10N_FILES = $(sort $(wildcard docs/l10n/*/user-guide/*.md))

$(BUILD_DIR)/guide.gresource.xml: $(GUIDE_FILES) $(GUIDE_L10N_FILES) Makefile | $(BUILD_DIR)
	@{ echo '<?xml version="1.0" encoding="UTF-8"?>'; \
	  echo '<gresources>'; \
	  echo '  <gresource prefix="/org/lookin/guide">'; \
	  for f in $(GUIDE_FILES:docs/%=%); do echo "    <file>$$f</file>"; done; \
	  echo '  </gresource>'; \
	  echo '  <gresource prefix="/org/lookin/guide-l10n">'; \
	  for f in $(GUIDE_L10N_FILES:docs/%=%); do echo "    <file alias=\"$${f#l10n/}\">$$f</file>"; done; \
	  echo '  </gresource>'; \
	  echo '</gresources>'; } > $@

$(BUILD_DIR)/guide_resources.c: $(BUILD_DIR)/guide.gresource.xml $(GUIDE_FILES) $(GUIDE_L10N_FILES)
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

# The Jupyter bridge (data/jupyter/ipynb_bridge.py, run by src/jupyter/ipynb_tab.c
# as python3 -c <script>) bundled into the binary
JUPYTER_FILES = data/jupyter/ipynb_bridge.py

$(BUILD_DIR)/jupyter.gresource.xml: Makefile | $(BUILD_DIR)
	@{ echo '<?xml version="1.0" encoding="UTF-8"?>'; \
	  echo '<gresources>'; \
	  echo '  <gresource prefix="/org/lookin/jupyter">'; \
	  for f in $(JUPYTER_FILES:data/jupyter/%=%); do echo "    <file>$$f</file>"; done; \
	  echo '  </gresource>'; \
	  echo '</gresources>'; } > $@

$(BUILD_DIR)/jupyter_resources.c: $(BUILD_DIR)/jupyter.gresource.xml $(JUPYTER_FILES)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=data/jupyter --c-name jupyter --generate-source --target=$@ $<

$(BUILD_DIR)/jupyter_resources.o: $(BUILD_DIR)/jupyter_resources.c $(FLAGS_STAMP)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

# Translations (src/core/i18n.h, po/): po/<lang>.po compiled to
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
	@echo "xgettext -> po/$(GETTEXT_DOMAIN).pot"
	@$(XGETTEXT) $(XGETTEXT_FLAGS) -o po/$(GETTEXT_DOMAIN).pot $(sort $(wildcard src/*.c src/*.h src/*/*.c src/*/*.h))

po-update: pot
	@for po in $(PO_FILES); do echo "== $$po"; $(MSGMERGE) --update --backup=none $$po po/$(GETTEXT_DOMAIN).pot || exit 1; done

# Unit tests (GLib g_test, no display needed): tests/test_<module>.c links
# against the listed objects only. Modules under test must not create widgets.
# Tests use the application CFLAGS so that AppData has the same layout as in
# the objects under test. Register new tests in TESTS and TEST_DEPS_<name>.
TEST_CFLAGS = $(ALL_CFLAGS)
TEST_LDFLAGS = $(ALL_LDFLAGS) $(LIBS)
TESTS = test_app_paths test_i18n test_file_types test_file_type_icons test_notebook_icons test_git_utils test_git_refs test_git_worktree test_git_stats test_git_repo_info test_git_stash_tags test_remote_store test_remote_mounts test_preview_summary test_explorer_row test_layout_store test_layout_history_stack test_line_diff test_latex_log test_latex_root test_latex_synctex test_pane_geometry test_split_ratio test_minimap_geometry test_dir_summary test_dir_scan test_file_save test_editor_stamp test_hex_dump test_elf_info test_zip_reader test_docx test_doc_writer test_doc_pages test_odt_reader test_preview_doc test_csv test_json_view test_data_tree test_xml_view test_text_outline test_toml_view test_yaml_view test_diff_view test_side_diff test_archive_listing test_archive_extract test_cert_view test_card_view test_log_view test_exif test_font_info test_man_view test_notebook test_ipynb_doc test_ipynb_kernel test_ipynb_outline test_spreadsheet test_odf test_pptx test_slide_render test_odp_reader test_epub test_rtf test_sqlite_view test_gettext_view test_class_info test_wasm_info test_torrent_info test_pyc_info test_tfm_info test_pcap_info test_parquet_view test_media_tags test_media_tracks test_text_encoding test_preview_formats test_org_time test_org_doc test_org_table test_dunnet test_vim_engine test_vim_parse test_vim_regex test_vim_ex test_code_scan test_code_outline_tree test_code_index test_code_tags test_jump_list test_lsp_protocol test_lsp_edit test_code_lint test_lsp_client test_lsp_config test_ole_reader test_office97 test_mail test_gif_decoder test_palette test_image_theme test_fonts test_theme_store test_text_tags test_preview_cache test_path_kind test_nav_keys test_trash_restore test_fuzzy_match test_text_search test_path_globs test_session_store test_tab_tree test_settings_store test_note_types test_html_text test_tex_math test_md_out test_rich_parse test_notes_store test_canvas_doc test_canvas_geometry test_canvas_history test_user_guide test_user_guide_search test_web_address test_web_keys test_ps_convert test_vt_screen test_vt_pty test_ui_probe test_preview_state test_git_state test_sysstat test_sysstat_host test_proc_query test_countdown test_run_time test_jpeg_decode test_profile test_module_path test_memory_trim test_png_encode test_disk_scan test_git_space test_git_filter test_git_graph test_treemap_layout test_date_bins test_sunburst_layout test_deep_query test_deep_search test_deep_search_store test_folder_complete test_xml_text
TEST_BINS = $(TESTS:%=$(BUILD_DIR)/tests/%)
TEST_DEPS_test_app_paths = $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_i18n = $(BUILD_DIR)/i18n.o
TEST_DEPS_test_file_types = $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_file_type_icons = $(BUILD_DIR)/file_type_icons.o
TEST_DEPS_test_notebook_icons = $(BUILD_DIR)/notebook_icons.o
TEST_DEPS_test_git_utils = $(BUILD_DIR)/git_log.o $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_git_refs = $(BUILD_DIR)/git_refs.o
TEST_DEPS_test_git_worktree = $(BUILD_DIR)/git_worktree.o $(BUILD_DIR)/git_lib.o
TEST_DEPS_test_git_stats = $(BUILD_DIR)/git_stats.o $(BUILD_DIR)/git_lib.o
TEST_DEPS_test_git_repo_info = $(BUILD_DIR)/git_repo_info.o $(BUILD_DIR)/git_lib.o
TEST_DEPS_test_git_stash_tags = $(BUILD_DIR)/git_stash_tags.o $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/i18n.o
TEST_DEPS_test_remote_store = $(BUILD_DIR)/remote_store.o $(BUILD_DIR)/remote_mounts.o $(BUILD_DIR)/app_paths.o $(BUILD_DIR)/notebook_icons.o
TEST_DEPS_test_remote_mounts = $(BUILD_DIR)/remote_mounts.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_preview_summary = $(BUILD_DIR)/preview_summary.o $(BUILD_DIR)/explorer_row.o $(BUILD_DIR)/note_types.o $(BUILD_DIR)/canvas_doc.o $(BUILD_DIR)/deep_search_store.o $(BUILD_DIR)/app_paths.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/i18n.o
TEST_DEPS_test_note_types = $(BUILD_DIR)/note_types.o
TEST_DEPS_test_html_text = $(BUILD_DIR)/html_text.o
TEST_DEPS_test_tex_math = $(BUILD_DIR)/tex_math.o $(BUILD_DIR)/tex_symbols.o
TEST_DEPS_test_md_out = $(BUILD_DIR)/md_out.o $(BUILD_DIR)/markdown_parse.o $(BUILD_DIR)/markdown_table.o $(BUILD_DIR)/markdown_link.o $(BUILD_DIR)/image_utils.o $(BUILD_DIR)/i18n.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o
TEST_DEPS_test_rich_parse = $(TEST_DEPS_test_md_out) $(BUILD_DIR)/md_show.o $(BUILD_DIR)/math_paintable.o $(BUILD_DIR)/tex_math.o $(BUILD_DIR)/tex_symbols.o $(BUILD_DIR)/html.o $(BUILD_DIR)/html_text.o $(BUILD_DIR)/org_render.o $(BUILD_DIR)/org_doc.o $(BUILD_DIR)/org_list.o $(BUILD_DIR)/org_time.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/icons.o $(BUILD_DIR)/file_type_icons.o
TEST_DEPS_test_notes_store = $(BUILD_DIR)/notes_store.o $(BUILD_DIR)/note_types.o $(BUILD_DIR)/notebook_icons.o
TEST_DEPS_test_canvas_doc = $(BUILD_DIR)/canvas_doc.o
TEST_DEPS_test_canvas_geometry = $(BUILD_DIR)/canvas_geometry.o
TEST_DEPS_test_canvas_history = $(BUILD_DIR)/canvas_history.o
TEST_DEPS_test_user_guide = $(BUILD_DIR)/user_guide.o $(BUILD_DIR)/guide_resources.o
TEST_DEPS_test_user_guide_search = $(BUILD_DIR)/user_guide_search.o $(BUILD_DIR)/user_guide.o $(BUILD_DIR)/guide_resources.o
TEST_DEPS_test_explorer_row = $(BUILD_DIR)/explorer_row.o
TEST_DEPS_test_layout_store = $(BUILD_DIR)/layout_store.o $(BUILD_DIR)/tab_tree.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_layout_history_stack = $(BUILD_DIR)/layout_history_stack.o $(BUILD_DIR)/layout_store.o $(BUILD_DIR)/tab_tree.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_line_diff = $(BUILD_DIR)/line_diff.o
TEST_DEPS_test_latex_log = $(BUILD_DIR)/latex_log.o
TEST_DEPS_test_latex_root = $(BUILD_DIR)/latex_root.o
TEST_DEPS_test_latex_synctex = $(BUILD_DIR)/latex_synctex.o
TEST_DEPS_test_preview_cache = $(BUILD_DIR)/preview_cache.o
TEST_DEPS_test_path_kind = $(BUILD_DIR)/path_kind.o
TEST_DEPS_test_nav_keys = $(BUILD_DIR)/nav_keys.o
TEST_DEPS_test_pane_geometry = $(BUILD_DIR)/pane_geometry.o
TEST_DEPS_test_split_ratio = $(BUILD_DIR)/split_ratio.o
TEST_DEPS_test_minimap_geometry = $(BUILD_DIR)/minimap_geometry.o
TEST_DEPS_test_ui_probe = $(BUILD_DIR)/ui_probe.o
TEST_DEPS_test_profile = $(BUILD_DIR)/profile.o
TEST_DEPS_test_module_path = $(BUILD_DIR)/module_path.o
TEST_DEPS_test_png_encode = $(BUILD_DIR)/png_encode.o
TEST_DEPS_test_memory_trim = $(BUILD_DIR)/memory_trim.o $(BUILD_DIR)/profile.o
TEST_DEPS_test_sysstat = $(BUILD_DIR)/sysstat.o
TEST_DEPS_test_sysstat_host = $(BUILD_DIR)/sysstat_host.o $(BUILD_DIR)/sysstat.o
TEST_DEPS_test_proc_query = $(BUILD_DIR)/proc_query.o
TEST_DEPS_test_countdown = $(BUILD_DIR)/countdown.o
TEST_DEPS_test_run_time = $(BUILD_DIR)/run_time.o
TEST_DEPS_test_disk_scan = $(BUILD_DIR)/disk_scan.o $(BUILD_DIR)/disk_stats.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/i18n.o
TEST_DEPS_test_git_space = $(BUILD_DIR)/git_space.o $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/disk_scan.o
TEST_DEPS_test_git_filter = $(BUILD_DIR)/git_filter.o $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/disk_scan.o
TEST_DEPS_test_git_graph = $(BUILD_DIR)/git_graph.o
TEST_DEPS_test_treemap_layout = $(BUILD_DIR)/treemap_layout.o
TEST_DEPS_test_date_bins = $(BUILD_DIR)/date_bins.o
TEST_DEPS_test_sunburst_layout = $(BUILD_DIR)/sunburst_layout.o
TEST_DEPS_test_preview_state = $(BUILD_DIR)/preview_state.o
TEST_DEPS_test_git_state = $(BUILD_DIR)/git_state.o
TEST_DEPS_test_dir_summary = $(BUILD_DIR)/dir_summary.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_file_save = $(BUILD_DIR)/file_save.o
TEST_DEPS_test_editor_stamp = $(BUILD_DIR)/editor_stamp.o $(BUILD_DIR)/preview_cache.o
TEST_DEPS_test_dir_scan = $(BUILD_DIR)/dir_scan.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_ps_convert = $(BUILD_DIR)/ps_convert.o
TEST_DEPS_test_vt_screen = $(BUILD_DIR)/vt_screen.o
TEST_DEPS_test_vt_pty = $(BUILD_DIR)/vt_pty.o
TEST_DEPS_test_hex_dump = $(BUILD_DIR)/hex_dump.o
TEST_DEPS_test_elf_info = $(BUILD_DIR)/elf_info.o
TEST_DEPS_test_zip_reader = $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_docx = $(BUILD_DIR)/docx.o $(BUILD_DIR)/docx_styles.o $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/doc_pages.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_preview_doc = $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_doc_writer = $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_doc_pages = $(BUILD_DIR)/doc_pages.o $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_odt_reader = $(BUILD_DIR)/odt_reader.o $(BUILD_DIR)/doc_pages.o $(BUILD_DIR)/doc_writer.o $(BUILD_DIR)/odp_styles.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_json_view = $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_xml_view = $(BUILD_DIR)/xml_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_data_tree = $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_text_outline = $(BUILD_DIR)/text_outline.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_toml_view = $(BUILD_DIR)/toml_view.o $(BUILD_DIR)/text_outline.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_yaml_view = $(BUILD_DIR)/yaml_view.o $(BUILD_DIR)/text_outline.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_diff_view = $(BUILD_DIR)/diff_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_side_diff = $(BUILD_DIR)/side_diff.o
TEST_DEPS_test_archive_listing = $(BUILD_DIR)/archive_listing.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_archive_extract = $(BUILD_DIR)/archive_extract.o $(BUILD_DIR)/zip_reader.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_cert_view = $(BUILD_DIR)/cert_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_card_view = $(BUILD_DIR)/card_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_log_view = $(BUILD_DIR)/log_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_exif = $(BUILD_DIR)/exif.o
TEST_DEPS_test_font_info = $(BUILD_DIR)/font_info.o
TEST_DEPS_test_man_view = $(BUILD_DIR)/man_view.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_notebook = $(BUILD_DIR)/notebook.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_ipynb_kernel = $(BUILD_DIR)/ipynb_kernel.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_ipynb_outline = $(BUILD_DIR)/ipynb_outline.o $(BUILD_DIR)/ipynb_doc.o $(BUILD_DIR)/code_outline_tree.o $(BUILD_DIR)/notebook.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_ipynb_doc = $(BUILD_DIR)/ipynb_doc.o $(BUILD_DIR)/notebook.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_spreadsheet = $(BUILD_DIR)/spreadsheet.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_odf = $(BUILD_DIR)/odf.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_pptx = $(BUILD_DIR)/pptx.o $(BUILD_DIR)/pptx_shapes.o $(BUILD_DIR)/pptx_build.o $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/png_encode.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
TEST_DEPS_test_slide_render = $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/png_encode.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_odp_reader = $(BUILD_DIR)/odp_reader.o $(BUILD_DIR)/odp_styles.o $(BUILD_DIR)/odp_geometry.o $(BUILD_DIR)/odp_text.o $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/png_encode.o $(BUILD_DIR)/preview_doc.o $(BUILD_DIR)/zip_xml.o $(BUILD_DIR)/zip_reader.o
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
TEST_DEPS_test_parquet_view = $(BUILD_DIR)/parquet_view.o $(BUILD_DIR)/parquet_pages.o $(BUILD_DIR)/parquet_meta.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_media_tags = $(BUILD_DIR)/media_tags.o
TEST_DEPS_test_media_tracks = $(BUILD_DIR)/media_tracks.o
TEST_DEPS_test_text_encoding = $(BUILD_DIR)/text_encoding.o
TEST_DEPS_test_csv = $(BUILD_DIR)/csv.o $(BUILD_DIR)/preview_doc.o
# Modules of the structured previews (preview_formats and the parsers it calls)
PREVIEW_MODULES = preview_formats preview_doc doc_writer doc_pages odt_reader file_types text_encoding hex_dump elf_info zip_reader docx docx_styles csv json_view data_tree xml_view text_outline toml_view yaml_view diff_view archive_listing cert_view card_view log_view font_info man_view notebook zip_xml spreadsheet odf odp_reader odp_styles odp_geometry odp_text pptx pptx_shapes pptx_build slide_render png_encode epub rtf sqlite_view gettext_view class_info wasm_info torrent_info pyc_info tfm_info pcap_info parquet_meta parquet_pages parquet_view ole_reader ole_props office97 doc_reader xls_reader ppt_reader mail_message mail_view mail_box mail_dir mail_msg mail_tnef mail_store
PREVIEW_FORMAT_OBJS = $(PREVIEW_MODULES:%=$(BUILD_DIR)/%.o)
TEST_DEPS_test_preview_formats = $(PREVIEW_FORMAT_OBJS)
TEST_DEPS_test_org_time = $(BUILD_DIR)/org_time.o
TEST_DEPS_test_org_doc = $(BUILD_DIR)/org_doc.o $(BUILD_DIR)/org_list.o $(BUILD_DIR)/org_time.o
TEST_DEPS_test_org_table = $(BUILD_DIR)/org_table.o $(BUILD_DIR)/org_doc.o $(BUILD_DIR)/org_list.o $(BUILD_DIR)/org_time.o
TEST_DEPS_test_dunnet = $(BUILD_DIR)/dunnet.o
VIM_OBJS = $(BUILD_DIR)/vim_engine.o $(BUILD_DIR)/vim_engine_motions.o $(BUILD_DIR)/vim_engine_insert.o $(BUILD_DIR)/vim_engine_ops.o $(BUILD_DIR)/vim_engine_normal.o $(BUILD_DIR)/vim_engine_visual.o $(BUILD_DIR)/vim_engine_ex.o $(BUILD_DIR)/vim_parse.o $(BUILD_DIR)/vim_motion.o $(BUILD_DIR)/vim_regex.o $(BUILD_DIR)/vim_ex.o $(BUILD_DIR)/vim_keys.o
TEST_DEPS_test_vim_engine = $(VIM_OBJS) $(BUILD_DIR)/text_tags.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o
TEST_DEPS_test_vim_parse = $(BUILD_DIR)/vim_parse.o $(BUILD_DIR)/vim_keys.o
TEST_DEPS_test_vim_regex = $(BUILD_DIR)/vim_regex.o $(BUILD_DIR)/vim_motion.o
TEST_DEPS_test_vim_ex = $(BUILD_DIR)/vim_ex.o
CODE_SCAN_OBJS = $(BUILD_DIR)/code_scan.o $(BUILD_DIR)/code_scan_c.o $(BUILD_DIR)/code_symbol.o
TEST_DEPS_test_code_scan = $(CODE_SCAN_OBJS)
TEST_DEPS_test_code_outline_tree = $(BUILD_DIR)/code_outline_tree.o
CODE_TAGS_OBJS = $(BUILD_DIR)/code_tags.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_code_index = $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_code_tags = $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_jump_list = $(BUILD_DIR)/jump_list.o
TEST_DEPS_test_trash_restore = $(BUILD_DIR)/trash_restore.o
TEST_DEPS_test_fuzzy_match = $(BUILD_DIR)/fuzzy_match.o
TEST_DEPS_test_folder_complete = $(BUILD_DIR)/folder_complete.o
TEST_DEPS_test_web_address = $(BUILD_DIR)/web_address.o
TEST_DEPS_test_web_keys = $(BUILD_DIR)/web_keys.o
TEST_DEPS_test_path_globs = $(BUILD_DIR)/path_globs.o
TEST_DEPS_test_xml_text = $(BUILD_DIR)/xml_text.o
TEST_DEPS_test_session_store = $(BUILD_DIR)/session_store.o $(BUILD_DIR)/layout_store.o $(BUILD_DIR)/tab_tree.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_tab_tree = $(BUILD_DIR)/tab_tree.o
TEST_DEPS_test_settings_store = $(BUILD_DIR)/settings_store.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_text_search = $(BUILD_DIR)/text_search.o $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_deep_query = $(BUILD_DIR)/deep_query.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/i18n.o $(BUILD_DIR)/path_globs.o $(BUILD_DIR)/text_search.o $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_deep_search = $(BUILD_DIR)/deep_search.o $(BUILD_DIR)/deep_query.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/i18n.o $(BUILD_DIR)/path_globs.o $(BUILD_DIR)/text_search.o $(BUILD_DIR)/code_index.o $(CODE_SCAN_OBJS) $(CODE_TAGS_OBJS) $(BUILD_DIR)/git_utils.o $(BUILD_DIR)/git_lib.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_deep_search_store = $(BUILD_DIR)/deep_search_store.o $(BUILD_DIR)/app_paths.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/i18n.o
TEST_DEPS_test_palette = $(BUILD_DIR)/palette.o
TEST_DEPS_test_image_theme = $(BUILD_DIR)/image_theme.o $(BUILD_DIR)/palette.o
TEST_DEPS_test_fonts = $(BUILD_DIR)/fonts.o
TEST_DEPS_test_theme_store = $(BUILD_DIR)/theme_store.o $(BUILD_DIR)/theme_resources.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o $(BUILD_DIR)/app_paths.o
TEST_DEPS_test_text_tags = $(BUILD_DIR)/text_tags.o $(BUILD_DIR)/palette.o $(BUILD_DIR)/fonts.o
LSP_PROTOCOL_OBJS = $(BUILD_DIR)/lsp_protocol.o $(BUILD_DIR)/code_symbol.o $(BUILD_DIR)/json_view.o $(BUILD_DIR)/data_tree.o $(BUILD_DIR)/preview_doc.o
TEST_DEPS_test_lsp_protocol = $(LSP_PROTOCOL_OBJS)
TEST_DEPS_test_lsp_edit = $(BUILD_DIR)/lsp_edit.o $(LSP_PROTOCOL_OBJS)
TEST_DEPS_test_code_lint = $(BUILD_DIR)/code_lint.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/toml_view.o $(BUILD_DIR)/yaml_view.o $(BUILD_DIR)/xml_view.o $(BUILD_DIR)/text_outline.o $(LSP_PROTOCOL_OBJS)
TEST_DEPS_test_ole_reader = $(BUILD_DIR)/ole_reader.o
TEST_DEPS_test_gif_decoder = $(BUILD_DIR)/gif_decoder.o
TEST_DEPS_test_jpeg_decode = $(BUILD_DIR)/jpeg_decode.o
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

# Explorer and application benchmarks (docs/performance.md): make bench runs
# them all with the preview one; each fails when a result exceeds its budget
BENCH_TOOL_OBJS = $(BUILD_DIR)/dir_scan.o $(BUILD_DIR)/file_types.o $(BUILD_DIR)/text_encoding.o $(BUILD_DIR)/explorer_row.o $(BUILD_DIR)/profile.o $(BUILD_DIR)/slide_render.o $(BUILD_DIR)/png_encode.o $(BUILD_DIR)/preview_doc.o
$(BUILD_DIR)/bench_tool: tools/bench_tool.c $(BENCH_TOOL_OBJS) $(FLAGS_STAMP) | $(BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(DEPFLAGS) -o $@ $< $(BENCH_TOOL_OBJS) $(TEST_LDFLAGS)

# Listing and rows of a folder of 100 000 entries (BENCH_ENTRIES to change it)
bench-scan: $(BUILD_DIR)/bench_tool
	@$(BUILD_DIR)/bench_tool scan $(BUILD_DIR)/bench-scan $${BENCH_ENTRIES:-100000}

# Preview of a presentation full of photos (slide_render)
bench-slides: $(BUILD_DIR)/bench_tool
	@$(BUILD_DIR)/bench_tool slides

# Start-up, idle cost and browsing of the application under Xvfb
bench-app: $(TARGET)
	@sh tools/bench_app.sh ./$(TARGET)

bench:
	@status=0; for t in bench-scan bench-slides bench-preview bench-app; do \
		echo "== $$t"; $(MAKE) --no-print-directory $$t || status=1; done; exit $$status

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
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" $(ASAN_DIR)/lookinfiles$(EXE) \
		$(if $(WEBKIT_MODULE),$(ASAN_DIR)/lookinfiles-webkit.so)

clean:
	rm -rf $(TARGET) lookinfiles lookinfiles.exe $(BUILD_DIR) $(ASAN_DIR)

# Installation (make install PREFIX=/usr DESTDIR=pkgroot for packages):
# the binary, its desktop entry, AppStream metainfo and icon (data/)
APP_ID = io.github._80c01.LookinFiles
BINDIR = $(PREFIX)/bin
INSTALLED_DATA = applications/$(APP_ID).desktop metainfo/$(APP_ID).metainfo.xml icons/hicolor/scalable/apps/$(APP_ID).svg icons/hicolor/256x256/apps/$(APP_ID).png

install: $(TARGET) $(WEBKIT_MODULE) translations
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	$(if $(WEBKIT_MODULE),install -Dm755 $(WEBKIT_MODULE) $(DESTDIR)$(MODULEDIR)/lookinfiles-webkit.so)
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
	@WEBVIEW2_DIR=$(WEBVIEW2_DIR) sh packaging/windows/build_windows_zip.sh build

# A single .exe holding the whole portable zip (packaging/windows/launcher.c)
windows-exe:
	@bash packaging/windows/build_windows_exe.sh build

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET) $(INSTALLED_DATA:%=$(DESTDIR)$(DATADIR)/%)
	rm -f $(DESTDIR)$(MODULEDIR)/lookinfiles-webkit.so
	rm -f $(LINGUAS:%=$(DESTDIR)$(LOCALEDIR)/%/LC_MESSAGES/$(GETTEXT_DOMAIN).mo)

-include $(DEPS) $(TEST_BINS:=.d) $(BUILD_DIR)/preview_tool.d $(BUILD_DIR)/bench_tool.d

.PHONY: all translations pot po-update asan deps clean dist install uninstall test check ui-test ui-shots bench bench-scan bench-slides bench-app bench-preview preview-coverage deb appimage windows-zip windows-exe

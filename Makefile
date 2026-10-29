# Makefile for the GTK 4 file explorer
#
# Each source file is compiled to its own object in build/ with automatic
# header dependency tracking (-MMD -MP): editing a header rebuilds every
# file that includes it, and only those files.

CC = gcc
PKG_CONFIG ?= pkg-config

CFLAGS = -Wall -Wextra -Isrc `$(PKG_CONFIG) --cflags gtk4`
LDFLAGS = `$(PKG_CONFIG) --libs gtk4`
DEPFLAGS = -MMD -MP

# Optional embedded terminal: enabled automatically when vte-2.91-gtk4 is installed
HAVE_VTE != $(PKG_CONFIG) --exists vte-2.91-gtk4 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_VTE),yes)
CFLAGS += -DHAVE_VTE `$(PKG_CONFIG) --cflags vte-2.91-gtk4`
LDFLAGS += `$(PKG_CONFIG) --libs vte-2.91-gtk4`
endif

# Optional syntax highlighting: enabled automatically when gtksourceview-5 is installed
HAVE_GSV != $(PKG_CONFIG) --exists gtksourceview-5 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_GSV),yes)
CFLAGS += -DHAVE_GSV `$(PKG_CONFIG) --cflags gtksourceview-5`
LDFLAGS += `$(PKG_CONFIG) --libs gtksourceview-5`
endif

# Optional PDF rendering: enabled automatically when poppler-glib is installed
HAVE_POPPLER != $(PKG_CONFIG) --exists poppler-glib 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_POPPLER),yes)
CFLAGS += -DHAVE_POPPLER `$(PKG_CONFIG) --cflags poppler-glib`
LDFLAGS += `$(PKG_CONFIG) --libs poppler-glib`
endif

TARGET = explorateur_fichiers
BUILD_DIR = build

# Every .c file in src/ is part of the application: adding a module only
# requires creating src/<module>.c and src/<module>.h.
SRCS = $(sort $(wildcard src/*.c))
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o) $(BUILD_DIR)/icon_resources.o

# Toolbar icons bundled into the binary (icons/, see icons/COPYING and src/icons.c)
GLIB_COMPILE_RESOURCES != $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0
ICON_FILES = $(sort $(wildcard icons/scalable/actions/*.svg))
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/icon_resources.c: icons/explorateur.gresource.xml $(ICON_FILES) | $(BUILD_DIR)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=icons --generate-source --target=$@ $<

$(BUILD_DIR)/icon_resources.o: $(BUILD_DIR)/icon_resources.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

# Unit tests (GLib g_test, no display needed): tests/test_<module>.c links
# against the listed objects only. Modules under test must not create widgets.
# Tests use the application CFLAGS so that AppData has the same layout as in
# the objects under test. Register new tests in TESTS and TEST_DEPS_<name>.
TEST_CFLAGS = $(CFLAGS)
TEST_LDFLAGS = $(LDFLAGS)
TESTS = test_file_types test_git_utils test_remote_store test_preview_summary test_explorer_row test_layout_store test_line_diff test_pane_geometry test_dir_summary
TEST_BINS = $(TESTS:%=$(BUILD_DIR)/tests/%)
TEST_DEPS_test_file_types = $(BUILD_DIR)/file_types.o
TEST_DEPS_test_git_utils = $(BUILD_DIR)/git_utils.o
TEST_DEPS_test_remote_store = $(BUILD_DIR)/remote_store.o
TEST_DEPS_test_preview_summary = $(BUILD_DIR)/preview_summary.o $(BUILD_DIR)/explorer_row.o
TEST_DEPS_test_explorer_row = $(BUILD_DIR)/explorer_row.o
TEST_DEPS_test_layout_store = $(BUILD_DIR)/layout_store.o
TEST_DEPS_test_line_diff = $(BUILD_DIR)/line_diff.o
TEST_DEPS_test_pane_geometry = $(BUILD_DIR)/pane_geometry.o
TEST_DEPS_test_dir_summary = $(BUILD_DIR)/dir_summary.o

.SECONDEXPANSION:
$(BUILD_DIR)/tests/%: tests/%.c $$(TEST_DEPS_%) | $(BUILD_DIR)
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

# Headless UI smoke test (needs Xvfb, xdotool, ImageMagick). Screenshots go to
# build/ui/shots; set UI_BASELINE=<dir of shots> to compare with a reference run.
ui-test: $(TARGET)
	@sh tests/ui/run_ui_test.sh ./$(TARGET)

clean:
	rm -rf $(TARGET) $(BUILD_DIR)

-include $(DEPS) $(TEST_BINS:=.d)

.PHONY: all clean test check ui-test

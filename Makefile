# Makefile for the GTK 4 file explorer

CC = gcc
PKG_CONFIG ?= pkg-config

# Static build support: enable with 'make static' or 'STATIC=1 make'
STATIC ?= 0
ifeq ($(STATIC),1)
PKG_CONFIG_FLAGS = --static
STATIC_LDFLAGS ?= -static
else
PKG_CONFIG_FLAGS =
STATIC_LDFLAGS =
endif

CFLAGS = -Wall -Wextra -Wno-deprecated-declarations -Isrc `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --cflags gtk4`
LDFLAGS = $(STATIC_LDFLAGS) `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs gtk4`

# Optional embedded terminal: enabled automatically when vte-2.91-gtk4 is installed
HAVE_VTE != $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --exists vte-2.91-gtk4 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_VTE),yes)
CFLAGS += -DHAVE_VTE `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --cflags vte-2.91-gtk4`
LDFLAGS += `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs vte-2.91-gtk4`
endif

# Optional syntax highlighting: enabled automatically when gtksourceview-5 is installed
HAVE_GSV != $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --exists gtksourceview-5 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_GSV),yes)
CFLAGS += -DHAVE_GSV `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --cflags gtksourceview-5`
LDFLAGS += `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs gtksourceview-5`
endif

# Optional PDF rendering: enabled automatically when poppler-glib is installed
HAVE_POPPLER != $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --exists poppler-glib 2>/dev/null && echo yes || echo no
ifeq ($(HAVE_POPPLER),yes)
CFLAGS += -DHAVE_POPPLER `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --cflags poppler-glib`
LDFLAGS += `$(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs poppler-glib`
endif

TARGET = explorateur_fichiers
TARGET_STATIC = $(TARGET)-static
BUNDLE_DIR = bundle
BUNDLE_APP = $(BUNDLE_DIR)/$(TARGET)
BUNDLE_LIB = $(BUNDLE_DIR)/lib

SRCS = src/main.c \
       src/file_list.c \
       src/bookmarks.c \
       src/notes.c \
       src/remote.c \
       src/viewer.c \
       src/video.c \
       src/audio.c \
       src/markdown.c \
       src/html.c \
       src/pdf.c \
       src/git_tab.c \
       src/editor.c \
       src/minimap.c \
       src/terminal.c \
       src/search.c \
       src/ui.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)
ifeq ($(STATIC),1)
	@cp -f $(TARGET) $(TARGET_STATIC) 2>/dev/null || true
	@echo "Static binary available at $(TARGET) and $(TARGET_STATIC)"
endif

static:
	@GTK_LIBDIR="$$($(PKG_CONFIG) --variable=libdir gtk4 2>/dev/null)"; \
	GTK_A="$$($(CC) -print-file-name=libgtk-4.a 2>/dev/null)"; \
	if [ "$$GTK_A" = "libgtk-4.a" -o ! -f "$$GTK_A" ] && [ ! -f "$$GTK_LIBDIR/libgtk-4.a" ]; then \
		echo "================================================================================"; \
		echo "ERROR: Static GTK 4 archive (libgtk-4.a) was not found on your system."; \
		echo ""; \
		echo "Why this happens:"; \
		echo "  Standard Linux distributions (Debian, Ubuntu, Fedora, Arch) only distribute"; \
		echo "  shared libraries (.so) for GTK 4, Pango, and GLib via 'libgtk-4-dev'."; \
		echo "  Upstream GTK 4 does not package static (.a) archives because GTK 4 relies"; \
		echo "  on runtime dynamic loading (GIO modules, Wayland/X11, renderers, media)."; \
		echo ""; \
		echo "Available alternatives:"; \
		echo "  1) Build a portable standalone bundle (includes all shared libraries):"; \
		echo "       make bundle"; \
		echo "  2) Build dynamically as recommended:"; \
		echo "       make"; \
		echo "  3) If you compiled GTK 4 statically from source in a custom prefix:"; \
		echo "       PKG_CONFIG_PATH=/path/to/static/pkgconfig make static"; \
		echo "================================================================================"; \
		exit 1; \
	fi
	@$(MAKE) -B STATIC=1

$(TARGET_STATIC):
	@$(MAKE) static

bundle: $(TARGET)
	@echo "Creating portable standalone bundle in $(BUNDLE_DIR)..."
	@rm -rf $(BUNDLE_DIR)
	@mkdir -p $(BUNDLE_DIR)/bin $(BUNDLE_LIB)
	@cp -f $(TARGET) $(BUNDLE_DIR)/bin/
	@echo "Copying shared library dependencies into $(BUNDLE_LIB)..."
	@for lib in $$(ldd $(TARGET) 2>/dev/null | grep "=> /" | awk '{print $$3}' | grep -v 'libc\.so\|libm\.so\|libpthread\.so\|libdl\.so\|ld-linux'); do \
		if [ -f "$$lib" ]; then \
			cp -L -n "$$lib" $(BUNDLE_LIB)/ 2>/dev/null || true; \
		fi; \
	done
	@echo '#!/bin/sh' > $(BUNDLE_APP)
	@echo 'HERE="$$(dirname "$$(readlink -f "$$0")")"' >> $(BUNDLE_APP)
	@echo 'export LD_LIBRARY_PATH="$$HERE/lib:$${LD_LIBRARY_PATH}"' >> $(BUNDLE_APP)
	@echo 'exec "$$HERE/bin/$(TARGET)" "$$@"' >> $(BUNDLE_APP)
	@chmod +x $(BUNDLE_APP)
	@echo "================================================================================"
	@echo "Bundle created successfully!"
	@echo "Standalone launcher: $(BUNDLE_APP)"
	@echo "You can move or distribute the '$(BUNDLE_DIR)' folder to another Linux system."
	@echo "================================================================================"

clean:
	rm -rf $(TARGET) $(TARGET_STATIC) $(BUNDLE_DIR)

.PHONY: all clean static bundle

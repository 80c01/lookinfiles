# Makefile for the GTK 4 file explorer
#
# Each source file is compiled to its own object in build/ with automatic
# header dependency tracking (-MMD -MP): editing a header rebuilds every
# file that includes it, and only those files.

CC = gcc
PKG_CONFIG ?= pkg-config

CFLAGS = -Wall -Wextra -Wno-deprecated-declarations -Isrc `$(PKG_CONFIG) --cflags gtk4`
# Check deprecation:
#FLAGS = -Wall -Wextra -Werror -Isrc `$(PKG_CONFIG) --cflags gtk4`
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
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(TARGET) $(BUILD_DIR)

-include $(DEPS)

.PHONY: all clean

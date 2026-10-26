# Makefile for the GTK 4 file explorer

CC = gcc
CFLAGS = -Wall -Wextra -Wno-deprecated-declarations -Isrc `pkg-config --cflags gtk4`
LDFLAGS = `pkg-config --libs gtk4`

# Optional embedded terminal: enabled automatically when vte-2.91-gtk4 is installed
HAVE_VTE != pkg-config --exists vte-2.91-gtk4 && echo yes || echo no
ifeq ($(HAVE_VTE),yes)
CFLAGS += -DHAVE_VTE `pkg-config --cflags vte-2.91-gtk4`
LDFLAGS += `pkg-config --libs vte-2.91-gtk4`
endif

# Optional syntax highlighting: enabled automatically when gtksourceview-5 is installed
HAVE_GSV != pkg-config --exists gtksourceview-5 && echo yes || echo no
ifeq ($(HAVE_GSV),yes)
CFLAGS += -DHAVE_GSV `pkg-config --cflags gtksourceview-5`
LDFLAGS += `pkg-config --libs gtksourceview-5`
endif

# Optional PDF rendering: enabled automatically when poppler-glib is installed
HAVE_POPPLER != pkg-config --exists poppler-glib && echo yes || echo no
ifeq ($(HAVE_POPPLER),yes)
CFLAGS += -DHAVE_POPPLER `pkg-config --cflags poppler-glib`
LDFLAGS += `pkg-config --libs poppler-glib`
endif

TARGET = explorateur_fichiers
SRCS = src/main.c \
       src/file_list.c \
       src/viewer.c \
       src/markdown.c \
       src/html.c \
       src/pdf.c \
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

clean:
	rm -f $(TARGET)

.PHONY: all clean

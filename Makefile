# Makefile for the GTK+ 3 file explorer

CC = gcc
CFLAGS = -Wall -Wextra `pkg-config --cflags gtk+-3.0`
LDFLAGS = `pkg-config --libs gtk+-3.0`

# Optional embedded terminal: enabled automatically when libvte-2.91 is installed
HAVE_VTE != pkg-config --exists vte-2.91 && echo yes || echo no
ifeq ($(HAVE_VTE),yes)
CFLAGS += -DHAVE_VTE `pkg-config --cflags vte-2.91`
LDFLAGS += `pkg-config --libs vte-2.91`
endif

TARGET = explorateur_fichiers
SRCS = explorateur_fichiers.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build succeeded!"
	@ls -lh $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean

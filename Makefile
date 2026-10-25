# Makefile pour Explorateur de Fichiers GTK+ 3

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = `pkg-config --cflags --libs gtk+-3.0`

TARGET = explorateur_fichiers
SRCS = explorateur_fichiers.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Compilation terminee avec succes!"
	@ls -lh $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean

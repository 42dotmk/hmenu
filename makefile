.POSIX:

CC      = cc

# Version derived from `git describe` at build time so the binary reports
# the exact tag/commit it was built from; "dev" without git metadata.
VERSION != git describe --tags --always --dirty 2>/dev/null || echo dev

CFLAGS  = -std=c99 -pedantic -Wall -Wextra -Os -D_POSIX_C_SOURCE=200809L \
          -DHMENU_VERSION='"$(VERSION)"' \
          -isystem vendor `pkg-config --cflags xft`
LDLIBS  = -lX11 -lXrandr `pkg-config --libs xft`
BINDIR  = $(HOME)/.local/bin

all: hmenu

hmenu: hmenu.c config.h vendor/stb_ds.h
	$(CC) $(CFLAGS) -o $@ hmenu.c $(LDLIBS)

install: hmenu
	mkdir -p $(BINDIR)
	ln -sf "$$(pwd)/hmenu" $(BINDIR)/hmenu

uninstall:
	rm -f $(BINDIR)/hmenu

clean:
	rm -f hmenu

.PHONY: all install uninstall clean

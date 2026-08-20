# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

hmenu is a single-file X11 program (`hmenu.c`): a rofi-style launcher — a
centered override-redirect window with a typed filter above a vertical item
list. It deliberately contains no matching logic and no built-in item
sources: items come from shell commands (`modes[]` in `config.h`, or the
command passed as the argument), and matching/ranking is delegated to
`fzf --filter <query>`, re-run on every keystroke, so search behaves exactly
like interactive fzf. Return executes the selected line via `sh -c` (or the
typed text verbatim when nothing matches), Shift+Return wraps it in
`terminal -e sh -c line` (st-style `-e`), Escape/Ctrl-C cancels.

Suckless-style: configuration lives in `config.h` (included by `hmenu.c`),
not a runtime config file. The first block of settings can additionally be
overridden at startup by `HMENU_*` environment variables; each setting's
comment names its variable, and `loadconfig()` applies them.

## Build

```sh
make            # builds ./hmenu (needs libX11 + libXrandr + libXft headers)
make install    # symlinks it into ~/.local/bin
make clean
```

No tests; the compiler flags (`-std=c11 -pedantic -Wall -Wextra`) are the
lint. Keep the build warning-free. `fzf` is a runtime dependency.
`vendor/stb_ds.h` provides the dynamic arrays for the item/match lists.

Verify by running under Xephyr (`Xephyr :77 -screen 900x700 &`, then
`DISPLAY=:77 ./hmenu`) — never on the live display, since hmenu grabs the
keyboard. XTEST fake keys work against the grab for scripted checks.

## Architecture

- `loadlist()` runs the list command through `popen`, keeping the output
  twice: split into `items` (stb_ds array of lines) for display, and raw in
  a `tmpfile()` that is rewound and replayed as fzf's stdin on every
  refilter (avoids write/read pipe deadlocks).
- Two helper flags print item lists in the `DISPLAY\tACTION` shape and are
  what the built-in modes run: `hmenu -l` (EWMH window list, the win mode)
  and `hmenu -d` (XDG desktop applications, the app mode). `listapps()`
  scans `$XDG_DATA_HOME` then `$XDG_DATA_DIRS` `applications/` dirs
  (earlier dirs shadow later ones by filename, tracked with an stb_ds
  string map); `desktopentry()` parses only the `[Desktop Entry]` group,
  skips NoDisplay/Hidden/OnlyShowIn/non-Application entries, strips Exec
  field codes (`%%` unescapes), prepends `cd` for Path=, and wraps
  Terminal=true lines in `terminal -e sh -c` (shell-quoted by
  `shquote()`).
- `filter()` forks `fzf --filter <input>` per keystroke and splits its
  ranked stdout into `matches`; an empty query short-circuits to the full
  list. fzf exiting 127 (not found) is fatal, exiting 1 (no match) just
  means an empty list.
- `keypress()` uses `Xutf8LookupString` (XIM when available) for text
  input; editing is utf8-aware with a movable cursor (readline-ish keys:
  C-a/C-e/C-u/C-w, arrows, Home/End). Up/Down/Tab/C-p/C-n move the
  selection, PgUp/PgDn by a page.
- `drawmenu()` renders everything into a backbuffer pixmap with Xft and
  copies it over on each change; the input scrolls horizontally so the
  caret stays visible, long items are truncated at utf8 boundaries.
- The window is centered on the monitor holding the pointer (Xrandr), and
  carries `_NET_WM_WINDOW_TYPE_DIALOG`: hws pauses its own keyboard grab
  and raise-loop while an override-redirect dialog is mapped, which is what
  lets hmenu work on top of the workspace overview. Keep that property when
  touching window setup.
- `grabkb()` retries for ~1s like dmenu — both for keys still held from
  the launching keybind and to give hws time to yield its grab.
- `execline()` forks, `setsid()`s, and execs; the parent exits right away,
  so launched apps are reparented to init.

## Style

Suckless/OpenBSD C conventions like htray — C11, fixed-size buffers with
`snprintf`, no dynamic allocation beyond stb_ds — formatted by clang-format
via the repo's `.clang-format` (shared across the siblings: 4-space indent,
attached braces, 80 columns). Run `clang-format -i` on files you touch.

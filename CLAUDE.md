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

Configuration is layered (see `loadconfig()`), weakest first:

1. `config.h` (included by `hmenu.c`) — the compiled-in defaults and the
   source of truth for what exists; each setting's comment names its
   `[conf key]` and `HMENU_*` variable.
2. `~/.config/hackable/hmenu.conf`, read by `vendor/hconf.h` — a tiny
   `key = value` reader vendored like `stb_ds.h` and meant to be shared
   by the sibling tools (hmenu is the pilot). `[mode <name>]` sections
   add or override entries of `modes[]` (keys `cmd`, `fallback`); `args`
   replaces `defargs[]`. Values are raw (no quoting/escapes; the
   display/action separator is a literal tab), `\` at end of line
   continues a value.
3. `HMENU_*` environment variables — per-invocation, strongest.

Errors never stop hmenu: unparsable lines, unknown keys (checked against
`knownkeys[]`) and bad numbers become `hconf_diag()` warnings on stderr
and the defaults stay; `hmenu --check` prints the same diagnostics plus
the resulting mode list and exits 0/1 — run it after editing the file.
A `[mode x]` without a `cmd` is dropped with a warning.

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
  ranked stdout into `matches`; when that is empty `fallbackrow()` puts
  the mode's `fallback` line there instead (query substituted: as typed in
  the display part, shell-quoted in the action part), and Ctrl+Return runs
  that line from anywhere — `run` and `hist` use it to search in hweb; an empty query short-circuits to the full
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

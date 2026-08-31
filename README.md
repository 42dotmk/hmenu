# hmenu

A rofi-style launcher for X11 that outsources everything interesting:
items come from shell commands, matching comes from
[fzf](https://github.com/junegunn/fzf).

A centered window with a typed filter and a vertical list. Type to
filter (exactly fzf's matching — it *is* fzf, run with `--filter` on
every keystroke), Return runs the selected line with `sh -c`,
Shift+Return runs it in a terminal (`hterm -e sh -c ...`), Escape
cancels. If nothing matches, Return runs what you typed — or, for modes
with a fallback (`run`, `hist`: a web search in hweb), that; Ctrl+Return
runs the fallback from anywhere.

```sh
hmenu                 # the default modes from config.h: win, app, run
hmenu app             # a mode by name (XDG desktop applications)
hmenu hist            # hweb browsing history; Return opens the url in hweb,
                      # unmatched text (or Ctrl+Return) searches it in hweb
hmenu 'ls ~/scripts'  # any command; its output lines become the items
```

An item line may contain a tab: the part before it is shown and matched,
the part after it is what runs. Two helper flags print lists in that
shape: `hmenu -l` (open windows, activated via `hmenu -a id`) and
`hmenu -d` (XDG desktop entries, with Exec field codes stripped,
Terminal=true wrapped in the terminal).

Configuration is layered, weakest first: `config.h` defaults
(recompile), `~/.config/hackable/hmenu.conf` (runtime, optional), and
`HMENU_*` environment variables (per invocation). The file is plain
`key = value` lines — values raw, no quoting, a trailing backslash
continues a long one — plus `[mode <name>]` sections that add new item
sources or override built-in ones, and `args` for what a bare `hmenu`
shows:

```
fontsize = 16
lines = 20
args = win app run pass scripts

[mode scripts]
cmd = ls -1 ~/bin
```

`hmenu --check` validates the file (syntax, unknown keys, bad numbers)
and lists the resulting modes without opening a window; a broken file
never stops hmenu — it warns and runs on the defaults. Key names are in
`config.h`'s comments; the reader is `vendor/hconf.h`, shared across
the hackable tools.

hmenu tags its window `_NET_WM_WINDOW_TYPE_DIALOG`; the hws overview
yields its keyboard grab to such windows, so you can summon hmenu and
launch apps on top of the workspace overview.

## Build

```sh
make            # needs libX11, libXrandr, libXft; fzf at runtime
make install    # symlink into ~/.local/bin
```

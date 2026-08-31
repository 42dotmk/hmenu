# hmenu

A rofi-style launcher for X11 that outsources everything interesting:
items come from shell commands, matching comes from
[fzf](https://github.com/junegunn/fzf).

A centered window with a typed filter and a vertical list. Type to
filter (exactly fzf's matching — it *is* fzf, run with `--filter` on
every keystroke), Return runs the selected line with `sh -c`,
Shift+Return runs it in a terminal (`hterm -e sh -c ...`), Escape
cancels. If nothing matches, Return runs what you typed.

```sh
hmenu                 # the default modes from config.h: win, app, run
hmenu app             # a mode by name (XDG desktop applications)
hmenu hist            # hweb browsing history; Return opens the url in hweb
hmenu 'ls ~/scripts'  # any command; its output lines become the items
```

An item line may contain a tab: the part before it is shown and matched,
the part after it is what runs. Two helper flags print lists in that
shape: `hmenu -l` (open windows, activated via `hmenu -a id`) and
`hmenu -d` (XDG desktop entries, with Exec field codes stripped,
Terminal=true wrapped in the terminal).

Configuration is `config.h` (recompile), with `HMENU_*` environment
overrides for colors, font, size and terminal — see the comments there.

hmenu tags its window `_NET_WM_WINDOW_TYPE_DIALOG`; the hws overview
yields its keyboard grab to such windows, so you can summon hmenu and
launch apps on top of the workspace overview.

## Build

```sh
make            # needs libX11, libXrandr, libXft; fzf at runtime
make install    # symlink into ~/.local/bin
```

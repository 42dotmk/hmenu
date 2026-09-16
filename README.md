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
hmenu xbps            # Void packages, installed first: [*] installed, [u]
                      # update available, [-] not; Return asks install /
                      # update / remove / details (hmenu-xbps, a script)
hmenu keys            # hwm's key bindings with what they do (`hwm keys`);
                      # a cheat sheet: Return just closes it
hmenu 'ls ~/scripts'  # any command; its output lines become the items
hmenu --title 'Which branch?' 'git branch --format=%(refname:short)'
                      # a title above the input says what the menu is for
                      # (newlines kept, long lines wrapped)
b=$(hmenu -p 'git branch --format=%(refname:short)')
                      # -p prints the chosen line (or the typed text when
                      # nothing matches) instead of running it; Escape
                      # exits 1 — dmenu-style, for scripts
pw=$(hmenu -s --title 'Password:')
                      # -s: a secret prompt — the input row only, typed
                      # text shown as *s, Return prints it (implies -p)
```

An item line may contain a tab: the part before it is shown and matched,
the part after it is what runs. Two helper flags print lists in that
shape: `hmenu -l` (open windows, activated via `hmenu -a id`) and
`hmenu -d` (XDG desktop entries, with Exec field codes stripped,
Terminal=true wrapped in the terminal).
The `xbps` mode is a shell script, `hmenu-xbps`: `list` prints the
rows, `menu name` (each row's action) opens a second hmenu with the
choices that fit the package's state and runs the one picked in the
terminal (`HMENU_TERMINAL`, hterm by default). Its sudo asks for the
password through `hmenu-askpass` (`hmenu -s` with sudo's prompt as the
title, the askpass contract: password on stdout, exit 1 to cancel).
`export SUDO_ASKPASS=hmenu-askpass` in your X session makes any
`sudo -A` on the desktop prompt that way; a plain `sudo`, or one over
ssh without a DISPLAY, keeps asking on the terminal — sudo only calls
the askpass with `-A` or when it has no tty.

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

`HMENU_FALLBACK='answer: %s\thai answer %s'` gives a list command's menu
a fallback of its own (a mode's still wins), so unmatched typed text can
become an answer rather than a command — how hai asks questions, with
the question itself as the `--title`.
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

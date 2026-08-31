/* hmenu configuration: the compiled-in defaults. Two runtime layers
 * overlay them, weakest first: ~/.config/hackable/hmenu.conf (hconf;
 * key names in [brackets] below, [mode <name>] sections with cmd and
 * fallback add or override modes[], args replaces defargs[]; validate
 * with `hmenu --check`), then the HMENU_* environment variables named
 * in the comments (numbers decimal); see loadconfig(). */

static const char *fontname =
    "Iosevka NFM"; /* fontconfig name/pattern, [font], HMENU_FONT */
static unsigned int fontsize =
    14; /* px appended to the pattern; 0 = its own, [fontsize], HMENU_FONTSIZE */
static const char *col_bg = "#1a1b26"; /* background, [bg], HMENU_BG */
static const char *col_fg = "#c0caf5"; /* item text, [fg], HMENU_FG */
static const char *col_selbg =
    "#7aa2f7"; /* selected item background, [selbg], HMENU_SELBG */
static const char *col_selfg =
    "#1a1b26"; /* selected item text, [selfg], HMENU_SELFG */
static const char *col_prompt =
    "#7aa2f7"; /* input text and caret, [prompt], HMENU_PROMPTCOL */
static const char *col_dim =
    "#565f89"; /* match counter, separator, [dim], HMENU_DIM */
static const char *col_border =
    "#3b4261"; /* window border, [border], HMENU_BORDERCOL */
static unsigned int menuw = 640; /* menu width in px, [width], HMENU_WIDTH */
static unsigned int lines = 15;  /* list rows shown, [lines], HMENU_LINES */
static unsigned int borderw =
    2; /* window border width, [borderpx], HMENU_BORDERPX */
static unsigned int hpad = 12;   /* inner horizontal padding, [hpad] */
static unsigned int vpad = 8;    /* inner vertical padding, [vpad] */
static unsigned int linepad = 6; /* extra px of row height, [linepad] */
static const char *terminal =
    "hterm"; /* Shift+Return: `terminal -e sh -c line`, [terminal],
                HMENU_TERMINAL */
static const char *fzfcmd =
    "fzf"; /* filter program, run as `fzf --filter query`, [fzf] */

/* item sources: arguments are mode names, and any other argument is
 * itself a list command; the outputs are appended in argument order.
 * Commands run through `sh -c` and print one item per line; the chosen
 * line is executed with `sh -c` (Return) or in the terminal (Shift+Return).
 * A tab in a line splits display from action: the part before it is shown
 * and matched, the part after it is executed (see `hmenu -l`). A mode's
 * optional fallback is a line template that replaces "run the typed text"
 * when nothing matches (it is shown as the only row), and Ctrl+Return runs
 * it from anywhere: %s in its display part is the query as typed, in its
 * action part the query shell-quoted. The first given mode's is used. */
static const struct mode {
    const char *name;
    const char *cmd;
    const char *fallback;
} modes[] = {
    {"win", "hmenu -l", NULL}, /* open windows; Return activates via -a */
    {"app", "hmenu -d | sort -f", NULL}, /* XDG desktop applications */
    {"run",
     "{ IFS=:; for d in $PATH; do [ -d \"$d\" ] && ls -1 \"$d\"; done; "
     "} 2>/dev/null | sort -u",
     "search: %s\thweb %s"}, /* nothing to run: search the web */
    {"pass", /* password-store entries; Return copies via `pass -c` */
     "cd ~/.password-store && find . -name '*.gpg' -not -path './.git/*' | "
     "sed 's|^\\./||;s|\\.gpg$||' | sort -f | sed \"s/.*/&\\tpass -c '&'/\"",
     NULL},
    {"hist", /* hweb history, newest first, one entry per url; Return opens
              * the url in a new hweb window, unmatched text is searched */
     "tac \"${XDG_DATA_HOME:-$HOME/.local/share}/hweb/history\" 2>/dev/null | "
     "awk -F'\\t' '!seen[$1]++ { u=$1; gsub(/[\"\\\\$`]/, \"\\\\\\\\&\", u); "
     "printf \"%s  %s\\thweb \\\"%s\\\"\\n\", ($2 != \"\" ? $2 : $1), ($2 != "
     "\"\" ? $1 : \"\"), u }'",
     "search: %s\thweb %s"},
    /* { "scripts", "ls -1 ~/bin" }, */
};

/* what a bare `hmenu` shows, in order: mode names or list commands;
 * the conf file's [args] key replaces this list */
static const char *defargs[] = {"win", "app", "run"};

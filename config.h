/* hmenu configuration. Every setting in the first block can also be
 * overridden at startup by the environment variable named in its
 * comment (numbers decimal); see loadconfig(). Unset variables leave
 * these defaults in effect. */

static const char *fontname =
    "Iosevka NFM"; /* fontconfig name/pattern, HMENU_FONT */
static unsigned int fontsize =
    14; /* px size appended to the pattern; 0 = pattern's own, HMENU_FONTSIZE */
static const char *col_bg = "#1a1b26"; /* background, HMENU_BG */
static const char *col_fg = "#c0caf5"; /* item text, HMENU_FG */
static const char *col_selbg =
    "#7aa2f7"; /* selected item background, HMENU_SELBG */
static const char *col_selfg = "#1a1b26"; /* selected item text, HMENU_SELFG */
static const char *col_prompt =
    "#7aa2f7"; /* input text and caret, HMENU_PROMPTCOL */
static const char *col_dim =
    "#565f89"; /* match counter, separator, HMENU_DIM */
static const char *col_border = "#3b4261"; /* window border, HMENU_BORDERCOL */
static unsigned int menuw = 640;           /* menu width in px, HMENU_WIDTH */
static unsigned int lines = 15;            /* list rows shown, HMENU_LINES */
static unsigned int borderw = 2; /* window border width, HMENU_BORDERPX */
static unsigned int hpad = 12;   /* inner horizontal padding */
static unsigned int vpad = 8;    /* inner vertical padding */
static unsigned int linepad = 6; /* extra px of row height */
static const char *terminal =
    "hterm"; /* Shift+Return runs `terminal -e sh -c line`, HMENU_TERMINAL */
static const char *fzfcmd =
    "fzf"; /* filter program, run as `fzf --filter query` */

/* item sources: arguments are mode names, and any other argument is
 * itself a list command; the outputs are appended in argument order.
 * Commands run through `sh -c` and print one item per line; the chosen
 * line is executed with `sh -c` (Return) or in the terminal (Shift+Return).
 * A tab in a line splits display from action: the part before it is shown
 * and matched, the part after it is executed (see `hmenu -l`). */
static const struct mode {
    const char *name;
    const char *cmd;
} modes[] = {
    {"win", "hmenu -l"},           /* open windows; Return activates via -a */
    {"app", "hmenu -d | sort -f"}, /* XDG desktop applications */
    {"run", "{ IFS=:; for d in $PATH; do [ -d \"$d\" ] && ls -1 \"$d\"; done; "
            "} 2>/dev/null | sort -u"},
    /* { "scripts", "ls -1 ~/bin" }, */
};

/* what a bare `hmenu` shows, in order: mode names or list commands */
static const char *defargs[] = {"win", "app", "run"};

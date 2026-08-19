/* hmenu - a rofi-style launcher for X11.
 *
 * A centered override-redirect window with a typed filter and a vertical
 * item list. The items come from shell commands (see modes[] and defargs[]
 * in config.h; arguments are mode names or themselves list commands, their
 * outputs appended in order); matching and ranking are delegated to
 * `fzf --filter`, re-run on every keystroke, so search behaves exactly
 * like fzf. With an empty query the list shows the sources' own order.
 * Return executes the selected line with `sh -c` (or the typed text when
 * nothing matches), Shift+Return runs it in the terminal
 * (`terminal -e sh -c line`, st-style), Escape cancels.
 *
 * A line may contain a tab: the part before it is displayed and matched,
 * the part after it is what gets executed. `hmenu -l` prints the EWMH
 * window list in that shape ("TITLE\thmenu -a 0xID" - the win mode), and
 * `hmenu -a windowid` activates a window via _NET_ACTIVE_WINDOW.
 * `hmenu -d` prints XDG desktop applications the same way ("NAME\tcommand",
 * Exec with its field codes dropped - the app mode).
 *
 * The window carries _NET_WM_WINDOW_TYPE_DIALOG so exclusive overlays
 * (hws) can yield their keyboard grab while hmenu is up - launching apps
 * works even on top of the workspace overview. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xft/Xft.h>

#define STB_DS_IMPLEMENTATION
#include <stb_ds.h>

#include "config.h"

#ifndef HMENU_VERSION
#define HMENU_VERSION "dev"
#endif

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static Display *dpy;
static int screen;
static Window root, win;
static int mx, my, mw, mh;   /* monitor the menu is centered on */
static int ww, wh;           /* menu window size */
static Pixmap buf;
static GC gc;
static XftFont *font;
static XftDraw *xd;
static XftColor cbg, cfg, cselbg, cselfg, cprompt, cdim, cborder;
static XIM xim;
static XIC xic;
static int fh, rowh;         /* font height, list row height */
static char *listbuf;        /* stb_ds char array: the raw list, \0-split */
static char **items;         /* stb_ds: lines of listbuf */
static char *fbuf;           /* stb_ds char array: last fzf output */
static char **matches;       /* stb_ds: current matches (into listbuf/fbuf) */
static FILE *listfile;       /* raw list again, stdin for fzf */
static char input[1024];
static int cursor;           /* byte offset into input */
static int sel, off;         /* selected match, first visible match */
static int running = 1;

static void
die(const char *msg)
{
	fputs(msg, stderr);
	exit(1);
}

static void
usage(void)
{
	size_t i;

	fputs("usage: hmenu [mode|listcmd]...\n"
	      "       hmenu -l | -d | -a windowid | -v\nmodes:", stderr);
	for (i = 0; i < LEN(modes); i++)
		fprintf(stderr, " %s", modes[i].name);
	fputs("\n", stderr);
	exit(1);
}

static void
envuint(const char *name, unsigned int *dst)
{
	const char *s = getenv(name);
	char *end;
	long v;

	if (!s || !*s)
		return;
	v = strtol(s, &end, 10);
	if (!*end && v >= 0)
		*dst = (unsigned int)v;
}

static void
envstr(const char *name, const char **dst)
{
	const char *s = getenv(name);

	if (s && *s)
		*dst = s;
}

/* apply HMENU_* environment overrides to the config.h defaults */
static void
loadconfig(void)
{
	envstr("HMENU_FONT", &fontname);
	envuint("HMENU_FONTSIZE", &fontsize);
	envstr("HMENU_BG", &col_bg);
	envstr("HMENU_FG", &col_fg);
	envstr("HMENU_SELBG", &col_selbg);
	envstr("HMENU_SELFG", &col_selfg);
	envstr("HMENU_PROMPTCOL", &col_prompt);
	envstr("HMENU_DIM", &col_dim);
	envstr("HMENU_BORDERCOL", &col_border);
	envuint("HMENU_WIDTH", &menuw);
	envuint("HMENU_LINES", &lines);
	envuint("HMENU_BORDERPX", &borderw);
	envstr("HMENU_TERMINAL", &terminal);
}

/* byte offset of the previous/next utf8 character boundary */
static int
utf8prev(const char *s, int i)
{
	if (i > 0)
		for (i--; i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80; i--)
			;
	return i;
}

static int
utf8next(const char *s, int i)
{
	if (s[i])
		for (i++; ((unsigned char)s[i] & 0xc0) == 0x80; i++)
			;
	return i;
}

static int
textwn(const char *s, int len)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)s, len, &ext);
	return ext.xOff;
}

static int
textw(const char *s)
{
	return textwn(s, (int)strlen(s));
}

/* draw s at x/baseline y, truncated to maxw px; a tab ends the visible
 * part of a line (the rest is the action, see execline) */
static void
drawtext(int x, int y, XftColor *c, const char *s, int maxw)
{
	int len = (int)strcspn(s, "\t");

	if (len > 256) { /* far beyond any menu width; keep truncation cheap */
		len = 256;
		while (len && ((unsigned char)s[len] & 0xc0) == 0x80)
			len--;
	}
	while (len && textwn(s, len) > maxw)
		len = utf8prev(s, len);
	if (len)
		XftDrawStringUtf8(xd, c, font, x, y, (const FcChar8 *)s, len);
}

static void
splitlines(char *b, char ***out)
{
	char *p, *e;

	for (p = b; p && *p; p = e ? e + 1 : NULL) {
		e = strchr(p, '\n');
		if (e)
			*e = '\0';
		if (*p)
			arrput(*out, p);
	}
}

/* run a list command, appending its output both to the raw buffer (split
 * into items once every source has loaded, so the list keeps the sources'
 * order) and to listfile (replayed as fzf's stdin on every refilter) */
static void
loadlist(const char *cmd)
{
	FILE *f;
	char rb[4096];
	size_t n;

	if (!(f = popen(cmd, "r")))
		die("hmenu: cannot run list command\n");
	if (!listfile && !(listfile = tmpfile()))
		die("hmenu: cannot create temp file\n");
	while ((n = fread(rb, 1, sizeof(rb), f)) > 0) {
		fwrite(rb, 1, n, listfile);
		memcpy(arraddnptr(listbuf, (int)n), rb, n);
	}
	pclose(f);
	if (arrlen(listbuf) && listbuf[arrlen(listbuf) - 1] != '\n') {
		fputc('\n', listfile); /* don't fuse with the next source */
		arrput(listbuf, '\n');
	}
	fflush(listfile);
}

/* recompute matches for the current input via `fzf --filter` */
static void
filter(void)
{
	char rb[4096];
	int fds[2], st, i;
	ssize_t n;
	pid_t pid;

	arrsetlen(matches, 0);
	if (!input[0]) { /* no query: the list as-is */
		for (i = 0; i < arrlen(items); i++)
			arrput(matches, items[i]);
	} else {
		arrsetlen(fbuf, 0);
		if (fseek(listfile, 0L, SEEK_SET) || pipe(fds))
			die("hmenu: pipe failed\n");
		if ((pid = fork()) < 0)
			die("hmenu: fork failed\n");
		if (!pid) {
			dup2(fileno(listfile), 0);
			dup2(fds[1], 1);
			close(fds[0]);
			close(fds[1]);
			execlp(fzfcmd, fzfcmd, "--filter", input, "--delimiter",
			       "\t", "--nth", "1", (char *)NULL);
			_exit(127);
		}
		close(fds[1]);
		for (;;) {
			n = read(fds[0], rb, sizeof(rb));
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				break;
			memcpy(arraddnptr(fbuf, (int)n), rb, (size_t)n);
		}
		close(fds[0]);
		waitpid(pid, &st, 0);
		if (WIFEXITED(st) && WEXITSTATUS(st) == 127)
			die("hmenu: cannot run fzf\n");
		arrput(fbuf, '\0');
		splitlines(fbuf, &matches);
	}
	sel = off = 0;
}

static void
drawmenu(void)
{
	char cnt[32];
	const char *s;
	int i, n, y, x, cntw, caretx, start, maxw, sepy;

	n = (int)arrlen(matches);
	if (sel >= n)
		sel = n ? n - 1 : 0;
	if (sel < off)
		off = sel;
	if (sel >= off + (int)lines)
		off = sel - (int)lines + 1;

	XSetForeground(dpy, gc, cbg.pixel);
	XFillRectangle(dpy, buf, gc, 0, 0, (unsigned int)ww, (unsigned int)wh);

	/* input row: typed text with caret, match counter */
	y = (int)vpad + (int)linepad / 2 + font->ascent;
	x = (int)hpad;
	snprintf(cnt, sizeof(cnt), "%d/%d", n, (int)arrlen(items));
	cntw = textw(cnt);
	drawtext(ww - (int)hpad - cntw, y, &cdim, cnt, cntw);
	maxw = ww - x - (int)hpad - cntw - fh / 2;
	start = 0; /* scroll the input so the caret stays visible */
	while (textwn(input + start, cursor - start) > maxw && start < cursor)
		start = utf8next(input, start);
	drawtext(x, y, &cprompt, input + start, maxw);
	caretx = x + textwn(input + start, cursor - start);
	XSetForeground(dpy, gc, cprompt.pixel);
	XFillRectangle(dpy, buf, gc, caretx, y - font->ascent, 2,
	               (unsigned int)fh);

	sepy = (int)vpad + rowh + (int)vpad / 2;
	XSetForeground(dpy, gc, cdim.pixel);
	XDrawLine(dpy, buf, gc, 0, sepy, ww, sepy);

	y = (int)vpad + rowh + (int)vpad;
	for (i = off; i < n && i < off + (int)lines; i++) {
		s = matches[i];
		if (i == sel) {
			XSetForeground(dpy, gc, cselbg.pixel);
			XFillRectangle(dpy, buf, gc, 0, y, (unsigned int)ww,
			               (unsigned int)rowh);
		}
		drawtext(hpad, y + (int)linepad / 2 + font->ascent,
		         i == sel ? &cselfg : &cfg, s, ww - 2 * (int)hpad);
		y += rowh;
	}
	XCopyArea(dpy, buf, win, gc, 0, 0, (unsigned int)ww, (unsigned int)wh,
	          0, 0);
	XFlush(dpy);
}

/* run line and quit: `sh -c line`, or `terminal -e sh -c line` (st-style
 * -e, everything after it is the child's argv) with Shift held. A tab
 * splits display from action: only what follows it is executed */
static void
execline(const char *line, int interm)
{
	const char *t = strchr(line, '\t');
	pid_t pid;

	if (t)
		line = t + 1;
	if ((pid = fork()) < 0)
		die("hmenu: fork failed\n");
	if (!pid) {
		setsid();
		if (interm)
			execlp(terminal, terminal, "-e", "sh", "-c", line,
			       (char *)NULL);
		else
			execl("/bin/sh", "sh", "-c", line, (char *)NULL);
		_exit(127);
	}
	running = 0;
}

static void
insert(const char *s, int n)
{
	int len = (int)strlen(input);

	if (len + n >= (int)sizeof(input))
		return;
	memmove(input + cursor + n, input + cursor, (size_t)(len - cursor + 1));
	memcpy(input + cursor, s, (size_t)n);
	cursor += n;
}

static void
delete(int from, int to)
{
	memmove(input + from, input + to, strlen(input + to) + 1);
	cursor = from;
}

static void
keypress(XKeyEvent *ev)
{
	char kb[64];
	KeySym sym = NoSymbol;
	Status st = 0;
	int n, i, changed = 0;

	if (xic)
		n = Xutf8LookupString(xic, ev, kb, sizeof(kb) - 1, &sym, &st);
	else
		n = XLookupString(ev, kb, sizeof(kb) - 1, &sym, NULL);
	if (ev->state & ControlMask) {
		switch (sym) {
		case XK_a: cursor = 0; break;
		case XK_e: cursor = (int)strlen(input); break;
		case XK_p: sel--; break;
		case XK_n: sel++; break;
		case XK_u:
			delete(0, cursor);
			changed = 1;
			break;
		case XK_w:
			i = cursor;
			while (i > 0 && input[utf8prev(input, i)] == ' ')
				i = utf8prev(input, i);
			while (i > 0 && input[utf8prev(input, i)] != ' ')
				i = utf8prev(input, i);
			delete(i, cursor);
			changed = 1;
			break;
		case XK_c:
		case XK_g:
			exit(1);
		default:
			return;
		}
	} else {
		switch (sym) {
		case XK_Escape:
			exit(1);
		case XK_Return:
		case XK_KP_Enter:
			if (arrlen(matches))
				execline(matches[sel], ev->state & ShiftMask);
			else if (input[0])
				execline(input, ev->state & ShiftMask);
			return;
		case XK_BackSpace:
			if (cursor > 0) {
				delete(utf8prev(input, cursor), cursor);
				changed = 1;
			}
			break;
		case XK_Delete:
			if (input[cursor]) {
				delete(cursor, utf8next(input, cursor));
				changed = 1;
			}
			break;
		case XK_Left:
			cursor = utf8prev(input, cursor);
			break;
		case XK_Right:
			cursor = utf8next(input, cursor);
			break;
		case XK_Home: cursor = 0; break;
		case XK_End: cursor = (int)strlen(input); break;
		case XK_Up: sel--; break;
		case XK_Down:
		case XK_Tab: sel++; break;
		case XK_ISO_Left_Tab: sel--; break;
		case XK_Prior: sel -= (int)lines; break;
		case XK_Next: sel += (int)lines; break;
		default:
			if (n > 0 && !iscntrl((unsigned char)kb[0]) &&
			    (!xic || st == XLookupChars || st == XLookupBoth)) {
				insert(kb, n);
				changed = 1;
			}
			break;
		}
	}
	if (sel < 0)
		sel = 0;
	if (sel >= arrlen(matches))
		sel = arrlen(matches) ? (int)arrlen(matches) - 1 : 0;
	if (changed)
		filter();
	drawmenu();
}

/* center on the monitor holding the pointer */
static void
pickmonitor(void)
{
	XRRMonitorInfo *info;
	Window dw;
	int px = 0, py = 0, di, i, nmon = 0;
	unsigned int dui;

	mx = my = 0;
	mw = DisplayWidth(dpy, screen);
	mh = DisplayHeight(dpy, screen);
	XQueryPointer(dpy, root, &dw, &dw, &px, &py, &di, &di, &dui);
	info = XRRGetMonitors(dpy, root, True, &nmon);
	for (i = 0; i < nmon; i++)
		if (px >= info[i].x && px < info[i].x + info[i].width &&
		    py >= info[i].y && py < info[i].y + info[i].height) {
			mx = info[i].x;
			my = info[i].y;
			mw = info[i].width;
			mh = info[i].height;
			break;
		}
	if (info)
		XRRFreeMonitors(info);
}

/* the launching keybinding usually still holds keys down, and hws needs a
 * moment to yield its grab when we map on top of it; retry like dmenu */
static void
grabkb(void)
{
	struct timespec ts = {0, 10 * 1000000};
	int i;

	for (i = 0; i < 100; i++) {
		if (XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync,
		                  CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("hmenu: cannot grab keyboard\n");
}

static void
xftcolor(const char *name, XftColor *c)
{
	if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
	                       DefaultColormap(dpy, screen), name, c))
		die("hmenu: cannot allocate color\n");
}

static void
setup(void)
{
	XSetWindowAttributes swa;
	XClassHint ch = {(char *)"hmenu", (char *)"hmenu"};
	Atom type, dialog;
	char pat[256];
	int x, y;

	if (!(dpy = XOpenDisplay(NULL)))
		die("hmenu: cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	xftcolor(col_bg, &cbg);
	xftcolor(col_fg, &cfg);
	xftcolor(col_selbg, &cselbg);
	xftcolor(col_selfg, &cselfg);
	xftcolor(col_prompt, &cprompt);
	xftcolor(col_dim, &cdim);
	xftcolor(col_border, &cborder);
	if (fontsize)
		snprintf(pat, sizeof(pat), "%s:pixelsize=%u", fontname, fontsize);
	else
		snprintf(pat, sizeof(pat), "%s", fontname);
	if (!(font = XftFontOpenName(dpy, screen, pat)))
		die("hmenu: cannot load font\n");
	fh = font->ascent + font->descent;
	rowh = fh + (int)linepad;
	pickmonitor();
	ww = (int)menuw;
	if (ww > mw - 2 * (int)borderw)
		ww = mw - 2 * (int)borderw;
	wh = 2 * (int)vpad + rowh + (int)vpad + (int)lines * rowh;
	x = mx + (mw - ww) / 2 - (int)borderw;
	y = my + (mh - wh) / 2 - (int)borderw;
	swa.override_redirect = True;
	swa.background_pixel = cbg.pixel;
	swa.border_pixel = cborder.pixel;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	win = XCreateWindow(dpy, root, x, y, (unsigned int)ww,
	                    (unsigned int)wh, borderw, CopyFromParent,
	                    CopyFromParent, CopyFromParent,
	                    CWOverrideRedirect | CWBackPixel | CWBorderPixel |
	                        CWEventMask,
	                    &swa);
	XSetClassHint(dpy, win, &ch);
	XStoreName(dpy, win, "hmenu");
	/* exclusive overlays (hws) yield their grab to mapping OR dialogs */
	type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	XChangeProperty(dpy, win, type, XA_ATOM, 32, PropModeReplace,
	                (unsigned char *)&dialog, 1);
	buf = XCreatePixmap(dpy, win, (unsigned int)ww, (unsigned int)wh,
	                    (unsigned int)DefaultDepth(dpy, screen));
	gc = XCreateGC(dpy, buf, 0, NULL);
	xd = XftDrawCreate(dpy, buf, DefaultVisual(dpy, screen),
	                   DefaultColormap(dpy, screen));
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)))
		xic = XCreateIC(xim, XNInputStyle,
		                XIMPreeditNothing | XIMStatusNothing,
		                XNClientWindow, win, XNFocusWindow, win, NULL);
	XMapRaised(dpy, win);
	grabkb();
}

static void
run(void)
{
	XEvent ev;

	drawmenu();
	while (running && !XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, None))
			continue;
		switch (ev.type) {
		case Expose:
			if (!ev.xexpose.count)
				drawmenu();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

/* hmenu -l: print the EWMH window list as "TITLE\thmenu -a 0xID" items */
static int
listwindows(void)
{
	Atom netclientlist, netwmname, utf8, real;
	Window *list;
	char title[256], *name;
	int fmt, k;
	unsigned long i, n = 0, extra;
	unsigned char *p = NULL, *tp;

	if (!(dpy = XOpenDisplay(NULL)))
		die("hmenu: cannot open display\n");
	root = RootWindow(dpy, DefaultScreen(dpy));
	netclientlist = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netwmname = XInternAtom(dpy, "_NET_WM_NAME", False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	if (XGetWindowProperty(dpy, root, netclientlist, 0L, 1024L, False,
	                       XA_WINDOW, &real, &fmt, &n, &extra,
	                       &p) != Success ||
	    !p)
		return 0;
	list = (Window *)p;
	for (i = 0; i < n; i++) {
		title[0] = '\0';
		tp = NULL;
		if (XGetWindowProperty(dpy, list[i], netwmname, 0L, 64L, False,
		                       utf8, &real, &fmt, &extra, &extra,
		                       &tp) == Success &&
		    tp) {
			snprintf(title, sizeof(title), "%s", (char *)tp);
			XFree(tp);
		} else if (XFetchName(dpy, list[i], &name) && name) {
			snprintf(title, sizeof(title), "%s", name);
			XFree(name);
		}
		for (k = 0; title[k]; k++) /* tabs split display from action */
			if (title[k] == '\t' || title[k] == '\n')
				title[k] = ' ';
		printf("%s\thmenu -a 0x%lx\n", title[0] ? title : "(untitled)",
		       (unsigned long)list[i]);
	}
	XFree(p);
	XCloseDisplay(dpy);
	return 0;
}

/* hmenu -a windowid: ask the WM to activate the window, like a pager */
static int
activatewindow(const char *s)
{
	XEvent ev;
	Window w = (Window)strtoul(s, NULL, 0);

	if (!w)
		usage();
	if (!(dpy = XOpenDisplay(NULL)))
		die("hmenu: cannot open display\n");
	root = RootWindow(dpy, DefaultScreen(dpy));
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW",
	                                      False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 2; /* source: pager */
	XSendEvent(dpy, root, False,
	           SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XSync(dpy, False);
	XCloseDisplay(dpy);
	return 0;
}

/* single-quote s for the shell into dst */
static void
shquote(char *dst, size_t size, const char *s)
{
	size_t i = 0;

	if (i < size)
		dst[i++] = '\'';
	for (; *s && i + 6 < size; s++)
		if (*s == '\'') {
			memcpy(dst + i, "'\\''", 4);
			i += 4;
		} else
			dst[i++] = *s;
	if (i < size)
		dst[i++] = '\'';
	dst[i < size ? i : size - 1] = '\0';
}

/* print a "NAME\tcommand" item for one desktop entry file, if it is a
 * visible application: Exec with the %f/%u/... field codes dropped, run
 * from Path when set, wrapped in the terminal when Terminal=true */
static void
desktopentry(const char *file)
{
	FILE *f;
	char line[1024], name[1024], exec[1024], path[1024];
	char cmd[5504], q[4352]; /* sized so snprintf never truncates */
	char *s, *d;
	int k, inde = 0, app = 0, term = 0, hide = 0;

	if (!(f = fopen(file, "r")))
		return;
	name[0] = exec[0] = path[0] = '\0';
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') { /* only the main group, not actions */
			inde = !strcmp(line, "[Desktop Entry]");
			continue;
		}
		if (!inde)
			continue;
		if (!strncmp(line, "Name=", 5))
			snprintf(name, sizeof(name), "%s", line + 5);
		else if (!strncmp(line, "Exec=", 5))
			snprintf(exec, sizeof(exec), "%s", line + 5);
		else if (!strncmp(line, "Path=", 5))
			snprintf(path, sizeof(path), "%s", line + 5);
		else if (!strncmp(line, "Type=", 5))
			app = !strcmp(line + 5, "Application");
		else if (!strncmp(line, "Terminal=", 9))
			term = !strcmp(line + 9, "true");
		else if (!strncmp(line, "NoDisplay=", 10))
			hide |= !strcmp(line + 10, "true");
		else if (!strncmp(line, "Hidden=", 7))
			hide |= !strcmp(line + 7, "true");
		else if (!strncmp(line, "OnlyShowIn=", 11))
			hide = 1; /* DE-specific entry; we are no DE */
	}
	fclose(f);
	if (!app || hide || !name[0] || !exec[0])
		return;
	for (s = d = exec; *s; s++) /* drop field codes, unescape %% */
		if (*s == '%') {
			if (s[1] == '%')
				*d++ = *++s;
			else if (s[1])
				s++;
		} else
			*d++ = *s;
	*d = '\0';
	for (k = 0; name[k]; k++) /* tabs split display from action */
		if (name[k] == '\t')
			name[k] = ' ';
	if (path[0]) { /* run from Path: prepend a cd */
		shquote(q, sizeof(q), path);
		snprintf(cmd, sizeof(cmd), "cd %s && %s", q, exec);
	} else
		snprintf(cmd, sizeof(cmd), "%s", exec);
	if (term) {
		shquote(q, sizeof(q), cmd);
		printf("%s\t%s -e sh -c %s\n", name, terminal, q);
	} else
		printf("%s\t%s\n", name, cmd);
}

/* hmenu -d: print XDG desktop applications as "NAME\tcommand" items,
 * scanning $XDG_DATA_HOME then $XDG_DATA_DIRS (earlier dirs win) */
static int
listapps(void)
{
	struct { char *key; int value; } *seen = NULL;
	char dirs[2048], dir[2560], file[4096];
	const char *home, *env;
	char *d, *sep;
	DIR *dp;
	struct dirent *e;
	size_t n;

	sh_new_strdup(seen);
	env = getenv("XDG_DATA_HOME");
	home = getenv("HOME");
	if (env && *env)
		n = (size_t)snprintf(dirs, sizeof(dirs), "%s", env);
	else
		n = (size_t)snprintf(dirs, sizeof(dirs), "%s/.local/share",
		                     home ? home : ".");
	env = getenv("XDG_DATA_DIRS");
	if (!env || !*env)
		env = "/usr/local/share:/usr/share";
	if (n < sizeof(dirs))
		snprintf(dirs + n, sizeof(dirs) - n, ":%s", env);
	for (d = dirs; d; d = sep ? sep + 1 : NULL) {
		if ((sep = strchr(d, ':')))
			*sep = '\0';
		if (!*d)
			continue;
		snprintf(dir, sizeof(dir), "%s/applications", d);
		if (!(dp = opendir(dir)))
			continue;
		while ((e = readdir(dp))) {
			n = strlen(e->d_name);
			if (n < 9 || strcmp(e->d_name + n - 8, ".desktop"))
				continue;
			if (shgeti(seen, e->d_name) >= 0)
				continue; /* shadowed by an earlier dir */
			shput(seen, e->d_name, 1);
			snprintf(file, sizeof(file), "%s/%s", dir, e->d_name);
			desktopentry(file);
		}
		closedir(dp);
	}
	return 0;
}

/* a source argument is a mode name or itself a list command */
static void
loadsource(const char *arg)
{
	size_t i;

	if (arg[0] == '-')
		usage();
	for (i = 0; i < LEN(modes); i++)
		if (!strcmp(arg, modes[i].name)) {
			loadlist(modes[i].cmd);
			return;
		}
	loadlist(arg);
}

int
main(int argc, char *argv[])
{
	int a;

	setlocale(LC_CTYPE, "");
	loadconfig();
	if (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-l") && argc == 2)
			return listwindows();
		if (!strcmp(argv[1], "-d") && argc == 2)
			return listapps();
		if (!strcmp(argv[1], "-a") && argc == 3)
			return activatewindow(argv[2]);
		if (!strcmp(argv[1], "-v") && argc == 2) {
			printf("hmenu %s\n", HMENU_VERSION);
			return 0;
		}
		usage();
	}
	/* sources are appended in order; with an empty query the list
	 * shows them exactly that way, unsorted */
	for (a = 1; a < argc; a++)
		loadsource(argv[a]);
	if (argc < 2)
		for (a = 0; a < (int)LEN(defargs); a++)
			loadsource(defargs[a]);
	arrput(listbuf, '\0');
	splitlines(listbuf, &items);
	setup();
	filter();
	run();
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
	return 0;
}

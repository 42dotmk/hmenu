/* hconf.h - runtime configuration for the hackable tools; one tiny
 * reader, vendored per repo like stb_ds.h.
 *
 * The tool's compiled-in config.h stays the defaults and the source of
 * truth for structure; hconf overlays the values from
 * $XDG_CONFIG_HOME/hackable/<tool>.conf (default ~/.config/hackable/)
 * when that file exists. Nothing else is read.
 *
 * Format, deliberately dumb:
 *
 *   # comment (whole-line only)
 *   key = value
 *   [section]
 *   key = value
 *
 * Values are the trimmed remainder of the line, taken raw: no quoting
 * and no escapes, so shell commands paste in unchanged; an embedded
 * tab is a literal tab. A trailing backslash continues the value on
 * the next line (the continuation's leading whitespace is dropped),
 * which also means a value cannot itself end in a backslash. A
 * repeated key: the last one wins.
 *
 * Every problem - unreadable file, unparsable line, unknown key, bad
 * number - is a line in hconf_diag(), never a crash and never a stop:
 * the tool starts with its defaults for whatever was wrong. Print the
 * diagnostics as a warning at startup and verbatim under --check.
 *
 * Use: #define HCONF_IMPLEMENTATION in one translation unit.
 */
#ifndef HCONF_H
#define HCONF_H

#include <stddef.h>

/* read <tool>.conf; 0 = loaded or absent, -1 = present but unreadable
 * (also noted in the diagnostics). Call once, first. */
int hconf_load(const char *tool);
const char *hconf_path(void); /* file consulted; "" if none exists */

/* last value for key ("" section = top level); NULL if absent */
const char *hconf_get(const char *sec, const char *key);
const char *hconf_str(const char *sec, const char *key, const char *def);
long hconf_int(const char *sec, const char *key, long def);

/* every entry, in file order (repeated keys included) */
int hconf_count(void);
void hconf_entry(int i, const char **sec, const char **key, const char **val);

/* note every entry that matches no pattern. A pattern is "key" (top
 * level) or "sec.key"; a section pattern may end in '*' to cover a
 * family, e.g. "mode *.cmd". */
void hconf_check(const char *const *known, int n);

const char *hconf_diag(void); /* accumulated problems; "" if none */

#endif /* HCONF_H */

#ifdef HCONF_IMPLEMENTATION

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *sec, *key, *val;
    int line;
} HconfEnt;

static HconfEnt *hconf_ents;
static int hconf_n;
static char hconf_file[4096];
static char hconf_dbuf[4096];

static void hconf_note(int line, const char *what, const char *arg) {
    size_t n = strlen(hconf_dbuf);

    if (n + 2 >= sizeof hconf_dbuf)
        return;
    if (line)
        snprintf(hconf_dbuf + n, sizeof hconf_dbuf - n, "%s:%d: %s%s\n",
                 hconf_file, line, what, arg);
    else
        snprintf(hconf_dbuf + n, sizeof hconf_dbuf - n, "%s: %s%s\n",
                 hconf_file, what, arg);
}

static char *hconf_trim(char *s) {
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s &&
           (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
        *--e = '\0';
    return s;
}

int hconf_load(const char *tool) {
    char line[8192], *s, *eq, *val, *key, sec[256] = "";
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    FILE *f;
    int ln = 0, startln;
    size_t n;

    if (xdg && *xdg)
        snprintf(hconf_file, sizeof hconf_file, "%s/hackable/%s.conf", xdg,
                 tool);
    else
        snprintf(hconf_file, sizeof hconf_file, "%s/.config/hackable/%s.conf",
                 home ? home : "", tool);
    if (!(f = fopen(hconf_file, "r"))) {
        hconf_file[0] = '\0';
        return 0; /* no file: defaults rule, not an error */
    }
    while (fgets(line, sizeof line, f)) {
        ln++;
        s = hconf_trim(line);
        if (!*s || *s == '#')
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close || hconf_trim(close + 1)[0]) {
                hconf_note(ln, "bad section header: ", s);
                continue;
            }
            *close = '\0';
            snprintf(sec, sizeof sec, "%s", hconf_trim(s + 1));
            continue;
        }
        if (!(eq = strchr(s, '='))) {
            hconf_note(ln, "not a 'key = value' line: ", s);
            continue;
        }
        *eq = '\0';
        val = NULL;
        startln = ln;
        /* the line buffer is reused for continuations: copy the key out */
        key = strdup(hconf_trim(s));
        eq = hconf_trim(eq + 1);
        for (;;) { /* trailing backslash: the value continues */
            int cont = 0;
            n = strlen(eq);
            if (n && eq[n - 1] == '\\') {
                eq[--n] = '\0';
                cont = 1;
            }
            val = realloc(val, (val ? strlen(val) : 0) + n + 1);
            if (!val)
                break;
            if (startln == ln)
                memcpy(val, eq, n + 1);
            else
                memcpy(val + strlen(val), eq, n + 1);
            if (!cont || !fgets(line, sizeof line, f))
                break;
            ln++;
            eq = hconf_trim(line);
        }
        if (!val || !key || !*key) {
            hconf_note(startln, "bad entry: ", key ? key : "");
            free(val);
            free(key);
            continue;
        }
        hconf_ents =
            realloc(hconf_ents, ((size_t)hconf_n + 1) * sizeof *hconf_ents);
        hconf_ents[hconf_n].sec = strdup(sec);
        hconf_ents[hconf_n].key = key;
        hconf_ents[hconf_n].val = val;
        hconf_ents[hconf_n].line = startln;
        hconf_n++;
    }
    fclose(f);
    return 0;
}

const char *hconf_path(void) { return hconf_file; }

const char *hconf_get(const char *sec, const char *key) {
    int i;
    const char *v = NULL;

    for (i = 0; i < hconf_n; i++)
        if (!strcmp(hconf_ents[i].sec, sec) && !strcmp(hconf_ents[i].key, key))
            v = hconf_ents[i].val;
    return v;
}

const char *hconf_str(const char *sec, const char *key, const char *def) {
    const char *v = hconf_get(sec, key);

    return v ? v : def;
}

long hconf_int(const char *sec, const char *key, long def) {
    const char *v = hconf_get(sec, key);
    char *end;
    long r;

    if (!v)
        return def;
    r = strtol(v, &end, 10);
    if (end == v || *end) {
        hconf_note(0, "not a number, default kept: ", key);
        return def;
    }
    return r;
}

int hconf_count(void) { return hconf_n; }

void hconf_entry(int i, const char **sec, const char **key, const char **val) {
    *sec = hconf_ents[i].sec;
    *key = hconf_ents[i].key;
    *val = hconf_ents[i].val;
}

/* "sec pattern.key" where the section pattern may end in '*' */
static int hconf_match(const char *pat, const char *sec, const char *key) {
    const char *dot = strrchr(pat, '.');
    size_t sl;

    if (!dot)
        return !*sec && !strcmp(pat, key);
    if (strcmp(dot + 1, key))
        return 0;
    sl = (size_t)(dot - pat);
    if (sl && pat[sl - 1] == '*')
        return strlen(sec) >= sl - 1 && !strncmp(pat, sec, sl - 1);
    return strlen(sec) == sl && !strncmp(pat, sec, sl);
}

void hconf_check(const char *const *known, int n) {
    char full[512];
    int i, k;

    for (i = 0; i < hconf_n; i++) {
        for (k = 0; k < n; k++)
            if (hconf_match(known[k], hconf_ents[i].sec, hconf_ents[i].key))
                break;
        if (k == n) {
            if (hconf_ents[i].sec[0])
                snprintf(full, sizeof full, "[%s] %s", hconf_ents[i].sec,
                         hconf_ents[i].key);
            else
                snprintf(full, sizeof full, "%s", hconf_ents[i].key);
            hconf_note(hconf_ents[i].line, "unknown key: ", full);
        }
    }
}

const char *hconf_diag(void) { return hconf_dbuf; }

#endif /* HCONF_IMPLEMENTATION */

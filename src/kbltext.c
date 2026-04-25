/*---------------------------------------------------------------------------/
/  kyblText — minimal terminal text editor for kyblRTOS
/
/  Inspired by kilo (antirez). Single-buffer, no syntax highlighting, no
/  undo. Loads small text files from kyblFS, lets the user edit them with
/  arrow-key navigation, and writes them back on save. Designed to fit in
/  the existing 128 KB FreeRTOS heap with room for the rest of the system,
/  so we cap file size at 16 KB.
/
/  Key bindings (heads-up: some terminals catch ^S as XOFF — disable
/  software flow-control or use `stty -ixon` if it doesn't work):
/    Arrow keys / Home / End / PgUp / PgDn — navigate
/    Backspace                             — delete char to left
/    Delete                                — delete char under cursor
/    Enter                                 — split line / new line
/    TAB                                   — insert literal tab
/    Ctrl-S                                — save (prompts if unnamed)
/    Ctrl-Q                                — quit (prompts if dirty)
/    Ctrl-O                                — open another file
/    ESC                                   — cancel a prompt
/---------------------------------------------------------------------------*/

#include "commands.h"
#include "kyblFS.h"
#include "kyblrtos.h"

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>

#define KBLT_VERSION         "kyblText 0.1"
#define KBLT_TAB_STOP        4
#define KBLT_MAX_FILE_BYTES  (16 * 1024)   /* hard cap so we don't blow heap */
#define KBLT_NAME_MAX        64

#define KBLT_DEFAULT_ROWS    24
#define KBLT_DEFAULT_COLS    80

#define CTRL(c)  ((c) & 0x1F)

enum kbltext_key {
    KEY_BACKSPACE = 127,
    KEY_LEFT      = 1000,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_DEL,
};

/* ── one logical line in the buffer ─────────────────────────────────── */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} eline_t;

/* ── global editor state — there's only ever one editor running at a time
   from the shell, so a single static instance is fine. memset on entry to
   reset between invocations. ──────────────────────────────────────────── */
typedef struct {
    eline_t *rows;
    int      n_rows;
    int      cap_rows;

    int      cx, cy;          /* cursor in file coords (cx = byte col)   */
    int      rx;              /* cursor render-col (handles TABs)         */
    int      rowoff, coloff;  /* viewport scroll                          */

    int      screen_rows;     /* total rows incl. status + msg bars       */
    int      screen_cols;

    char     filename[KBLT_NAME_MAX];
    bool     dirty;

    char     status_msg[80];
    uint32_t status_time_us;
} kbltext_t;

static kbltext_t E;

/* ══════════════════════════════════════════════════════════════════════════
 *  LINE BUFFER PRIMITIVES
 * ══════════════════════════════════════════════════════════════════════════ */

/* FreeRTOS heap_4 has no realloc, so growing a buffer means malloc + memcpy
   + free. Doubling capacity keeps amortised cost reasonable. */
static int eline_grow(eline_t *l, int needed) {
    if (l->cap >= needed) return 0;
    int newcap = l->cap ? l->cap : 32;
    while (newcap < needed) newcap *= 2;
    char *nb = pvPortMalloc((size_t)newcap);
    if (!nb) return -1;
    if (l->len > 0 && l->buf) memcpy(nb, l->buf, (size_t)l->len);
    if (l->buf) vPortFree(l->buf);
    l->buf = nb;
    l->cap = newcap;
    return 0;
}

static void eline_free(eline_t *l) {
    if (l->buf) vPortFree(l->buf);
    l->buf = NULL;
    l->len = 0;
    l->cap = 0;
}

static int eline_set(eline_t *l, const char *s, int n) {
    if (eline_grow(l, n + 1) != 0) return -1;
    if (n > 0) memcpy(l->buf, s, (size_t)n);
    l->len = n;
    l->buf[n] = '\0';
    return 0;
}

static int eline_insert_char(eline_t *l, int at, int c) {
    if (at < 0 || at > l->len) at = l->len;
    if (eline_grow(l, l->len + 2) != 0) return -1;
    memmove(&l->buf[at + 1], &l->buf[at], (size_t)(l->len - at));
    l->buf[at] = (char)c;
    l->len++;
    l->buf[l->len] = '\0';
    return 0;
}

static void eline_del_char(eline_t *l, int at) {
    if (at < 0 || at >= l->len) return;
    memmove(&l->buf[at], &l->buf[at + 1], (size_t)(l->len - at - 1));
    l->len--;
    if (l->cap > 0) l->buf[l->len] = '\0';
}

static int eline_append(eline_t *l, const char *s, int n) {
    if (n <= 0) return 0;
    if (eline_grow(l, l->len + n + 1) != 0) return -1;
    memcpy(&l->buf[l->len], s, (size_t)n);
    l->len += n;
    l->buf[l->len] = '\0';
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  ROW ARRAY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════ */
static int rows_grow(int needed) {
    if (E.cap_rows >= needed) return 0;
    int newcap = E.cap_rows ? E.cap_rows : 64;
    while (newcap < needed) newcap *= 2;
    eline_t *nr = pvPortMalloc((size_t)newcap * sizeof(eline_t));
    if (!nr) return -1;
    memset(nr, 0, (size_t)newcap * sizeof(eline_t));
    if (E.n_rows > 0 && E.rows) {
        memcpy(nr, E.rows, (size_t)E.n_rows * sizeof(eline_t));
    }
    if (E.rows) vPortFree(E.rows);
    E.rows = nr;
    E.cap_rows = newcap;
    return 0;
}

static int row_insert_at(int at, const char *s, int n) {
    if (at < 0 || at > E.n_rows) return -1;
    if (rows_grow(E.n_rows + 1) != 0) return -1;
    memmove(&E.rows[at + 1], &E.rows[at],
            (size_t)(E.n_rows - at) * sizeof(eline_t));
    memset(&E.rows[at], 0, sizeof(eline_t));
    if (eline_set(&E.rows[at], s, n) != 0) {
        memmove(&E.rows[at], &E.rows[at + 1],
                (size_t)(E.n_rows - at) * sizeof(eline_t));
        return -1;
    }
    E.n_rows++;
    return 0;
}

static void row_delete_at(int at) {
    if (at < 0 || at >= E.n_rows) return;
    eline_free(&E.rows[at]);
    memmove(&E.rows[at], &E.rows[at + 1],
            (size_t)(E.n_rows - at - 1) * sizeof(eline_t));
    E.n_rows--;
    memset(&E.rows[E.n_rows], 0, sizeof(eline_t));
}

/* Convert a byte-column (cx) to a render column (rx) — TABs expand to the
   next multiple of KBLT_TAB_STOP. */
static int row_cx_to_rx(eline_t *l, int cx) {
    int rx = 0;
    if (!l || !l->buf) return 0;
    if (cx > l->len) cx = l->len;
    for (int i = 0; i < cx; i++) {
        if (l->buf[i] == '\t') rx += KBLT_TAB_STOP - (rx % KBLT_TAB_STOP);
        else rx++;
    }
    return rx;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  EDITS  (insert, newline, delete)
 * ══════════════════════════════════════════════════════════════════════════ */
static void editor_insert_char(int c) {
    if (E.cy == E.n_rows) {
        if (row_insert_at(E.n_rows, "", 0) != 0) return;
    }
    if (eline_insert_char(&E.rows[E.cy], E.cx, c) == 0) {
        E.cx++;
        E.dirty = true;
    }
}

static void editor_insert_newline(void) {
    if (E.cy >= E.n_rows) {
        if (row_insert_at(E.n_rows, "", 0) != 0) return;
    }
    if (E.cx == 0) {
        if (row_insert_at(E.cy, "", 0) != 0) return;
    } else {
        eline_t *cur = &E.rows[E.cy];
        int tail_len = cur->len - E.cx;
        char *tail_save = NULL;
        if (tail_len > 0) {
            tail_save = pvPortMalloc((size_t)tail_len);
            if (!tail_save) return;
            memcpy(tail_save, &cur->buf[E.cx], (size_t)tail_len);
        }
        cur->len = E.cx;
        if (cur->buf) cur->buf[cur->len] = '\0';
        if (row_insert_at(E.cy + 1, tail_save ? tail_save : "", tail_len) != 0) {
            /* Best-effort recovery: restore the line we just truncated. */
            if (tail_save) {
                eline_append(cur, tail_save, tail_len);
                vPortFree(tail_save);
            }
            return;
        }
        if (tail_save) vPortFree(tail_save);
    }
    E.cy++;
    E.cx = 0;
    E.dirty = true;
}

static void editor_del_char(void) {
    if (E.cy >= E.n_rows) return;
    if (E.cx == 0 && E.cy == 0) return;

    eline_t *cur = &E.rows[E.cy];
    if (E.cx > 0) {
        eline_del_char(cur, E.cx - 1);
        E.cx--;
    } else {
        /* cursor at col 0 → merge current line into previous */
        eline_t *prev = &E.rows[E.cy - 1];
        E.cx = prev->len;
        if (cur->buf && cur->len > 0)
            eline_append(prev, cur->buf, cur->len);
        row_delete_at(E.cy);
        E.cy--;
    }
    E.dirty = true;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  FILE I/O  (kyblFS-backed)
 * ══════════════════════════════════════════════════════════════════════════ */
static void editor_clear_buffer(void) {
    if (E.rows) {
        for (int i = 0; i < E.n_rows; i++) eline_free(&E.rows[i]);
        vPortFree(E.rows);
        E.rows = NULL;
    }
    E.n_rows = 0;
    E.cap_rows = 0;
    E.cx = E.cy = E.rx = 0;
    E.rowoff = E.coloff = 0;
    E.dirty = false;
}

/* Returns:
   0 = loaded existing file
   1 = path didn't exist; started blank ("new file")
  -1 = I/O error
  -2 = too large
  -3 = path is a directory                                                   */
static int editor_load_file(const char *path) {
    if (!kyblFS_is_mounted()) return -1;

    /* Probe with stat — if it's a directory, refuse. If it doesn't exist,
       fall through to the open() which will also fail and we'll start
       blank. */
    kybl_finfo_t info;
    int sr = kyblFS_stat(path, &info);
    if (sr == KYBLFS_OK && (info.attr & KYBLFS_ATTR_DIR)) return -3;

    kybl_file_t *f = kyblFS_open(path, KYBLFS_READ);
    if (!f) {
        /* New file — start with one empty line. */
        if (row_insert_at(0, "", 0) != 0) return -1;
        return 1;
    }

    uint32_t total = kyblFS_size(f);
    if (total > KBLT_MAX_FILE_BYTES) {
        kyblFS_close(f);
        return -2;
    }

    char *buf = pvPortMalloc((size_t)total + 1);
    if (!buf) { kyblFS_close(f); return -1; }

    int got = kyblFS_read(f, buf, total);
    kyblFS_close(f);
    if (got < 0) { vPortFree(buf); return -1; }
    if ((uint32_t)got < total) total = (uint32_t)got;
    buf[total] = '\0';

    /* Split on \n, swallow CR for CRLF tolerance. */
    uint32_t i = 0, line_start = 0;
    while (i <= total) {
        if (i == total || buf[i] == '\n') {
            int len = (int)(i - line_start);
            if (len > 0 && buf[line_start + len - 1] == '\r') len--;
            if (row_insert_at(E.n_rows, &buf[line_start], len) != 0) {
                vPortFree(buf);
                return -1;
            }
            line_start = i + 1;
        }
        i++;
    }
    vPortFree(buf);
    if (E.n_rows == 0) row_insert_at(0, "", 0);
    return 0;
}

/* Returns total bytes written or -1 on error. */
static int editor_save_file(const char *path) {
    if (!kyblFS_is_mounted()) return -1;

    kybl_file_t *f = kyblFS_open(path,
        KYBLFS_WRITE | KYBLFS_CREATE | KYBLFS_TRUNCATE);
    if (!f) return -1;

    int total = 0;
    for (int i = 0; i < E.n_rows; i++) {
        eline_t *l = &E.rows[i];
        if (l->len > 0 && l->buf) {
            int n = kyblFS_write(f, l->buf, l->len);
            if (n < 0) { kyblFS_close(f); return -1; }
            total += n;
        }
        int n = kyblFS_write(f, "\n", 1);
        if (n < 0) { kyblFS_close(f); return -1; }
        total += n;
    }
    kyblFS_close(f);
    return total;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  STATUS-BAR MESSAGES
 * ══════════════════════════════════════════════════════════════════════════ */
static void editor_set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_time_us = time_us_32();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  INPUT — read one logical key (handles ESC sequences for arrows/home/end)
 * ══════════════════════════════════════════════════════════════════════════ */
static int read_key_blocking(void) {
    int c;
    do {
        c = getchar_timeout_us(100000);
    } while (c == PICO_ERROR_TIMEOUT);

    if (c != 0x1B) return c;

    /* Could be a real ESC press OR start of an escape sequence. Wait
       briefly — real ESC has no follow-up bytes. */
    int c2 = getchar_timeout_us(50000);
    if (c2 == PICO_ERROR_TIMEOUT) return 0x1B;

    if (c2 == '[') {
        int c3 = getchar_timeout_us(50000);
        if (c3 == PICO_ERROR_TIMEOUT) return 0x1B;

        if (c3 >= '0' && c3 <= '9') {
            int c4 = getchar_timeout_us(50000);
            if (c4 == PICO_ERROR_TIMEOUT) return 0x1B;
            if (c4 == '~') {
                switch (c3) {
                    case '1': case '7': return KEY_HOME;
                    case '4': case '8': return KEY_END;
                    case '3':           return KEY_DEL;
                    case '5':           return KEY_PGUP;
                    case '6':           return KEY_PGDN;
                    default: break;
                }
            }
            return 0x1B;
        }
        switch (c3) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            default:  return 0x1B;
        }
    }
    if (c2 == 'O') {
        int c3 = getchar_timeout_us(50000);
        if (c3 == 'H') return KEY_HOME;
        if (c3 == 'F') return KEY_END;
        return 0x1B;
    }
    return 0x1B;
}

static void drain_input_quick(void) {
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { }
}

/* Try to detect terminal size via DSR (cursor position report). Sends
   "go to (999,999)" then "report position", parses ESC[<rows>;<cols>R.
   Falls back to 24x80 if the terminal doesn't respond. */
static void editor_get_window_size(void) {
    drain_input_quick();
    printf("\033[s\033[999;999H\033[6n");
    fflush(stdout);

    char buf[32];
    int  n = 0;
    uint32_t start = time_us_32();
    while (n < (int)sizeof(buf) - 1 && (time_us_32() - start) < 200000) {
        int c = getchar_timeout_us(20000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        buf[n++] = (char)c;
        if (c == 'R') break;
    }
    buf[n] = '\0';
    printf("\033[u");
    fflush(stdout);

    int rows = 0, cols = 0;
    if (n >= 6 && buf[0] == 0x1B && buf[1] == '[') {
        if (sscanf(&buf[2], "%d;%d", &rows, &cols) == 2 &&
                rows > 0 && cols > 0) {
            E.screen_rows = rows;
            E.screen_cols = cols;
            return;
        }
    }
    E.screen_rows = KBLT_DEFAULT_ROWS;
    E.screen_cols = KBLT_DEFAULT_COLS;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  RENDER
 * ══════════════════════════════════════════════════════════════════════════ */
static void editor_scroll(void) {
    E.rx = 0;
    if (E.cy < E.n_rows) E.rx = row_cx_to_rx(&E.rows[E.cy], E.cx);
    int edit_rows = E.screen_rows - 2;
    if (edit_rows < 1) edit_rows = 1;

    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + edit_rows) E.rowoff = E.cy - edit_rows + 1;
    if (E.rx < E.coloff) E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screen_cols) E.coloff = E.rx - E.screen_cols + 1;
}

static void editor_draw_rows(void) {
    int edit_rows = E.screen_rows - 2;
    for (int y = 0; y < edit_rows; y++) {
        int filerow = y + E.rowoff;
        printf("\033[%d;1H\033[K", y + 1);
        if (filerow >= E.n_rows) {
            if (E.n_rows == 0 && y == edit_rows / 3) {
                char welcome[80];
                int wn = snprintf(welcome, sizeof(welcome),
                                  "%s — ^S=save  ^Q=quit  ^O=open",
                                  KBLT_VERSION);
                int pad = (E.screen_cols - wn) / 2;
                if (pad > 0) {
                    printf(KYBL_ANSI_CYAN "~" KYBL_ANSI_RESET);
                    for (int i = 1; i < pad; i++) putchar(' ');
                    printf("%s", welcome);
                } else {
                    printf("%s", welcome);
                }
            } else {
                printf(KYBL_ANSI_CYAN "~" KYBL_ANSI_RESET);
            }
        } else {
            eline_t *l = &E.rows[filerow];
            /* Render with TAB expansion; honour coloff for horizontal scroll. */
            int rcol = 0;
            for (int i = 0; i < l->len; i++) {
                char ch = l->buf[i];
                if (ch == '\t') {
                    int spaces = KBLT_TAB_STOP - (rcol % KBLT_TAB_STOP);
                    for (int s = 0; s < spaces; s++) {
                        if (rcol >= E.coloff &&
                                rcol < E.coloff + E.screen_cols)
                            putchar(' ');
                        rcol++;
                    }
                } else {
                    if (rcol >= E.coloff && rcol < E.coloff + E.screen_cols) {
                        if ((unsigned char)ch < 0x20) putchar('?');
                        else putchar(ch);
                    }
                    rcol++;
                }
                if (rcol >= E.coloff + E.screen_cols) break;
            }
        }
    }
}

static void editor_draw_status_bar(void) {
    /* Inverse-video bar on screen_rows - 1 (i.e. 1-based row screen_rows-1). */
    printf("\033[%d;1H\033[K\033[7m", E.screen_rows - 1);

    char left[80];
    int  ln = snprintf(left, sizeof(left), " %s%s%s",
                       E.filename[0] ? E.filename : "[No name]",
                       E.dirty ? "  " : "",
                       E.dirty ? "[+]" : "");
    char right[40];
    int  rn = snprintf(right, sizeof(right), "Ln %d/%d, Col %d ",
                       E.cy + 1, E.n_rows ? E.n_rows : 1, E.rx + 1);

    int max_left = E.screen_cols - rn;
    if (max_left < 0) max_left = 0;
    if (ln > max_left) ln = max_left;
    printf("%.*s", ln, left);

    int padlen = E.screen_cols - ln - rn;
    for (int i = 0; i < padlen; i++) putchar(' ');
    printf("%s\033[m", right);
}

static void editor_draw_message_bar(void) {
    printf("\033[%d;1H\033[K", E.screen_rows);
    /* Status messages get 5 s of screen time, then we revert to the
       persistent help line. */
    bool have_msg = E.status_msg[0] &&
                    (time_us_32() - E.status_time_us) < 5000000u;
    if (have_msg) {
        int n = (int)strlen(E.status_msg);
        if (n > E.screen_cols) n = E.screen_cols;
        printf("%.*s", n, E.status_msg);
    } else {
        printf(KYBL_ANSI_CYAN "^S" KYBL_ANSI_RESET " save  "
               KYBL_ANSI_CYAN "^Q" KYBL_ANSI_RESET " quit  "
               KYBL_ANSI_CYAN "^O" KYBL_ANSI_RESET " open  "
               KYBL_ANSI_CYAN "ESC" KYBL_ANSI_RESET " cancel-prompt");
    }
}

static void editor_refresh_screen(void) {
    editor_scroll();

    printf("\033[?25l");                  /* hide cursor while redrawing  */
    printf("\033[H");                     /* home                          */
    editor_draw_rows();
    editor_draw_status_bar();
    editor_draw_message_bar();
    /* Place cursor at edit position. Clamp visible row so we don't go
       below the edit area. */
    int vrow = E.cy - E.rowoff + 1;
    int vcol = E.rx - E.coloff + 1;
    if (vrow < 1) vrow = 1;
    if (vrow > E.screen_rows - 2) vrow = E.screen_rows - 2;
    if (vcol < 1) vcol = 1;
    printf("\033[%d;%dH\033[?25h", vrow, vcol);
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  PROMPTS  (text input + yes/no/cancel)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Reads a single line from the user via the message bar. Returns 1 if
   non-empty input was confirmed, 0 if the user pressed ESC. */
static int editor_prompt(const char *prompt, char *out, int maxlen) {
    int n = 0;
    out[0] = '\0';
    for (;;) {
        editor_set_status("%s%s", prompt, out);
        editor_refresh_screen();
        int c = read_key_blocking();
        if (c == 0x1B || c == CTRL('c') || c == CTRL('q')) {
            editor_set_status("");
            return 0;
        }
        if (c == '\r' || c == '\n') {
            if (n > 0) { editor_set_status(""); return 1; }
            continue;
        }
        if (c == KEY_BACKSPACE || c == 0x08) {
            if (n > 0) { n--; out[n] = '\0'; }
            continue;
        }
        if (c >= 32 && c < 127 && n < maxlen - 1) {
            out[n++] = (char)c;
            out[n]   = '\0';
        }
    }
}

/* yes / no / cancel — returns 1=yes, 0=no, -1=cancel */
static int editor_prompt_ync(const char *prompt) {
    for (;;) {
        editor_set_status("%s [y/n/ESC]", prompt);
        editor_refresh_screen();
        int c = read_key_blocking();
        if (c == 0x1B || c == CTRL('c')) return -1;
        if (c == 'y' || c == 'Y')        return 1;
        if (c == 'n' || c == 'N')        return 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  CURSOR MOVEMENT
 * ══════════════════════════════════════════════════════════════════════════ */
static void editor_move_cursor(int key) {
    eline_t *line = (E.cy < E.n_rows) ? &E.rows[E.cy] : NULL;

    switch (key) {
    case KEY_LEFT:
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.rows[E.cy].len;
        }
        break;
    case KEY_RIGHT:
        if (line && E.cx < line->len) {
            E.cx++;
        } else if (line && E.cx == line->len && E.cy + 1 < E.n_rows) {
            E.cy++;
            E.cx = 0;
        }
        break;
    case KEY_UP:
        if (E.cy > 0) E.cy--;
        break;
    case KEY_DOWN:
        if (E.cy + 1 < E.n_rows) E.cy++;
        break;
    case KEY_HOME:
        E.cx = 0;
        break;
    case KEY_END:
        if (line) E.cx = line->len;
        break;
    case KEY_PGUP:
    case KEY_PGDN: {
        int rows = E.screen_rows - 2;
        if (rows < 1) rows = 1;
        if (key == KEY_PGUP) {
            E.cy -= rows;
            if (E.cy < 0) E.cy = 0;
        } else {
            E.cy += rows;
            if (E.cy >= E.n_rows) E.cy = E.n_rows ? E.n_rows - 1 : 0;
        }
        break;
    }
    default: break;
    }

    /* Snap cx back to (new) line length */
    line = (E.cy < E.n_rows) ? &E.rows[E.cy] : NULL;
    int linelen = line ? line->len : 0;
    if (E.cx > linelen) E.cx = linelen;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  HIGH-LEVEL ACTIONS  (save / open / quit-with-prompt)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Returns 0 on success, -1 on cancel/error. */
static int editor_do_save(void) {
    if (E.filename[0] == '\0') {
        char namebuf[KBLT_NAME_MAX];
        if (!editor_prompt("Save as: ", namebuf, sizeof(namebuf))) {
            editor_set_status("Save cancelled.");
            return -1;
        }
        strncpy(E.filename, namebuf, sizeof(E.filename) - 1);
        E.filename[sizeof(E.filename) - 1] = '\0';
    }
    int n = editor_save_file(E.filename);
    if (n >= 0) {
        E.dirty = false;
        editor_set_status("Wrote %d bytes to %s", n, E.filename);
        return 0;
    }
    editor_set_status("Save FAILED — %s not writable?", E.filename);
    return -1;
}

static int editor_handle_open(void) {
    if (E.dirty) {
        int yn = editor_prompt_ync("Discard unsaved changes?");
        if (yn != 1) {
            editor_set_status("Open cancelled.");
            return -1;
        }
    }
    char buf[KBLT_NAME_MAX];
    if (!editor_prompt("Open: ", buf, sizeof(buf))) {
        editor_set_status("Open cancelled.");
        return -1;
    }
    editor_clear_buffer();
    strncpy(E.filename, buf, sizeof(E.filename) - 1);
    E.filename[sizeof(E.filename) - 1] = '\0';
    int r = editor_load_file(E.filename);
    if      (r == -2) editor_set_status("File too large (>%d KB).",
                                        KBLT_MAX_FILE_BYTES / 1024);
    else if (r == -3) editor_set_status("'%s' is a directory.", E.filename);
    else if (r ==  0) editor_set_status("Loaded %s (%d line%s)",
                                        E.filename, E.n_rows,
                                        E.n_rows == 1 ? "" : "s");
    else if (r ==  1) editor_set_status("New file: %s", E.filename);
    else              editor_set_status("Failed to load %s.", E.filename);
    return r;
}

/* Returns 1 to quit the main loop, 0 to continue. */
static int editor_process_key(void) {
    int c = read_key_blocking();

    switch (c) {
    case '\r':
    case '\n':
        editor_insert_newline();
        return 0;

    case CTRL('q'): {
        if (E.dirty) {
            int yn = editor_prompt_ync("Save changes before quit?");
            if (yn == -1) {
                editor_set_status("Quit cancelled.");
                return 0;
            }
            if (yn == 1) {
                if (editor_do_save() != 0) {
                    /* Save was cancelled or failed — abort the quit so the
                       user doesn't lose their work. */
                    return 0;
                }
            }
        }
        return 1;
    }

    case CTRL('s'):
        editor_do_save();
        return 0;

    case CTRL('o'):
        editor_handle_open();
        return 0;

    case KEY_BACKSPACE:
    case 0x08:
        editor_del_char();
        return 0;

    case KEY_DEL:
        /* Forward-delete = move right, then backspace */
        editor_move_cursor(KEY_RIGHT);
        editor_del_char();
        return 0;

    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_UP:
    case KEY_DOWN:
    case KEY_HOME:
    case KEY_END:
    case KEY_PGUP:
    case KEY_PGDN:
        editor_move_cursor(c);
        return 0;

    case 0x1B:
        /* lone ESC — no-op (user maybe pressed it expecting nano-style menu) */
        editor_set_status("ESC has no action — ^Q to quit.");
        return 0;

    default:
        if (c == '\t' || (c >= 32 && c < 127)) {
            editor_insert_char(c);
        }
        /* Anything else (other control codes) silently ignored. */
        return 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  ENTRY POINT
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_kbltext(int argc, char *argv[]) {
    if (!kyblFS_is_mounted()) {
        printf(KYBL_ANSI_RED
               "kbltext: SD card not mounted — run 'mount' first.\r\n"
               KYBL_ANSI_RESET);
        return KYBL_ERR_INVALID_ARG;
    }

    memset(&E, 0, sizeof(E));
    editor_get_window_size();
    if (E.screen_rows < 4)  E.screen_rows = KBLT_DEFAULT_ROWS;
    if (E.screen_cols < 20) E.screen_cols = KBLT_DEFAULT_COLS;

    int load_err = 0;
    if (argc >= 2) {
        strncpy(E.filename, argv[1], sizeof(E.filename) - 1);
        E.filename[sizeof(E.filename) - 1] = '\0';
        load_err = editor_load_file(E.filename);
        if (load_err == -2) {
            printf(KYBL_ANSI_RED "kbltext: %s is too large (>%d KB)\r\n"
                   KYBL_ANSI_RESET,
                   E.filename, KBLT_MAX_FILE_BYTES / 1024);
            editor_clear_buffer();
            return KYBL_ERR_INVALID_ARG;
        }
        if (load_err == -3) {
            printf(KYBL_ANSI_RED "kbltext: %s is a directory\r\n"
                   KYBL_ANSI_RESET, E.filename);
            editor_clear_buffer();
            return KYBL_ERR_INVALID_ARG;
        }
        /* load_err == -1 (I/O / not-found) or 0 — both fine; -1 starts
           a blank named buffer thanks to the fallback in editor_load_file. */
    } else {
        row_insert_at(0, "", 0);
    }

    /* Initial status message */
    if (argc >= 2 && load_err == 0) {
        editor_set_status("Opened %s (%d line%s)",
                          E.filename, E.n_rows,
                          E.n_rows == 1 ? "" : "s");
    } else if (argc >= 2 && load_err == 1) {
        editor_set_status("New file: %s", E.filename);
    } else if (argc >= 2) {
        /* Shouldn't happen — too-large/dir cases were handled above. */
        editor_set_status("%s opened with errors", E.filename);
    } else {
        editor_set_status("%s — ^S save, ^Q quit", KBLT_VERSION);
    }

    /* Clear screen + home cursor. We don't use the alternate-screen buffer
       (\033[?1049h) because it's not enabled by default in `screen`, which
       leaves all our '~' tildes on the user's shell screen at exit. A plain
       clear is universally supported. */
    printf("\033[2J\033[H");
    fflush(stdout);

    int quit = 0;
    while (!quit) {
        editor_refresh_screen();
        quit = editor_process_key();
    }

    /* Show cursor + wipe everything we drew, return to a clean prompt. */
    printf("\033[?25h\033[2J\033[H");
    fflush(stdout);

    editor_clear_buffer();

    printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN "  kyblText" KYBL_ANSI_RESET
           " — exited.\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_kbltext = {
    .name        = "kbltext",
    .usage       = "[file]",
    .description = "kyblText — minimal text editor (^S save, ^Q quit, ^O open)",
    .entry       = run_kbltext,
};

#include "shell.h"
#include "kernel.h"
#include "kyblrtos.h"

#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ── ANSI escape sequences we react to ───────────────────────────────────── */
#define ESC         0x1B
#define DEL_KEY     0x7F
#define BACKSPACE   0x08
#define CTRL_C      0x03
#define CTRL_L      0x0C    /* clear screen shortcut */

/* ── History ring buffer ─────────────────────────────────────────────────── */
static char s_history[KYBL_SHELL_HISTORY_LEN][KYBL_SHELL_LINE_MAX];
static int  s_hist_write = 0;   /* next slot to write                 */
static int  s_hist_count = 0;   /* how many valid entries             */

static void history_push(const char *line) {
    if (line[0] == '\0') return;
    strncpy(s_history[s_hist_write], line, KYBL_SHELL_LINE_MAX - 1);
    s_hist_write = (s_hist_write + 1) % KYBL_SHELL_HISTORY_LEN;
    if (s_hist_count < KYBL_SHELL_HISTORY_LEN) s_hist_count++;
}

/* Returns entry at `back` steps from newest (0 = newest), or NULL */
static const char *history_get(int back) {
    if (back < 0 || back >= s_hist_count) return NULL;
    int idx = ((s_hist_write - 1 - back) + KYBL_SHELL_HISTORY_LEN * 2)
              % KYBL_SHELL_HISTORY_LEN;
    return s_history[idx];
}

/* ── Line editor state ────────────────────────────────────────────────────── */
typedef struct {
    char buf[KYBL_SHELL_LINE_MAX];
    int  len;           /* current character count                    */
    int  cur;           /* cursor position (0 = leftmost)             */
    int  hist_pos;      /* -1 = live line; >= 0 = history index       */
    char saved[KYBL_SHELL_LINE_MAX]; /* backup of live line during history browse */
} line_ed_t;

static void le_init(line_ed_t *le) {
    memset(le, 0, sizeof(*le));
    le->hist_pos = -1;
}

/* Redraw the whole line after in-place edits */
static void le_refresh(const line_ed_t *le) {
    /* CR, clear to EOL, re-print buffer, position cursor */
    printf("\r" KYBL_ANSI_CYAN "kybl>" KYBL_ANSI_RESET " %s\033[K", le->buf);
    /* Move cursor left if not at end */
    int back = le->len - le->cur;
    if (back > 0) printf("\033[%dD", back);
}

static void le_insert(line_ed_t *le, char c) {
    if (le->len >= KYBL_SHELL_LINE_MAX - 1) return;
    memmove(&le->buf[le->cur + 1], &le->buf[le->cur], le->len - le->cur);
    le->buf[le->cur++] = c;
    le->buf[++le->len] = '\0';
}

static void le_delete_back(line_ed_t *le) {
    if (le->cur == 0) return;
    memmove(&le->buf[le->cur - 1], &le->buf[le->cur], le->len - le->cur);
    le->buf[--le->len] = '\0';
    le->cur--;
}

static void le_load(line_ed_t *le, const char *s) {
    strncpy(le->buf, s, KYBL_SHELL_LINE_MAX - 1);
    le->buf[KYBL_SHELL_LINE_MAX - 1] = '\0';
    le->len = (int)strlen(le->buf);
    le->cur = le->len;
}

/* ── Tokeniser ────────────────────────────────────────────────────────────── */
static int tokenise(char *line, char *argv[], int max_argc) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        bool quoted = (*p == '"');
        if (quoted) p++;

        argv[argc++] = p;
        if (argc >= max_argc) break;

        if (quoted) {
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

/* ── Banner ───────────────────────────────────────────────────────────────── */
static void print_banner(void) {
    printf("\033[2J\033[H");   /* clear screen */
    printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN
           "\r\n"
           "  ██╗  ██╗██╗   ██╗██████╗ ██╗     ██████╗ ████████╗ ██████╗ ███████╗\r\n"
           "  ██║ ██╔╝╚██╗ ██╔╝██╔══██╗██║     ██╔══██╗╚══██╔══╝██╔═══██╗██╔════╝\r\n"
           "  █████╔╝  ╚████╔╝ ██████╔╝██║     ██████╔╝   ██║   ██║   ██║███████╗\r\n"
           "  ██╔═██╗   ╚██╔╝  ██╔══██╗██║     ██╔══██╗   ██║   ██║   ██║╚════██║\r\n"
           "  ██║  ██╗   ██║   ██████╔╝███████╗██║  ██║   ██║   ╚██████╔╝███████║\r\n"
           "  ╚═╝  ╚═╝   ╚═╝   ╚═════╝ ╚══════╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚══════╝\r\n"
           KYBL_ANSI_RESET);
    printf(KYBL_ANSI_CYAN "  kyblRTOS v" KYBL_VERSION_STR
           "  |  RP2040 + FreeRTOS  |  type 'help'\r\n" KYBL_ANSI_RESET);
    printf("  ─────────────────────────────────────────────────────────────\r\n\r\n");
}

/* ── Shell task ───────────────────────────────────────────────────────────── */
void shell_task(void *params) {
    (void)params;

    /* Wait for USB host to enumerate */
    while (!stdio_usb_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    print_banner();

    line_ed_t le;
    le_init(&le);
    printf(KYBL_ANSI_CYAN "kybl>" KYBL_ANSI_RESET " ");
    fflush(stdout);

    /* ANSI escape parser state */
    int esc_state = 0;  /* 0=normal, 1=got ESC, 2=got ESC[ */
    char esc_buf[4];
    int  esc_idx = 0;

    for (;;) {
        int c = getchar_timeout_us(10000);   /* 10 ms poll — friendly to RTOS */
        if (c == PICO_ERROR_TIMEOUT) {
            taskYIELD();
            continue;
        }

        /* ── ANSI escape sequence handling ─────────────────────────────── */
        if (esc_state == 1) {
            if (c == '[') { esc_state = 2; esc_idx = 0; continue; }
            esc_state = 0;
        }
        if (esc_state == 2) {
            esc_buf[esc_idx++] = (char)c;
            if (isalpha(c) || esc_idx >= 3) {
                esc_buf[esc_idx] = '\0';
                esc_state = 0;

                if (c == 'A') {
                    /* Up arrow — older history */
                    int next = le.hist_pos + 1;
                    const char *entry = history_get(next);
                    if (entry) {
                        if (le.hist_pos < 0)
                            strncpy(le.saved, le.buf, KYBL_SHELL_LINE_MAX);
                        le.hist_pos = next;
                        le_load(&le, entry);
                        le_refresh(&le);
                    }
                } else if (c == 'B') {
                    /* Down arrow — newer history / back to live */
                    if (le.hist_pos > 0) {
                        le.hist_pos--;
                        le_load(&le, history_get(le.hist_pos));
                        le_refresh(&le);
                    } else if (le.hist_pos == 0) {
                        le.hist_pos = -1;
                        le_load(&le, le.saved);
                        le_refresh(&le);
                    }
                } else if (c == 'C') {
                    /* Right arrow */
                    if (le.cur < le.len) {
                        le.cur++;
                        printf("\033[C");
                    }
                } else if (c == 'D') {
                    /* Left arrow */
                    if (le.cur > 0) {
                        le.cur--;
                        printf("\033[D");
                    }
                }
                fflush(stdout);
            }
            continue;
        }
        if (c == ESC) { esc_state = 1; continue; }

        /* ── Control characters ─────────────────────────────────────────── */
        if (c == CTRL_C) {
            printf("^C\r\n");
            le_init(&le);
            printf(KYBL_ANSI_CYAN "kybl>" KYBL_ANSI_RESET " ");
            fflush(stdout);
            continue;
        }
        if (c == CTRL_L) {
            printf("\033[2J\033[H");
            printf(KYBL_ANSI_CYAN "kybl>" KYBL_ANSI_RESET " %s", le.buf);
            if (le.len - le.cur > 0) printf("\033[%dD", le.len - le.cur);
            fflush(stdout);
            continue;
        }

        /* ── Backspace / Delete ──────────────────────────────────────────── */
        if (c == BACKSPACE || c == DEL_KEY) {
            if (le.cur > 0) {
                le_delete_back(&le);
                le_refresh(&le);
                fflush(stdout);
            }
            continue;
        }

        /* ── Enter / Return ─────────────────────────────────────────────── */
        if (c == '\r' || c == '\n') {
            printf("\r\n");

            /* Trim leading/trailing whitespace */
            char *trimmed = le.buf;
            while (isspace((unsigned char)*trimmed)) trimmed++;
            int tlen = (int)strlen(trimmed);
            while (tlen > 0 && isspace((unsigned char)trimmed[tlen - 1]))
                trimmed[--tlen] = '\0';

            if (tlen > 0) {
                history_push(trimmed);

                /* Tokenise and dispatch */
                char *argv[KYBL_SHELL_ARGC_MAX];
                int argc = tokenise(trimmed, argv, KYBL_SHELL_ARGC_MAX);

                if (argc > 0) {
                    int rc = kernel_exec(argv[0], argc, argv);
                    if (rc == KYBL_ERR_NOT_FOUND) {
                        printf(KYBL_ANSI_RED "kyblRTOS:" KYBL_ANSI_RESET
                               " command not found: %s"
                               "  (type 'help' for list)\r\n", argv[0]);
                    }
                    fflush(stdout);
                }
            }

            le_init(&le);
            printf(KYBL_ANSI_CYAN "kybl>" KYBL_ANSI_RESET " ");
            fflush(stdout);
            continue;
        }

        /* ── Printable character ─────────────────────────────────────────── */
        if (isprint(c)) {
            /* Exit history browse on any edit */
            if (le.hist_pos >= 0) {
                le.hist_pos = -1;
            }
            le_insert(&le, (char)c);
            le_refresh(&le);
            fflush(stdout);
        }
    }
}

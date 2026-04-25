#ifndef KYBLRTOS_H
#define KYBLRTOS_H

#include <stdint.h>
#include <stddef.h>

/* ── Version ──────────────────────────────────────────────────────────────── */
#define KYBL_VERSION_MAJOR  0
#define KYBL_VERSION_MINOR  1
#define KYBL_VERSION_PATCH  0
#define KYBL_VERSION_STR    "0.1.0"
#define KYBL_BUILD_DATE     __DATE__ " " __TIME__

/* ── LED GPIO pins (binary display, LSB → MSB) ────────────────────────────── */
/*  Wire your 4 LEDs (with resistors) to GP16..GP19                           */
#define KYBL_LED_PIN_BIT0   16
#define KYBL_LED_PIN_BIT1   17
#define KYBL_LED_PIN_BIT2   18
#define KYBL_LED_PIN_BIT3   19

/* ── Shell limits ─────────────────────────────────────────────────────────── */
#define KYBL_SHELL_LINE_MAX     128     /* max command line length             */
#define KYBL_SHELL_ARGC_MAX     16      /* max number of tokens per command    */
#define KYBL_SHELL_HISTORY_LEN  8       /* number of stored history entries    */

/* ── Program registry ─────────────────────────────────────────────────────── */
#define KYBL_MAX_PROGRAMS       64

/* ── Return codes ─────────────────────────────────────────────────────────── */
#define KYBL_OK                 0
#define KYBL_ERR_NOT_FOUND     -1
#define KYBL_ERR_INVALID_ARG   -2
#define KYBL_ERR_FULL          -3

/* ── Colour escape helpers (ANSI, works in most serial terminals) ──────────── */
#define KYBL_ANSI_RESET         "\033[0m"
#define KYBL_ANSI_BOLD          "\033[1m"
#define KYBL_ANSI_RED           "\033[31m"
#define KYBL_ANSI_GREEN         "\033[32m"
#define KYBL_ANSI_YELLOW        "\033[33m"
#define KYBL_ANSI_CYAN          "\033[36m"
#define KYBL_ANSI_MAGENTA       "\033[35m"

/* ── Program descriptor ───────────────────────────────────────────────────── */
/**
 * Every built-in command AND every future SD-card program is described
 * by this struct.  SD-card programs will be loaded into RAM and their
 * entry point stored in the function pointer at runtime.
 */
typedef struct {
    const char *name;                           /* command / program name       */
    const char *usage;                          /* short usage string           */
    const char *description;                    /* one-line description         */
    int (*entry)(int argc, char *argv[]);       /* entry point                  */
} kybl_program_t;

#endif /* KYBLRTOS_H */

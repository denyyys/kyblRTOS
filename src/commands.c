#include "commands.h"
#include "kernel.h"
#include "led_display.h"
#include "kyblrtos.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

/* Defined in main.c — incremented by vApplicationMallocFailedHook each time
   a pvPortMalloc() call returns NULL. Surfaced by the 'mem' and 'fragcheck'
   commands so the user can see when the heap ran out without having to wait
   for the LED flash. */
extern volatile uint32_t g_malloc_fail_count;

/* ════════════════════════════════════════════════════════════════════════════
 *  HELP  –  list all registered commands
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_help(int argc, char *argv[]) {

    /* help tasks — explain the task table */
    if (argc >= 2 && strcmp(argv[1], "tasks") == 0) {
        printf(KYBL_ANSI_BOLD "\r\n  'tasks' column reference\r\n" KYBL_ANSI_RESET);
        printf("  ────────────────────────────────────────────────────────\r\n");
        printf("  " KYBL_ANSI_CYAN "Name " KYBL_ANSI_RESET
               "    Task name (set at creation, max 16 chars)\r\n");
        printf("  " KYBL_ANSI_CYAN "State" KYBL_ANSI_RESET
               "    " KYBL_ANSI_GREEN "X" KYBL_ANSI_RESET "=Running  "
                       KYBL_ANSI_GREEN "R" KYBL_ANSI_RESET "=Ready  "
                       KYBL_ANSI_YELLOW "B" KYBL_ANSI_RESET "=Blocked  "
                       KYBL_ANSI_YELLOW "S" KYBL_ANSI_RESET "=Suspended  "
                       KYBL_ANSI_RED    "D" KYBL_ANSI_RESET "=Deleted\r\n");
        printf("  " KYBL_ANSI_CYAN "Prio " KYBL_ANSI_RESET
               "    Current priority (higher = more important)\r\n");
        printf("  " KYBL_ANSI_CYAN "Stack" KYBL_ANSI_RESET
               "    High-water mark: " KYBL_ANSI_YELLOW "words"
               KYBL_ANSI_RESET " remaining on stack.\r\n"
               "             Low values = stack nearly full = danger!\r\n"
               "             Run 'stackcheck' for a detailed view.\r\n");
        printf("  " KYBL_ANSI_CYAN "Num  " KYBL_ANSI_RESET
               "    Unique task number assigned by FreeRTOS\r\n");
        printf("  ────────────────────────────────────────────────────────\r\n\r\n");
        return KYBL_OK;
    }

    printf(KYBL_ANSI_BOLD KYBL_ANSI_CYAN
           "\r\n  kyblRTOS v" KYBL_VERSION_STR " — built-in commands\r\n"
           KYBL_ANSI_RESET);
    printf("  ─────────────────────────────────────────────────\r\n");

    int idx = 0;
    const kybl_program_t *p;
    while ((p = kernel_next(&idx)) != NULL) {
        printf("  " KYBL_ANSI_GREEN "%-12s" KYBL_ANSI_RESET
               " %-22s  %s\r\n",
               p->name,
               p->usage ? p->usage : "",
               p->description ? p->description : "");
    }
    printf("  ─────────────────────────────────────────────────\r\n");
    printf("  Tip: 'help tasks' explains the task table columns.\r\n\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_help = {
    .name        = "help",
    .usage       = "",
    .description = "List all commands",
    .entry       = run_help,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  VER  –  version + memory info
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_ver(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Unique board ID */
    char uid[17] = {0};
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
        snprintf(uid + i * 2, 3, "%02X", board_id.id[i]);

    size_t heap_free  = xPortGetFreeHeapSize();
    size_t heap_min   = xPortGetMinimumEverFreeHeapSize();
    size_t heap_total = configTOTAL_HEAP_SIZE;
    size_t heap_used  = heap_total - heap_free;

    /* Uptime from FreeRTOS tick counter */
    TickType_t ticks   = xTaskGetTickCount();
    uint32_t   total_s = ticks / configTICK_RATE_HZ;
    uint32_t   days    = total_s / 86400;
    uint32_t   hours   = (total_s % 86400) / 3600;
    uint32_t   mins    = (total_s % 3600)  / 60;
    uint32_t   secs    = total_s % 60;

    printf(KYBL_ANSI_BOLD "\r\n  kyblRTOS\r\n" KYBL_ANSI_RESET);
    printf("  Version   : " KYBL_ANSI_GREEN KYBL_VERSION_STR KYBL_ANSI_RESET "\r\n");
    printf("  Built     : %s\r\n", KYBL_BUILD_DATE);
    printf("  Board ID  : %s\r\n", uid);
    printf("  FreeRTOS  : v" tskKERNEL_VERSION_NUMBER "\r\n");
    printf("  CPU clock : " KYBL_ANSI_CYAN "%lu MHz" KYBL_ANSI_RESET "\r\n",
           (unsigned long)clock_get_hz(clk_sys) / 1000000UL);
    printf("  Cores     : 2 total, " KYBL_ANSI_GREEN "1 utilized"
           KYBL_ANSI_RESET " (FreeRTOS single-core, core 0)\r\n");
    printf("  Uptime    : " KYBL_ANSI_GREEN "%ud %02uh %02um %02us" KYBL_ANSI_RESET "\r\n",
           days, hours, mins, secs);
    printf("  Heap total: %u bytes\r\n",  (unsigned)heap_total);
    printf("  Heap used : " KYBL_ANSI_YELLOW "%u bytes" KYBL_ANSI_RESET "\r\n", (unsigned)heap_used);
    printf("  Heap free : " KYBL_ANSI_GREEN  "%u bytes" KYBL_ANSI_RESET "\r\n", (unsigned)heap_free);
    printf("  Heap min  : %u bytes (lowest ever free)\r\n\r\n", (unsigned)heap_min);
    return KYBL_OK;
}

const kybl_program_t cmd_ver = {
    .name        = "ver",
    .usage       = "",
    .description = "Show version and RAM usage",
    .entry       = run_ver,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  PRINT  –  print text to the terminal
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_print(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: print <text...>\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_print = {
    .name        = "print",
    .usage       = "<text...>",
    .description = "Print text to the terminal",
    .entry       = run_print,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  BIN  –  display a number (0-15) in binary on the 4 LEDs
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_bin(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: bin <0-15>\r\n");
        printf("Displays the lower 4 bits on the LED bar (GP16-GP19).\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    long val = strtol(argv[1], NULL, 0);   /* accepts 0b… 0x… decimal */
    if (val < 0 || val > 15) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET
               " value must be 0-15 (4 bits).\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    uint8_t nibble = (uint8_t)(val & 0x0F);
    led_display_set(nibble);

    /* Pretty binary string */
    char bits[5];
    for (int i = 3; i >= 0; i--)
        bits[3 - i] = ((nibble >> i) & 1) ? '1' : '0';
    bits[4] = '\0';

    printf("LED display: " KYBL_ANSI_YELLOW "%d" KYBL_ANSI_RESET
           "  binary: " KYBL_ANSI_CYAN "%s" KYBL_ANSI_RESET
           "  [GP19 GP18 GP17 GP16]\r\n",
           nibble, bits);
    return KYBL_OK;
}

const kybl_program_t cmd_bin = {
    .name        = "bin",
    .usage       = "<0-15>",
    .description = "Show number in binary on 4 LEDs (GP16-GP19)",
    .entry       = run_bin,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  CLEAR  –  clear the terminal screen
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("\033[2J\033[H");   /* erase screen + cursor home */
    return KYBL_OK;
}

const kybl_program_t cmd_clear = {
    .name        = "clear",
    .usage       = "",
    .description = "Clear the terminal screen",
    .entry       = run_clear,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  TASKS  –  list FreeRTOS tasks
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_tasks(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* vTaskList needs configUSE_TRACE_FACILITY and
       configUSE_STATS_FORMATTING_FUNCTIONS set to 1 */
    static char buf[512];
    vTaskList(buf);
    printf(KYBL_ANSI_BOLD
           "\r\n  Name            State  Prio  Stack   Num  Core\r\n"
           KYBL_ANSI_RESET);
    printf("  ─────────────────────────────────────────────────\r\n");
    printf("%s\r\n", buf);
    return KYBL_OK;
}

const kybl_program_t cmd_tasks = {
    .name        = "tasks",
    .usage       = "",
    .description = "List active FreeRTOS tasks",
    .entry       = run_tasks,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  MEM  –  full RP2040 memory map (flash + SRAM0-5 + FreeRTOS heap)
 *
 *  Pulls real numbers from:
 *    - GCC linker symbols (__flash_binary_start/_end, __data_*, __bss_*,
 *      __end__, __HeapLimit, __StackLimit, __StackTop, __StackBottom,
 *      __StackOneTop, __StackOneBottom, __scratch_x/y_start/end).
 *    - FreeRTOS heap_4 stats (vPortGetHeapStats).
 *  No estimates — every byte is accounted for by what the linker actually
 *  placed in memory.
 * ════════════════════════════════════════════════════════════════════════════ */

/* Linker-provided symbols. Declared as char[] so &sym gives the address. */
extern char __flash_binary_start[], __flash_binary_end[];
extern char __data_start__[],       __data_end__[];
extern char __bss_start__[],        __bss_end__[];
extern char __end__[],              __HeapLimit[];
extern char __StackLimit[],         __StackTop[], __StackBottom[];
extern char __StackOneTop[],        __StackOneBottom[];
extern char __scratch_x_start__[],  __scratch_x_end__[];
extern char __scratch_y_start__[],  __scratch_y_end__[];

static void mem_print_region(const char *name, uintptr_t base, size_t size,
                              size_t used, const char *extra) {
    unsigned pct = size ? (unsigned)((used * 100) / size) : 0;
    printf("  %-18s 0x%08lX  %6u / %6u B  %3u%%  %s\r\n",
           name, (unsigned long)base,
           (unsigned)used, (unsigned)size, pct, extra ? extra : "");
}

static int run_mem(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* ── Flash binary footprint ───────────────────────────────────────────── */
    uintptr_t flash_start = (uintptr_t)__flash_binary_start;
    uintptr_t flash_end   = (uintptr_t)__flash_binary_end;
    size_t    flash_used  = flash_end - flash_start;
    size_t    flash_total = 2 * 1024 * 1024;   /* Pico W has 2 MB QSPI */

    /* ── SRAM striped (SRAM0..3) = 256 KB ─────────────────────────────────── */
    uintptr_t sram_base = 0x20000000;
    size_t    sram_size = 256 * 1024;

    size_t data_size  = (size_t)(__data_end__ - __data_start__);
    size_t bss_size   = (size_t)(__bss_end__  - __bss_start__);
    /* Region from __end__ (after .bss) up to __HeapLimit = free system RAM
       (this is where newlib sbrk would grow; we don't use it, heap_4 lives
        inside .bss as ucHeap[configTOTAL_HEAP_SIZE]). */
    size_t sys_free   = (size_t)(__HeapLimit - __end__);
    size_t sram_used  = (size_t)(__end__ - (char *)sram_base);

    /* ── Scratch banks (SRAM4/5) = 4 KB each ──────────────────────────────── */
    size_t sx_size  = 4 * 1024;
    size_t sy_size  = 4 * 1024;
    size_t sx_used  = (size_t)(__scratch_x_end__ - __scratch_x_start__);
    size_t sy_used  = (size_t)(__scratch_y_end__ - __scratch_y_start__);
    size_t stack0   = (size_t)(__StackTop        - __StackBottom);    /* core0 */
    size_t stack1   = (size_t)(__StackOneTop     - __StackOneBottom); /* core1 */

    /* ── FreeRTOS heap_4 stats ────────────────────────────────────────────── */
    HeapStats_t hs;
    vPortGetHeapStats(&hs);
    size_t heap_total   = configTOTAL_HEAP_SIZE;
    size_t heap_free    = hs.xAvailableHeapSpaceInBytes;
    size_t heap_used    = heap_total - heap_free;
    size_t heap_min     = hs.xMinimumEverFreeBytesRemaining;
    size_t heap_largest = hs.xSizeOfLargestFreeBlockInBytes;
    size_t heap_blocks  = hs.xNumberOfFreeBlocks;

    /* ── Tasks ────────────────────────────────────────────────────────────── */
    UBaseType_t n_tasks = uxTaskGetNumberOfTasks();

    /* ── Report ───────────────────────────────────────────────────────────── */
    printf(KYBL_ANSI_BOLD "\r\n  RP2040 Memory Map\r\n" KYBL_ANSI_RESET);
    printf("  ────────────────────────────────────────────────────────────────────\r\n");
    printf("  Region             Base        Used / Size        %%    Notes\r\n");
    printf("  ────────────────────────────────────────────────────────────────────\r\n");

    char note[64];
    snprintf(note, sizeof(note), "binary: %u B", (unsigned)flash_used);
    mem_print_region("FLASH (QSPI)",  flash_start, flash_total, flash_used, note);

    snprintf(note, sizeof(note), ".data+.bss+heap+stacks");
    mem_print_region("SRAM 0-3 (main)", sram_base, sram_size, sram_used, note);

    snprintf(note, sizeof(note), "RAM-resident code+vars");
    mem_print_region("  .data",         (uintptr_t)__data_start__, data_size, data_size, note);

    snprintf(note, sizeof(note), "zero-init (incl. RTOS heap pool)");
    mem_print_region("  .bss",          (uintptr_t)__bss_start__,  bss_size,  bss_size,  note);

    snprintf(note, sizeof(note), "sbrk-range (not used by heap_4)");
    mem_print_region("  sys-free",      (uintptr_t)__end__,        sys_free,  0,         note);

    snprintf(note, sizeof(note), "core-1 stack = %u B", (unsigned)stack1);
    mem_print_region("SRAM 4 (scratch_x)", (uintptr_t)0x20040000, sx_size, sx_used, note);

    snprintf(note, sizeof(note), "core-0 stack = %u B", (unsigned)stack0);
    mem_print_region("SRAM 5 (scratch_y)", (uintptr_t)0x20041000, sy_size, sy_used, note);

    printf("  ────────────────────────────────────────────────────────────────────\r\n");
    printf(KYBL_ANSI_BOLD "  FreeRTOS heap_4 (pool inside .bss)\r\n" KYBL_ANSI_RESET);
    printf("  ────────────────────────────────────────────────────────────────────\r\n");
    unsigned heap_pct = (unsigned)((heap_used * 100) / heap_total);
    printf("  Total pool       : %u B\r\n",                          (unsigned)heap_total);
    printf("  Used             : " KYBL_ANSI_YELLOW "%u B" KYBL_ANSI_RESET " (%u%%)\r\n",
                                                                      (unsigned)heap_used, heap_pct);
    printf("  Free             : " KYBL_ANSI_GREEN  "%u B" KYBL_ANSI_RESET "\r\n",
                                                                      (unsigned)heap_free);
    printf("  Largest block    : %u B\r\n",                           (unsigned)heap_largest);
    printf("  Free blocks      : %u\r\n",                             (unsigned)heap_blocks);
    printf("  Min-ever-free    : %u B (lowest high-water)\r\n",       (unsigned)heap_min);
    printf("  Malloc-fails     : %lu\r\n",                            (unsigned long)g_malloc_fail_count);

    printf("  ────────────────────────────────────────────────────────────────────\r\n");
    printf("  Active tasks     : %u\r\n", (unsigned)n_tasks);
    printf("  Flash binary     : %u B / %u B  (%u%% of 2 MB)\r\n",
           (unsigned)flash_used, (unsigned)flash_total,
           (unsigned)((flash_used * 100) / flash_total));
    printf("  SRAM total       : %u B (264 KB = 256 main + 2x 4 scratch)\r\n",
           (unsigned)(sram_size + sx_size + sy_size));
    printf("  ────────────────────────────────────────────────────────────────────\r\n\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_mem = {
    .name        = "mem",
    .usage       = "",
    .description = "Show heap memory usage",
    .entry       = run_mem,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  ECHO  –  echo arguments back (useful for scripts)
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_echo = {
    .name        = "echo",
    .usage       = "<text...>",
    .description = "Echo arguments to stdout",
    .entry       = run_echo,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  LED  –  direct LED control: led on|off|<0-15>
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_led(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: led <on|off|0-15>\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    if (strcmp(argv[1], "on") == 0) {
        led_display_set(0x0F);
        printf("All LEDs ON\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        led_display_clear();
        printf("All LEDs OFF\r\n");
    } else {
        long val = strtol(argv[1], NULL, 0);
        if (val < 0 || val > 15) {
            printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " value must be 0-15.\r\n");
            return KYBL_ERR_INVALID_ARG;
        }
        led_display_set((uint8_t)val);
        printf("LEDs set to %ld\r\n", val);
    }
    return KYBL_OK;
}

const kybl_program_t cmd_led = {
    .name        = "led",
    .usage       = "<on|off|0-15>",
    .description = "Direct LED bar control",
    .entry       = run_led,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  REBOOT  –  software reset via watchdog
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Rebooting...\r\n");
    /* Small delay so the message actually goes out */
    vTaskDelay(pdMS_TO_TICKS(100));
    watchdog_reboot(0, 0, 0);
    for (;;) {}   /* unreachable */
    return KYBL_OK;
}

const kybl_program_t cmd_reboot = {
    .name        = "reboot",
    .usage       = "",
    .description = "Soft-reboot the device",
    .entry       = run_reboot,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  RAND  –  random number via RP2040 ring oscillator
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_rand(int argc, char *argv[]) {
    /* Xorshift32 seeded from hardware timer — no pico_rand needed.
       time_us_64() reads the RP2040's always-running 64-bit timer,
       giving a different seed every call even at microsecond intervals. */
    static uint32_t state = 0;
    if (state == 0) {
        uint64_t t = time_us_64();
        state = (uint32_t)(t ^ (t >> 32));
        if (state == 0) state = 0xDEADBEEF;
    }
    /* Mix in current tick for extra entropy on repeated calls */
    state ^= (uint32_t)xTaskGetTickCount();
    /* Xorshift32 */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    uint32_t r = state;

    if (argc >= 2) {
        long max = strtol(argv[1], NULL, 0);
        if (max < 1) {
            printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " max must be >= 1\r\n");
            return KYBL_ERR_INVALID_ARG;
        }
        r = r % (uint32_t)max;
    }

    /* Also show on LEDs (lower nibble) */
    led_display_set((uint8_t)(r & 0x0F));

    printf(KYBL_ANSI_CYAN "%lu" KYBL_ANSI_RESET "\r\n", (unsigned long)r);
    return KYBL_OK;
}

const kybl_program_t cmd_rand = {
    .name        = "rand",
    .usage       = "[max]",
    .description = "Random number from hardware RNG (shown on LEDs too)",
    .entry       = run_rand,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  CALC  –  integer expression evaluator
 *
 *  Supports: + - * / %  with correct precedence, parentheses, unary minus.
 *  Grammar (recursive descent):
 *    expr   = term   { ('+' | '-') term   }
 *    term   = factor { ('*' | '/' | '%') factor }
 *    factor = ['-'] ( number | '(' expr ')' )
 * ════════════════════════════════════════════════════════════════════════════ */
typedef struct { const char *p; bool err; } calc_ctx_t;

static void      calc_skip_ws(calc_ctx_t *c) { while (*c->p == ' ') c->p++; }
static long long calc_expr(calc_ctx_t *c);

static long long calc_factor(calc_ctx_t *c) {
    calc_skip_ws(c);

    /* Unary minus */
    if (*c->p == '-') { c->p++; return -calc_factor(c); }
    /* Unary plus */
    if (*c->p == '+') { c->p++; return  calc_factor(c); }

    /* Parenthesised sub-expression */
    if (*c->p == '(') {
        c->p++;
        long long v = calc_expr(c);
        calc_skip_ws(c);
        if (*c->p == ')') c->p++;
        else c->err = true;
        return v;
    }

    /* Integer literal (decimal / 0x hex / 0b binary) */
    if (isdigit((unsigned char)*c->p)) {
        char *end;
        long long v;
        if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X'))
            v = strtoll(c->p, &end, 16);
        else if (c->p[0] == '0' && (c->p[1] == 'b' || c->p[1] == 'B'))
            v = strtoll(c->p + 2, &end, 2);
        else
            v = strtoll(c->p, &end, 10);
        c->p = end;
        return v;
    }

    c->err = true;
    return 0;
}

static long long calc_term(calc_ctx_t *c) {
    long long v = calc_factor(c);
    for (;;) {
        calc_skip_ws(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%') break;
        c->p++;
        long long r = calc_factor(c);
        if (op == '*') v *= r;
        else if (r == 0) { printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " division by zero\r\n"); c->err = true; return 0; }
        else if (op == '/') v /= r;
        else                v %= r;
    }
    return v;
}

static long long calc_expr(calc_ctx_t *c) {
    long long v = calc_term(c);
    for (;;) {
        calc_skip_ws(c);
        char op = *c->p;
        if (op != '+' && op != '-') break;
        c->p++;
        long long r = calc_term(c);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

static int run_calc(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: calc <expression>\r\n");
        printf("  Supports: + - * / %% ()  unary minus  0x hex  0b binary\r\n");
        printf("  Example:  calc 3+4*2              -> 11\r\n");
        printf("  Example:  calc (3+4)*2            -> 14\r\n");
        printf("  Example:  calc 5111245545*548      -> 2800962638660\r\n");
        printf("  Example:  calc 0xFF-0b1010         -> 245\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    /* Re-join tokens into one string (shell split on spaces) */
    static char expr[KYBL_SHELL_LINE_MAX];
    expr[0] = '\0';
    for (int i = 1; i < argc; i++) {
        strncat(expr, argv[i], sizeof(expr) - strlen(expr) - 1);
    }

    calc_ctx_t ctx = { .p = expr, .err = false };
    long long result = calc_expr(&ctx);
    calc_skip_ws(&ctx);

    if (ctx.err || *ctx.p != '\0') {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET
               " invalid expression near '%s'\r\n", ctx.p);
        return KYBL_ERR_INVALID_ARG;
    }

    /* Binary string — show 64 bits, trim leading zeros (keep min 4) */
    char bin_str[65] = {0};
    for (int i = 63; i >= 0; i--)
        bin_str[63 - i] = ((result >> i) & 1) ? '1' : '0';
    const char *bin_trim = bin_str;
    while (*bin_trim == '0' && *(bin_trim + 4)) bin_trim++;

    /* Lower nibble on LEDs */
    led_display_set((uint8_t)(result & 0x0F));

    printf(KYBL_ANSI_CYAN "= %lld" KYBL_ANSI_RESET
           "  (0x%llX  0b%s)\r\n",
           result, (unsigned long long)result, bin_trim);
    return KYBL_OK;
}

const kybl_program_t cmd_calc = {
    .name        = "calc",
    .usage       = "<expr>",
    .description = "Integer calculator: + - * / % () 0x 0b supported",
    .entry       = run_calc,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  UPTIME  –  standalone uptime command
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_uptime(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TickType_t ticks   = xTaskGetTickCount();
    uint32_t   total_s = ticks / configTICK_RATE_HZ;
    uint32_t   days    = total_s / 86400;
    uint32_t   hours   = (total_s % 86400) / 3600;
    uint32_t   mins    = (total_s % 3600)  / 60;
    uint32_t   secs    = total_s % 60;
    uint32_t   ms      = (ticks % configTICK_RATE_HZ) * 1000 / configTICK_RATE_HZ;

    printf("Uptime: " KYBL_ANSI_GREEN "%ud %02uh %02um %02u.%03us"
           KYBL_ANSI_RESET "  (%lu ticks)\r\n",
           days, hours, mins, secs, ms, (unsigned long)ticks);
    return KYBL_OK;
}

const kybl_program_t cmd_uptime = {
    .name        = "uptime",
    .usage       = "",
    .description = "Show time since boot",
    .entry       = run_uptime,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  STACKCHECK  –  per-task stack high-water mark report
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_stackcheck(int argc, char *argv[]) {
    (void)argc; (void)argv;

    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *st = pvPortMalloc(n * sizeof(TaskStatus_t));
    if (!st) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " out of heap\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    UBaseType_t filled = uxTaskGetSystemState(st, n, NULL);

    printf(KYBL_ANSI_BOLD
           "\r\n  %-16s  %10s  %s\r\n" KYBL_ANSI_RESET,
           "Task", "HWM(words)", "Status");
    printf("  ─────────────────────────────────────────────\r\n");

    for (UBaseType_t i = 0; i < filled; i++) {
        UBaseType_t hwm = st[i].usStackHighWaterMark;
        const char *status;
        if (hwm < 20)       status = KYBL_ANSI_RED    "CRITICAL – overflow risk!" KYBL_ANSI_RESET;
        else if (hwm < 48)  status = KYBL_ANSI_YELLOW "LOW – consider enlarging"  KYBL_ANSI_RESET;
        else                status = KYBL_ANSI_GREEN   "OK"                        KYBL_ANSI_RESET;

        printf("  %-16s  %10u  %s\r\n",
               st[i].pcTaskName, (unsigned)hwm, status);
    }

    vPortFree(st);
    printf("\r\n  HWM = words remaining. Lower = closer to overflow.\r\n\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_stackcheck = {
    .name        = "stackcheck",
    .usage       = "",
    .description = "Per-task stack high-water mark (overflow risk detector)",
    .entry       = run_stackcheck,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  FRAGCHECK  –  heap fragmentation probe
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_fragcheck(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* heap_4 keeps precise free-block bookkeeping in its internal linked list.
       vPortGetHeapStats() reads that list directly — zero allocations, no risk
       of tripping the malloc-failed hook (the old binary-search probe did, and
       required a hard reset of the Pico). */
    HeapStats_t hs;
    vPortGetHeapStats(&hs);

    size_t total     = configTOTAL_HEAP_SIZE;
    size_t free_now  = hs.xAvailableHeapSpaceInBytes;
    size_t largest   = hs.xSizeOfLargestFreeBlockInBytes;
    size_t smallest  = hs.xSizeOfSmallestFreeBlockInBytes;
    size_t blocks    = hs.xNumberOfFreeBlocks;
    size_t min_ever  = hs.xMinimumEverFreeBytesRemaining;

    if (free_now == 0) {
        printf(KYBL_ANSI_RED "Heap empty!\r\n" KYBL_ANSI_RESET);
        return KYBL_ERR_INVALID_ARG;
    }

    size_t fragments = (free_now > largest) ? (free_now - largest) : 0;
    unsigned frag_pct = (unsigned)(fragments * 100 / free_now);
    unsigned used_pct = (unsigned)((total - free_now) * 100 / total);

    printf(KYBL_ANSI_BOLD "\r\n  Heap Fragmentation Report\r\n" KYBL_ANSI_RESET);
    printf("  ─────────────────────────────────────────\r\n");
    printf("  Heap total        : %u bytes\r\n",             (unsigned)total);
    printf("  Currently free    : %u bytes (%u%% used)\r\n", (unsigned)free_now, used_pct);
    printf("  Minimum ever free : %u bytes\r\n",             (unsigned)min_ever);
    printf("  ─────────────────────────────────────────\r\n");
    printf("  Free blocks       : %u\r\n",                    (unsigned)blocks);
    printf("  Largest block     : %u bytes\r\n",              (unsigned)largest);
    printf("  Smallest block    : %u bytes\r\n",              (unsigned)smallest);
    printf("  In fragments      : %u bytes (%u%%)\r\n",       (unsigned)fragments, frag_pct);
    printf("  Malloc-fails      : %lu\r\n",                   (unsigned long)g_malloc_fail_count);
    printf("  ─────────────────────────────────────────\r\n");

    if (frag_pct < 5 && blocks <= 2)
        printf("  Status            : " KYBL_ANSI_GREEN  "Healthy\r\n"                KYBL_ANSI_RESET);
    else if (frag_pct < 25)
        printf("  Status            : " KYBL_ANSI_YELLOW "Moderate fragmentation\r\n" KYBL_ANSI_RESET);
    else
        printf("  Status            : " KYBL_ANSI_RED    "Heavily fragmented!\r\n"    KYBL_ANSI_RESET);

    printf("\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_fragcheck = {
    .name        = "fragcheck",
    .usage       = "",
    .description = "Probe heap for fragmentation",
    .entry       = run_fragcheck,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  NOTE / NOTES  –  in-RAM note store
 * ════════════════════════════════════════════════════════════════════════════ */
#define KYBL_MAX_NOTES   16
#define KYBL_NOTE_LEN    80

static char s_notes[KYBL_MAX_NOTES][KYBL_NOTE_LEN];
static int  s_note_count = 0;

static int run_note(int argc, char *argv[]) {
    /* note clear */
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        s_note_count = 0;
        printf("All notes cleared.\r\n");
        return KYBL_OK;
    }

    /* note <text...> */
    if (argc < 2) {
        printf("Usage: note <text>   — add a note\r\n");
        printf("       note clear    — delete all notes\r\n");
        printf("       notes         — list all notes\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    if (s_note_count >= KYBL_MAX_NOTES) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET
               " note store full (%d max). Use 'note clear'.\r\n", KYBL_MAX_NOTES);
        return KYBL_ERR_FULL;
    }

    /* Join args into one string */
    char *dst = s_notes[s_note_count];
    dst[0] = '\0';
    for (int i = 1; i < argc; i++) {
        strncat(dst, argv[i], KYBL_NOTE_LEN - strlen(dst) - 2);
        if (i < argc - 1) strncat(dst, " ", KYBL_NOTE_LEN - strlen(dst) - 1);
    }

    printf("[%d] %s\r\n", s_note_count + 1, dst);
    s_note_count++;
    return KYBL_OK;
}

static int run_notes(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (s_note_count == 0) {
        printf("No notes. Use 'note <text>' to add one.\r\n");
        return KYBL_OK;
    }
    printf(KYBL_ANSI_BOLD "\r\n  Notes (%d/%d)\r\n" KYBL_ANSI_RESET,
           s_note_count, KYBL_MAX_NOTES);
    printf("  ─────────────────────────────────\r\n");
    for (int i = 0; i < s_note_count; i++)
        printf("  " KYBL_ANSI_CYAN "[%2d]" KYBL_ANSI_RESET " %s\r\n",
               i + 1, s_notes[i]);
    printf("\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_note = {
    .name        = "note",
    .usage       = "<text> | clear",
    .description = "Add a note to RAM (persists until reboot)",
    .entry       = run_note,
};

const kybl_program_t cmd_notes = {
    .name        = "notes",
    .usage       = "",
    .description = "List all stored notes",
    .entry       = run_notes,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  BOUNCE  –  background FreeRTOS task: LED bounces back and forth
 *
 *  Note: we explicitly re-init GPIO16..19 from inside the task (not via
 *  led_display_init) because cyw43_arch_init + FreeRTOS scheduler transition
 *  can leave some SIO bookkeeping in a state where the pin function defaults
 *  are not re-applied. Doing gpio_init + gpio_set_function + gpio_set_dir
 *  + gpio_put directly from this task guarantees the lines are driven.
 *  Priority is raised to the same level as the shell so it time-slices
 *  (priority 1 was being starved by the CYW43 background task at prio 4).
 * ════════════════════════════════════════════════════════════════════════════ */
static TaskHandle_t s_bounce_handle = NULL;

static inline void led_force_output(void) {
    const uint8_t pins[4] = {
        KYBL_LED_PIN_BIT0, KYBL_LED_PIN_BIT1,
        KYBL_LED_PIN_BIT2, KYBL_LED_PIN_BIT3
    };
    for (int i = 0; i < 4; i++) {
        gpio_init(pins[i]);
        gpio_set_function(pins[i], GPIO_FUNC_SIO);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }
}

static inline void led_force_set(uint8_t value) {
    const uint8_t pins[4] = {
        KYBL_LED_PIN_BIT0, KYBL_LED_PIN_BIT1,
        KYBL_LED_PIN_BIT2, KYBL_LED_PIN_BIT3
    };
    for (int i = 0; i < 4; i++) {
        gpio_put(pins[i], (value >> i) & 1u);
    }
}

static void bounce_task_fn(void *params) {
    uint32_t ms = (uint32_t)(uintptr_t)params;
    int pos = 0, dir = 1;

    /* Hammer GPIO config directly — bypasses led_display module in case its
       static state got out-of-sync with the hardware. */
    led_force_output();

    for (;;) {
        led_force_set((uint8_t)(1u << pos));
        vTaskDelay(pdMS_TO_TICKS(ms));
        pos += dir;
        if (pos >= 3) dir = -1;
        if (pos <= 0) dir =  1;
    }
}

static int run_bounce(int argc, char *argv[]) {
    /* bounce stop */
    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        if (s_bounce_handle) {
            vTaskDelete(s_bounce_handle);
            s_bounce_handle = NULL;
            led_force_set(0);
            printf("Bounce stopped.\r\n");
        } else {
            printf("Bounce is not running.\r\n");
        }
        return KYBL_OK;
    }

    /* bounce [ms] start (default 120 ms) */
    if (s_bounce_handle) {
        printf("Bounce already running. Use 'bounce stop' first.\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    uint32_t ms = 120;
    if (argc >= 2) ms = (uint32_t)strtoul(argv[1], NULL, 0);
    if (ms < 20) ms = 20;

    /* priority 2 = same as shell; 1 got starved by CYW43 background task */
    BaseType_t ok = xTaskCreate(bounce_task_fn, "Bounce", 512,
                                (void *)(uintptr_t)ms, 2, &s_bounce_handle);
    if (ok != pdPASS) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " failed to create task\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    printf("Bounce started at %lu ms/step. 'bounce stop' to halt.\r\n", (unsigned long)ms);
    return KYBL_OK;
}

const kybl_program_t cmd_bounce = {
    .name        = "bounce",
    .usage       = "[ms] | stop",
    .description = "Background LED bounce animation (FreeRTOS task)",
    .entry       = run_bounce,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  BLINKER  –  background task: blink a pattern on the LEDs
 * ════════════════════════════════════════════════════════════════════════════ */
static TaskHandle_t s_blinker_handle = NULL;

typedef struct { uint8_t pattern; uint32_t ms; } blinker_cfg_t;

static void blinker_task_fn(void *params) {
    blinker_cfg_t cfg = *(blinker_cfg_t *)params;
    vPortFree(params);

    /* Bypass led_display module — see bounce_task_fn comment for rationale. */
    led_force_output();

    for (;;) {
        led_force_set(cfg.pattern);
        vTaskDelay(pdMS_TO_TICKS(cfg.ms));
        led_force_set(0);
        vTaskDelay(pdMS_TO_TICKS(cfg.ms));
    }
}

static int run_blinker(int argc, char *argv[]) {
    /* blinker stop */
    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        if (s_blinker_handle) {
            vTaskDelete(s_blinker_handle);
            s_blinker_handle = NULL;
            led_display_clear();
            printf("Blinker stopped.\r\n");
        } else {
            printf("Blinker is not running.\r\n");
        }
        return KYBL_OK;
    }

    /* blinker <pattern 0-15> <ms> */
    if (argc < 3) {
        printf("Usage: blinker <pattern 0-15> <ms>   — start\r\n");
        printf("       blinker stop                   — stop\r\n");
        printf("  pattern is a 4-bit LED mask (e.g. 5 = 0101 = LEDs 0 and 2)\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    if (s_blinker_handle) {
        printf("Blinker already running. Use 'blinker stop' first.\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    long pattern = strtol(argv[1], NULL, 0);
    long ms      = strtol(argv[2], NULL, 0);
    if (pattern < 0 || pattern > 15 || ms < 10) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET
               " pattern 0-15, ms >= 10\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    blinker_cfg_t *cfg = pvPortMalloc(sizeof(blinker_cfg_t));
    if (!cfg) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " out of heap\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    cfg->pattern = (uint8_t)pattern;
    cfg->ms      = (uint32_t)ms;

    /* priority 2 = same as shell so the task actually time-slices. */
    BaseType_t ok = xTaskCreate(blinker_task_fn, "Blinker", 512,
                                cfg, 2, &s_blinker_handle);
    if (ok != pdPASS) {
        vPortFree(cfg);
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " failed to create task\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    printf("Blinker started: pattern=%ld (0b%c%c%c%c) @ %ld ms. "
           "'blinker stop' to halt.\r\n",
           pattern,
           (pattern>>3)&1?'1':'0', (pattern>>2)&1?'1':'0',
           (pattern>>1)&1?'1':'0', (pattern>>0)&1?'1':'0',
           ms);
    return KYBL_OK;
}

const kybl_program_t cmd_blinker = {
    .name        = "blinker",
    .usage       = "<0-15> <ms> | stop",
    .description = "Background LED blink pattern (FreeRTOS task)",
    .entry       = run_blinker,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  STRESS  –  spawn N short-lived FreeRTOS tasks, wait for all to complete
 *
 *  Uses a counting semaphore so each child signals the shell when it finishes.
 *  The old implementation async-printf'd from tasks after they had self-deleted
 *  which mangled the shell prompt and left the heap stats ambiguous. Now we
 *  give/take the semaphore, print a single clean before/after summary, and
 *  wait for every child before returning.
 * ════════════════════════════════════════════════════════════════════════════ */
typedef struct {
    uint32_t           ms;
    int                id;
    SemaphoreHandle_t  done;
} stress_cfg_t;

static void stress_task_fn(void *params) {
    stress_cfg_t *cfg = (stress_cfg_t *)params;
    /* Run for the requested duration */
    vTaskDelay(pdMS_TO_TICKS(cfg->ms));
    /* Signal completion BEFORE freeing our config */
    xSemaphoreGive(cfg->done);
    vPortFree(cfg);
    vTaskDelete(NULL);
}

static int run_stress(int argc, char *argv[]) {
    int      n  = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : 4;
    uint32_t ms = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 1500;

    if (n < 1 || n > 16) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " tasks must be 1-16\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    /* Snapshot heap + task count BEFORE */
    size_t heap_before  = xPortGetFreeHeapSize();
    UBaseType_t t_before = uxTaskGetNumberOfTasks();

    /* Counting semaphore: starts at 0, each child gives once on completion */
    SemaphoreHandle_t done = xSemaphoreCreateCounting((UBaseType_t)n, 0);
    if (!done) {
        printf(KYBL_ANSI_RED "Error:" KYBL_ANSI_RESET " failed to create semaphore\r\n");
        return KYBL_ERR_INVALID_ARG;
    }

    printf(KYBL_ANSI_BOLD "\r\n  Stress test\r\n" KYBL_ANSI_RESET);
    printf("  ─────────────────────────────────────────\r\n");
    printf("  Spawning %d task(s), each running %lu ms\r\n", n, (unsigned long)ms);
    printf("  Heap before : %u bytes free\r\n", (unsigned)heap_before);
    printf("  Tasks before: %u\r\n", (unsigned)t_before);

    int spawned = 0;
    for (int i = 0; i < n; i++) {
        stress_cfg_t *cfg = pvPortMalloc(sizeof(stress_cfg_t));
        if (!cfg) { printf("  [!] out of heap at task %d\r\n", i + 1); break; }
        cfg->ms   = ms;
        cfg->id   = i + 1;
        cfg->done = done;
        char name[12];
        snprintf(name, sizeof(name), "Stress%d", i + 1);
        if (xTaskCreate(stress_task_fn, name, 256, cfg, 1, NULL) != pdPASS) {
            vPortFree(cfg);
            printf("  [!] failed to create task %d\r\n", i + 1);
            break;
        }
        spawned++;
    }

    size_t heap_mid  = xPortGetFreeHeapSize();
    UBaseType_t t_mid = uxTaskGetNumberOfTasks();
    printf("  Heap during : %u bytes free (%d task(s) running)\r\n",
           (unsigned)heap_mid, (int)(t_mid - t_before));
    printf("  Waiting for %d task(s) to finish... ", spawned);
    fflush(stdout);

    /* Wait for every child's give(). Timeout = ms + generous slack. */
    TickType_t timeout = pdMS_TO_TICKS(ms + 5000);
    int completed = 0;
    for (int i = 0; i < spawned; i++) {
        if (xSemaphoreTake(done, timeout) == pdTRUE) {
            completed++;
            /* LED progress bar: lower-4 bits show fraction */
            uint8_t mask = (uint8_t)((completed * 4 / (spawned ? spawned : 1)) & 0xF);
            uint8_t bits = 0;
            for (int b = 0; b < 4 && b < (mask + 1); b++) bits |= (1u << b);
            if (completed == spawned) bits = 0x0F;
            led_display_set(bits);
        }
    }
    printf(KYBL_ANSI_GREEN "done\r\n" KYBL_ANSI_RESET);

    vSemaphoreDelete(done);

    /* Brief flash to mark completion, then clear */
    vTaskDelay(pdMS_TO_TICKS(200));
    led_display_clear();

    /* Let self-deleted tasks actually get reaped by the idle task */
    vTaskDelay(pdMS_TO_TICKS(50));

    size_t heap_after  = xPortGetFreeHeapSize();
    UBaseType_t t_after = uxTaskGetNumberOfTasks();

    printf("  Completed   : %d / %d\r\n", completed, spawned);
    printf("  Heap after  : %u bytes free", (unsigned)heap_after);
    long delta = (long)heap_after - (long)heap_before;
    if (delta == 0)      printf(" (" KYBL_ANSI_GREEN  "no leak" KYBL_ANSI_RESET ")\r\n");
    else if (delta > 0)  printf(" (+%ld recovered)\r\n", delta);
    else                 printf(" (" KYBL_ANSI_YELLOW "%ld leaked" KYBL_ANSI_RESET ")\r\n", -delta);
    printf("  Tasks after : %u\r\n", (unsigned)t_after);
    printf("  ─────────────────────────────────────────\r\n\r\n");

    return (completed == spawned) ? KYBL_OK : KYBL_ERR_INVALID_ARG;
}

const kybl_program_t cmd_stress = {
    .name        = "stress",
    .usage       = "[tasks] [ms]",
    .description = "Spawn N temporary FreeRTOS tasks (default: 4, 1500ms)",
    .entry       = run_stress,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  SNAKE  –  terminal Snake game (runs inline, takes over the shell)
 *
 *  Controls: WASD or arrow keys.  Q to quit.
 *  Renders using ANSI escape codes — use a colour terminal (PuTTY, screen…)
 * ════════════════════════════════════════════════════════════════════════════ */
#define SNK_W  36       /* board inner width  (must be even) */
#define SNK_H  16       /* board inner height                */
#define SNK_MAX_LEN  (SNK_W * SNK_H)

typedef struct { int x, y; } snk_pt_t;

static snk_pt_t snk_body[SNK_MAX_LEN];
static int      snk_len;
static snk_pt_t snk_food;
static int      snk_dx, snk_dy;
static int      snk_score;
static bool     snk_dead;

/* Cursor positioning helper */
static void snk_goto(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

static void snk_place_food(void) {
    /* Keep trying random spots until one is free of the snake body */
    static uint32_t rng = 0x1234ABCD;
    for (int attempt = 0; attempt < 200; attempt++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        int fx = (int)(rng % (uint32_t)SNK_W);
        int fy = (int)((rng >> 8) % (uint32_t)SNK_H);
        bool clear = true;
        for (int i = 0; i < snk_len; i++)
            if (snk_body[i].x == fx && snk_body[i].y == fy) { clear = false; break; }
        if (clear) { snk_food.x = fx; snk_food.y = fy; return; }
    }
}

static void snk_draw_board(void) {
    /* Top border */
    snk_goto(1, 1);
    printf(KYBL_ANSI_GREEN "+" );
    for (int x = 0; x < SNK_W; x++) printf("-");
    printf("+" KYBL_ANSI_RESET);

    /* Side borders + clear interior */
    for (int y = 0; y < SNK_H; y++) {
        snk_goto(y + 2, 1);
        printf(KYBL_ANSI_GREEN "|" KYBL_ANSI_RESET);
        for (int x = 0; x < SNK_W; x++) printf(" ");
        printf(KYBL_ANSI_GREEN "|" KYBL_ANSI_RESET);
    }

    /* Bottom border */
    snk_goto(SNK_H + 2, 1);
    printf(KYBL_ANSI_GREEN "+");
    for (int x = 0; x < SNK_W; x++) printf("-");
    printf("+" KYBL_ANSI_RESET);

    /* Status line */
    snk_goto(SNK_H + 3, 1);
    printf("Score: " KYBL_ANSI_CYAN "%-6d" KYBL_ANSI_RESET
           "  WASD/arrows = move   Q = quit", snk_score);
}

static void snk_draw_cell(int x, int y, const char *ch) {
    snk_goto(y + 2, x + 2);   /* +2 offset for border */
    printf("%s", ch);
}

static int run_snake(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Init state */
    snk_len   = 3;
    snk_dx    = 1; snk_dy = 0;
    snk_score = 0;
    snk_dead  = false;
    snk_body[0] = (snk_pt_t){ SNK_W/2,     SNK_H/2 };
    snk_body[1] = (snk_pt_t){ SNK_W/2 - 1, SNK_H/2 };
    snk_body[2] = (snk_pt_t){ SNK_W/2 - 2, SNK_H/2 };
    snk_place_food();

    /* Hide cursor, clear screen */
    printf("\033[2J\033[?25l");
    snk_draw_board();

    /* Draw initial snake */
    for (int i = 0; i < snk_len; i++)
        snk_draw_cell(snk_body[i].x, snk_body[i].y,
                      i == 0 ? KYBL_ANSI_GREEN "@" KYBL_ANSI_RESET
                              : KYBL_ANSI_GREEN "o" KYBL_ANSI_RESET);

    /* Draw food */
    snk_draw_cell(snk_food.x, snk_food.y,
                  KYBL_ANSI_RED "*" KYBL_ANSI_RESET);

    fflush(stdout);

    int pending_dx = snk_dx, pending_dy = snk_dy;
    int esc_st = 0;

    while (!snk_dead) {
        /* Drain ALL pending input before advancing the frame. A single arrow
           key is an ESC '[' 'A' three-byte sequence — the old code processed
           one byte per tick (continue), so the snake kept moving in the old
           direction for ~240 ms before turning. Here we take one 80 ms
           blocking read (our frame pace), then soak up any remaining bytes
           non-blocking. Only the first valid direction change in a tick is
           accepted, which prevents insta-reversal via two rapid keypresses. */
        bool dir_changed = false;
        int  c           = getchar_timeout_us(80000);

        while (c != PICO_ERROR_TIMEOUT) {
            if (esc_st == 1) {
                esc_st = (c == '[') ? 2 : 0;
            } else if (esc_st == 2) {
                esc_st = 0;
                if (!dir_changed) {
                    if      (c == 'A' && snk_dy != 1)  { pending_dx = 0;  pending_dy = -1; dir_changed = true; }
                    else if (c == 'B' && snk_dy != -1) { pending_dx = 0;  pending_dy =  1; dir_changed = true; }
                    else if (c == 'C' && snk_dx != -1) { pending_dx = 1;  pending_dy =  0; dir_changed = true; }
                    else if (c == 'D' && snk_dx != 1)  { pending_dx = -1; pending_dy =  0; dir_changed = true; }
                }
            } else if (c == 0x1B) {
                esc_st = 1;
            } else {
                char ch = (char)c;
                if (!dir_changed) {
                    if      ((ch=='w'||ch=='W') && snk_dy != 1)  { pending_dx = 0;  pending_dy = -1; dir_changed = true; }
                    else if ((ch=='s'||ch=='S') && snk_dy != -1) { pending_dx = 0;  pending_dy =  1; dir_changed = true; }
                    else if ((ch=='d'||ch=='D') && snk_dx != -1) { pending_dx = 1;  pending_dy =  0; dir_changed = true; }
                    else if ((ch=='a'||ch=='A') && snk_dx != 1)  { pending_dx = -1; pending_dy =  0; dir_changed = true; }
                }
                if (ch=='q'||ch=='Q')  { snk_dead = true; break; }
            }
            /* Non-blocking drain of any remaining bytes in the buffer. */
            c = getchar_timeout_us(0);
        }
        if (snk_dead) break;

        /* Commit direction */
        snk_dx = pending_dx;
        snk_dy = pending_dy;

        /* Compute new head */
        snk_pt_t nh = { snk_body[0].x + snk_dx, snk_body[0].y + snk_dy };

        /* Wall collision */
        if (nh.x < 0 || nh.x >= SNK_W || nh.y < 0 || nh.y >= SNK_H) {
            snk_dead = true; break;
        }

        /* Self collision */
        for (int i = 0; i < snk_len - 1; i++) {
            if (snk_body[i].x == nh.x && snk_body[i].y == nh.y) {
                snk_dead = true; break;
            }
        }
        if (snk_dead) break;

        bool ate = (nh.x == snk_food.x && nh.y == snk_food.y);

        /* Erase tail (before shift) unless we ate */
        if (!ate) {
            snk_pt_t tail = snk_body[snk_len - 1];
            snk_draw_cell(tail.x, tail.y, " ");
        }

        /* Shift body */
        if (ate && snk_len < SNK_MAX_LEN) snk_len++;
        for (int i = snk_len - 1; i > 0; i--)
            snk_body[i] = snk_body[i - 1];
        snk_body[0] = nh;

        /* Redraw head and neck */
        snk_draw_cell(snk_body[0].x, snk_body[0].y,
                      KYBL_ANSI_GREEN "@" KYBL_ANSI_RESET);
        if (snk_len > 1)
            snk_draw_cell(snk_body[1].x, snk_body[1].y,
                          KYBL_ANSI_GREEN "o" KYBL_ANSI_RESET);

        if (ate) {
            snk_score += 10;
            snk_place_food();
            snk_draw_cell(snk_food.x, snk_food.y,
                          KYBL_ANSI_RED "*" KYBL_ANSI_RESET);
            /* Update score */
            snk_goto(SNK_H + 3, 8);
            printf(KYBL_ANSI_CYAN "%-6d" KYBL_ANSI_RESET, snk_score);
            led_display_set((uint8_t)(snk_score & 0x0F));
        }

        fflush(stdout);
    }

    /* Game over screen */
    snk_goto(SNK_H / 2 + 1, SNK_W / 2 - 4);
    printf(KYBL_ANSI_BOLD KYBL_ANSI_RED "GAME OVER" KYBL_ANSI_RESET);
    snk_goto(SNK_H / 2 + 2, SNK_W / 2 - 5);
    printf("Score: " KYBL_ANSI_CYAN "%d" KYBL_ANSI_RESET, snk_score);
    snk_goto(SNK_H + 4, 1);
    printf("\r\n");

    /* Restore cursor, pause so player sees the score */
    printf("\033[?25h");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));
    led_display_clear();

    /* Redraw shell banner */
    printf("\033[2J\033[H");
    printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN
           "  kyblRTOS v" KYBL_VERSION_STR KYBL_ANSI_RESET
           "  — snake done. Final score: "
           KYBL_ANSI_CYAN "%d" KYBL_ANSI_RESET "\r\n\r\n", snk_score);
    return KYBL_OK;
}

const kybl_program_t cmd_snake = {
    .name        = "snake",
    .usage       = "",
    .description = "Play Snake in the terminal (WASD/arrows, Q=quit)",
    .entry       = run_snake,
};

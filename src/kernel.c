#include "kernel.h"
#include "commands.h"
#include "kyblrtos.h"
#include <string.h>
#include <stdio.h>

/* ── Registry ─────────────────────────────────────────────────────────────── */
static kybl_program_t s_registry[KYBL_MAX_PROGRAMS];
static int            s_count = 0;

/* ── Internal helpers ─────────────────────────────────────────────────────── */
static void register_builtins(void) {
    /* Declared in commands.h */
    extern const kybl_program_t cmd_help;
    extern const kybl_program_t cmd_ver;
    extern const kybl_program_t cmd_print;
    extern const kybl_program_t cmd_bin;
    extern const kybl_program_t cmd_clear;
    extern const kybl_program_t cmd_tasks;
    extern const kybl_program_t cmd_mem;
    extern const kybl_program_t cmd_echo;
    extern const kybl_program_t cmd_led;
    extern const kybl_program_t cmd_reboot;
    extern const kybl_program_t cmd_rand;
    extern const kybl_program_t cmd_calc;
    extern const kybl_program_t cmd_uptime;
    extern const kybl_program_t cmd_stackcheck;
    extern const kybl_program_t cmd_fragcheck;
    extern const kybl_program_t cmd_note;
    extern const kybl_program_t cmd_notes;
    extern const kybl_program_t cmd_bounce;
    extern const kybl_program_t cmd_blinker;
    extern const kybl_program_t cmd_stress;
    extern const kybl_program_t cmd_snake;
    extern const kybl_program_t cmd_wifi;
    extern const kybl_program_t cmd_ping;
    extern const kybl_program_t cmd_nslookup;
    extern const kybl_program_t cmd_netstat;
    extern const kybl_program_t cmd_traceroute;
    extern const kybl_program_t cmd_mount;
    extern const kybl_program_t cmd_umount;
    extern const kybl_program_t cmd_format;
    extern const kybl_program_t cmd_sdinfo;
    extern const kybl_program_t cmd_df;
    extern const kybl_program_t cmd_ls;
    extern const kybl_program_t cmd_cat;
    extern const kybl_program_t cmd_write;
    extern const kybl_program_t cmd_append;
    extern const kybl_program_t cmd_rm;
    extern const kybl_program_t cmd_mkdir;
    extern const kybl_program_t cmd_touch;
    extern const kybl_program_t cmd_stat;
    extern const kybl_program_t cmd_mv;
    extern const kybl_program_t cmd_label;
    extern const kybl_program_t cmd_kbltext;

    kernel_register(&cmd_help);
    kernel_register(&cmd_ver);
    kernel_register(&cmd_print);
    kernel_register(&cmd_bin);
    kernel_register(&cmd_clear);
    kernel_register(&cmd_tasks);
    kernel_register(&cmd_mem);
    kernel_register(&cmd_echo);
    kernel_register(&cmd_led);
    kernel_register(&cmd_reboot);
    kernel_register(&cmd_rand);
    kernel_register(&cmd_calc);
    kernel_register(&cmd_uptime);
    kernel_register(&cmd_stackcheck);
    kernel_register(&cmd_fragcheck);
    kernel_register(&cmd_note);
    kernel_register(&cmd_notes);
    kernel_register(&cmd_bounce);
    kernel_register(&cmd_blinker);
    kernel_register(&cmd_stress);
    kernel_register(&cmd_snake);
    kernel_register(&cmd_wifi);
    kernel_register(&cmd_ping);
    kernel_register(&cmd_nslookup);
    kernel_register(&cmd_netstat);
    kernel_register(&cmd_traceroute);
    kernel_register(&cmd_mount);
    kernel_register(&cmd_umount);
    kernel_register(&cmd_format);
    kernel_register(&cmd_sdinfo);
    kernel_register(&cmd_df);
    kernel_register(&cmd_ls);
    kernel_register(&cmd_cat);
    kernel_register(&cmd_write);
    kernel_register(&cmd_append);
    kernel_register(&cmd_rm);
    kernel_register(&cmd_mkdir);
    kernel_register(&cmd_touch);
    kernel_register(&cmd_stat);
    kernel_register(&cmd_mv);
    kernel_register(&cmd_label);
    kernel_register(&cmd_kbltext);
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void kernel_init(void) {
    s_count = 0;
    memset(s_registry, 0, sizeof(s_registry));
    register_builtins();
}

int kernel_register(const kybl_program_t *prog) {
    if (!prog || !prog->name || !prog->entry) return KYBL_ERR_INVALID_ARG;
    if (s_count >= KYBL_MAX_PROGRAMS)             return KYBL_ERR_FULL;

    s_registry[s_count++] = *prog;
    return KYBL_OK;
}

const kybl_program_t *kernel_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_registry[i].name, name) == 0)
            return &s_registry[i];
    }
    return NULL;
}

int kernel_exec(const char *name, int argc, char *argv[]) {
    const kybl_program_t *p = kernel_find(name);
    if (!p) return KYBL_ERR_NOT_FOUND;
    return p->entry(argc, argv);
}

const kybl_program_t *kernel_next(int *idx) {
    if (!idx || *idx >= s_count) return NULL;
    return &s_registry[(*idx)++];
}

int kernel_count(void) {
    return s_count;
}

/* ── SD-card stub ─────────────────────────────────────────────────────────── */
int kernel_load_from_sd(const char *path) {
    (void)path;
    printf("[kernel] SD-card loader not yet implemented.\r\n");
    return KYBL_ERR_NOT_FOUND;
}

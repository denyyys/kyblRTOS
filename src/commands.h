#ifndef COMMANDS_H
#define COMMANDS_H

#include "kyblrtos.h"

/* ── Built-in command descriptors (defined in commands.c) ─────────────────── */
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

/* ── Network command descriptors (defined in net_commands.c) ──────────────── */
extern const kybl_program_t cmd_wifi;
extern const kybl_program_t cmd_ping;
extern const kybl_program_t cmd_nslookup;
extern const kybl_program_t cmd_netstat;
extern const kybl_program_t cmd_traceroute;

/* ── Filesystem command descriptors (defined in fs_commands.c) ────────────── */
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

/* ── User programs ────────────────────────────────────────────────────────── */
extern const kybl_program_t cmd_kbltext;   /* kbltext.c */

#endif /* COMMANDS_H */

/*---------------------------------------------------------------------------/
/  Shell commands for kyblFS
/
/  All filesystem work goes through kyblFS, never FatFs directly, so the
/  same mutex-protected path is exercised as user apps will later use.
/---------------------------------------------------------------------------*/

#include "commands.h"
#include "kyblFS.h"
#include "sd_spi.h"
#include "kyblrtos.h"

#include "pico/stdlib.h"       /* getchar_timeout_us, PICO_ERROR_TIMEOUT      */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Shared helpers ────────────────────────────────────────────────────── */
static bool require_mount(void) {
    if (!kyblFS_is_mounted()) {
        printf(KYBL_ANSI_RED "No filesystem mounted." KYBL_ANSI_RESET
               "  Run 'mount' first.\r\n");
        return false;
    }
    return true;
}

static void print_size_human(uint64_t bytes) {
    /* newlib-nano drops 64-bit printf support, so small sizes go through
       unsigned long and larger ones through double — the float path is
       already enabled because we use it for RSSI / ping times. */
    const char *suffix[] = { "B", "KB", "MB", "GB", "TB" };
    int         s        = 0;
    double      v        = (double)bytes;
    while (v >= 1024.0 && s < 4) { v /= 1024.0; s++; }
    if (s == 0) printf("%lu %s",  (unsigned long)bytes, suffix[s]);
    else        printf("%.2f %s", v, suffix[s]);
}

static const char *card_type_str(sd_card_type_t t) {
    switch (t) {
    case SD_CARD_V1:    return "SDSC v1.x";
    case SD_CARD_V2_SC: return "SDSC v2.x";
    case SD_CARD_V2_HC: return "SDHC/SDXC";
    default:            return "unknown";
    }
}

static void print_date_time(uint16_t date, uint16_t time) {
    int y = ((date >> 9) & 0x7F) + 1980;
    int m = (date >> 5) & 0x0F;
    int d =  date       & 0x1F;
    int H = (time >> 11) & 0x1F;
    int M = (time >>  5) & 0x3F;
    printf("%04d-%02d-%02d %02d:%02d", y, m, d, H, M);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  mount / umount / format / sdinfo
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_mount(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (kyblFS_is_mounted()) { printf("Already mounted.\r\n"); return KYBL_OK; }

    printf("Mounting SD card... "); fflush(stdout);
    int r = kyblFS_mount();
    if (r == KYBLFS_OK) {
        printf(KYBL_ANSI_GREEN "OK\r\n" KYBL_ANSI_RESET);
        char lbl[12] = {0};
        kyblFS_label(lbl, sizeof(lbl));
        printf("  Label: %s\r\n", (lbl[0] ? lbl : "(none)"));
    } else {
        printf(KYBL_ANSI_RED "FAILED: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
        printf("  Card wired to GP2(CLK)/GP3(MOSI)/GP4(MISO)/GP5(CS)?\r\n");
        printf("  Card formatted FAT32?  (Try 'format' to reformat.)\r\n");
    }
    return KYBL_OK;
}

static int run_umount(int argc, char *argv[]) {
    (void)argc; (void)argv;
    int r = kyblFS_unmount();
    if (r == KYBLFS_OK) printf("Unmounted.\r\n");
    else                printf(KYBL_ANSI_RED "umount failed: %s\r\n" KYBL_ANSI_RESET,
                               kyblFS_strerror(r));
    return KYBL_OK;
}

static int run_format(int argc, char *argv[]) {
    const char *label = (argc >= 2) ? argv[1] : "KYBL";

    printf(KYBL_ANSI_YELLOW
           "This will ERASE ALL DATA on the SD card. Type 'yes' to continue: "
           KYBL_ANSI_RESET);
    fflush(stdout);

    char line[16] = {0};
    int n = 0;
    for (;;) {
        int c = getchar_timeout_us(30000000);
        if (c == PICO_ERROR_TIMEOUT) { printf("\r\n"); return KYBL_OK; }
        if (c == '\r' || c == '\n') { printf("\r\n"); break; }
        if (n < (int)sizeof(line) - 1 && isprint(c)) {
            line[n++] = (char)c; line[n] = '\0';
            printf("%c", c); fflush(stdout);
        }
    }
    if (strcmp(line, "yes") != 0) { printf("Aborted.\r\n"); return KYBL_OK; }

    printf("Formatting as FAT32 (label='%s')... ", label); fflush(stdout);
    int r = kyblFS_format(label);
    if (r == KYBLFS_OK) printf(KYBL_ANSI_GREEN "OK\r\n" KYBL_ANSI_RESET);
    else                printf(KYBL_ANSI_RED "FAILED: %s\r\n" KYBL_ANSI_RESET,
                               kyblFS_strerror(r));
    printf("  Remember to 'mount' before using it.\r\n");
    return KYBL_OK;
}

static int run_sdinfo(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("\r\n  SD card\r\n");
    printf("  ────────────────────────────────────────\r\n");

    printf("  Driver   : %s\r\n",
           sd_spi_is_ready() ? KYBL_ANSI_GREEN "ready" KYBL_ANSI_RESET
                             : KYBL_ANSI_YELLOW "not init" KYBL_ANSI_RESET);
    printf("  Type     : %s\r\n", card_type_str(sd_spi_card_type()));
    printf("  Sectors  : %lu (%.2f MB)\r\n",
           (unsigned long)sd_spi_sector_count(),
           (double)sd_spi_sector_count() * 512.0 / (1024.0 * 1024.0));
    printf("  Pins     : CLK=GP2  MOSI=GP3  MISO=GP4  CS=GP5\r\n");

    printf("  FS       : %s\r\n",
           kyblFS_is_mounted() ? KYBL_ANSI_GREEN "mounted" KYBL_ANSI_RESET
                               : KYBL_ANSI_YELLOW "unmounted" KYBL_ANSI_RESET);

    if (kyblFS_is_mounted()) {
        uint64_t total = 0, freeb = 0;
        if (kyblFS_statvfs(&total, &freeb) == KYBLFS_OK) {
            printf("  Size     : "); print_size_human(total); printf("\r\n");
            printf("  Free     : "); print_size_human(freeb);
            printf("  (%.1f%%)\r\n", total ? 100.0 * freeb / total : 0.0);
        }
        char lbl[12] = {0};
        kyblFS_label(lbl, sizeof(lbl));
        printf("  Label    : %s\r\n", lbl[0] ? lbl : "(none)");
    }
    printf("\r\n");
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  df
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_df(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!require_mount()) return KYBL_OK;

    uint64_t total = 0, freeb = 0;
    int r = kyblFS_statvfs(&total, &freeb);
    if (r != KYBLFS_OK) {
        printf(KYBL_ANSI_RED "df: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
        return KYBL_OK;
    }
    uint64_t used = total - freeb;
    printf("  Total: "); print_size_human(total); printf("\r\n");
    printf("  Used : "); print_size_human(used);  printf("\r\n");
    printf("  Free : "); print_size_human(freeb); printf("\r\n");
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  ls
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_ls(int argc, char *argv[]) {
    if (!require_mount()) return KYBL_OK;

    const char *path = (argc >= 2) ? argv[1] : "/";
    kybl_dir_t *d = kyblFS_opendir(path);
    if (!d) {
        printf(KYBL_ANSI_RED "ls: cannot open '%s'\r\n" KYBL_ANSI_RESET, path);
        return KYBL_OK;
    }

    printf(KYBL_ANSI_BOLD
           "  %-30s %10s  %-19s %s\r\n"
           KYBL_ANSI_RESET,
           "Name", "Size", "Modified", "Attr");

    kybl_finfo_t info;
    int items = 0;
    int r;
    while ((r = kyblFS_readdir(d, &info)) == 1) {
        bool is_dir = (info.attr & KYBLFS_ATTR_DIR);
        if (is_dir) printf("  " KYBL_ANSI_CYAN "%-30s" KYBL_ANSI_RESET, info.name);
        else        printf("  %-30s", info.name);

        if (is_dir) printf(" %10s  ", "<DIR>");
        else        printf(" %10lu  ", (unsigned long)info.size);

        print_date_time(info.date, info.time);

        printf("  ");
        if (info.attr & KYBLFS_ATTR_RO)      printf("R");
        if (info.attr & KYBLFS_ATTR_HIDDEN)  printf("H");
        if (info.attr & KYBLFS_ATTR_SYSTEM)  printf("S");
        if (info.attr & KYBLFS_ATTR_ARCHIVE) printf("A");
        printf("\r\n");
        items++;
    }
    kyblFS_closedir(d);

    if (r < 0) printf(KYBL_ANSI_RED "ls: read error\r\n" KYBL_ANSI_RESET);
    printf("  %d item%s\r\n", items, items == 1 ? "" : "s");
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  cat
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_cat(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: cat <path>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    kybl_file_t *f = kyblFS_open(argv[1], KYBLFS_READ);
    if (!f) {
        printf(KYBL_ANSI_RED "cat: cannot open '%s'\r\n" KYBL_ANSI_RESET, argv[1]);
        return KYBL_OK;
    }

    char buf[128];
    int  n;
    while ((n = kyblFS_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            int c = (unsigned char)buf[i];
            if (c == '\n') { putchar('\r'); putchar('\n'); }
            else             putchar(c);
        }
    }
    kyblFS_close(f);
    printf("\r\n");
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  write <path> <text...>  — quick text write (space-joined)
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_write(int argc, char *argv[]) {
    if (argc < 3) { printf("Usage: write <path> <text...>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    kybl_file_t *f = kyblFS_open(argv[1], KYBLFS_WRITE | KYBLFS_CREATE | KYBLFS_TRUNCATE);
    if (!f) {
        printf(KYBL_ANSI_RED "write: cannot open '%s'\r\n" KYBL_ANSI_RESET, argv[1]);
        return KYBL_OK;
    }

    /* Join the remaining tokens with spaces so 'write a.txt hello world'
       writes "hello world\n" — what the user almost certainly meant. */
    int total = 0;
    for (int i = 2; i < argc; i++) {
        int len = (int)strlen(argv[i]);
        int n = kyblFS_write(f, argv[i], len);
        if (n < 0) { printf(KYBL_ANSI_RED "write error\r\n" KYBL_ANSI_RESET); break; }
        total += n;
        if (i + 1 < argc) { kyblFS_write(f, " ", 1); total += 1; }
    }
    kyblFS_write(f, "\n", 1); total += 1;
    kyblFS_close(f);
    printf("Wrote %d bytes to %s\r\n", total, argv[1]);
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  append <path> <text...>  — append-mode variant of write
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_append(int argc, char *argv[]) {
    if (argc < 3) { printf("Usage: append <path> <text...>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    kybl_file_t *f = kyblFS_open(argv[1], KYBLFS_WRITE | KYBLFS_CREATE | KYBLFS_APPEND);
    if (!f) {
        printf(KYBL_ANSI_RED "append: cannot open '%s'\r\n" KYBL_ANSI_RESET, argv[1]);
        return KYBL_OK;
    }
    int total = 0;
    for (int i = 2; i < argc; i++) {
        total += kyblFS_write(f, argv[i], strlen(argv[i]));
        if (i + 1 < argc) total += kyblFS_write(f, " ", 1);
    }
    total += kyblFS_write(f, "\n", 1);
    kyblFS_close(f);
    printf("Appended %d bytes to %s\r\n", total, argv[1]);
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  rm / mkdir / touch / stat / mv
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_rm(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: rm <path>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    int r = kyblFS_unlink(argv[1]);
    if (r == KYBLFS_OK) printf("Removed '%s'.\r\n", argv[1]);
    else                printf(KYBL_ANSI_RED "rm: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
    return KYBL_OK;
}

static int run_mkdir(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: mkdir <path>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    int r = kyblFS_mkdir(argv[1]);
    if (r == KYBLFS_OK) printf("Created directory '%s'.\r\n", argv[1]);
    else                printf(KYBL_ANSI_RED "mkdir: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
    return KYBL_OK;
}

static int run_touch(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: touch <path>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    int r = kyblFS_touch(argv[1]);
    if (r != KYBLFS_OK) printf(KYBL_ANSI_RED "touch: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
    return KYBL_OK;
}

static int run_stat(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: stat <path>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    kybl_finfo_t info;
    int r = kyblFS_stat(argv[1], &info);
    if (r != KYBLFS_OK) {
        printf(KYBL_ANSI_RED "stat: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
        return KYBL_OK;
    }
    printf("  Path     : %s\r\n", argv[1]);
    printf("  Name     : %s\r\n", info.name);
    printf("  Size     : %lu bytes\r\n", (unsigned long)info.size);
    printf("  Modified : "); print_date_time(info.date, info.time); printf("\r\n");
    printf("  Type     : %s\r\n", (info.attr & KYBLFS_ATTR_DIR) ? "directory" : "file");
    printf("  Attr     : %s%s%s%s\r\n",
           (info.attr & KYBLFS_ATTR_RO)      ? "R" : "-",
           (info.attr & KYBLFS_ATTR_HIDDEN)  ? "H" : "-",
           (info.attr & KYBLFS_ATTR_SYSTEM)  ? "S" : "-",
           (info.attr & KYBLFS_ATTR_ARCHIVE) ? "A" : "-");
    return KYBL_OK;
}

static int run_mv(int argc, char *argv[]) {
    if (argc < 3) { printf("Usage: mv <from> <to>\r\n"); return KYBL_ERR_INVALID_ARG; }
    if (!require_mount()) return KYBL_OK;

    int r = kyblFS_rename(argv[1], argv[2]);
    if (r == KYBLFS_OK) printf("'%s' -> '%s'\r\n", argv[1], argv[2]);
    else                printf(KYBL_ANSI_RED "mv: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  label  — read or set the FAT volume label without reformatting
 *
 *  FAT/FAT32 labels are at most 11 characters, ASCII-printable, no leading
 *  space, and must avoid the set " * + , . / : ; < = > ? [ \ ] | . FatFs
 *  uppercases automatically. Lowercase here is silently accepted by the
 *  library; we just clamp length and reject obvious bad chars.
 * ══════════════════════════════════════════════════════════════════════════ */
static int run_label(int argc, char *argv[]) {
    if (!require_mount()) return KYBL_OK;

    /* No argument — print current label */
    if (argc < 2) {
        char lbl[12] = {0};
        int r = kyblFS_label(lbl, sizeof(lbl));
        if (r != KYBLFS_OK) {
            printf(KYBL_ANSI_RED "label: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
            return KYBL_OK;
        }
        printf("  Volume label: %s\r\n", lbl[0] ? lbl : "(none)");
        printf("  Use 'label <new_name>' to change (≤11 chars, no special).\r\n");
        return KYBL_OK;
    }

    const char *new_label = argv[1];
    if (strlen(new_label) > 11) {
        printf(KYBL_ANSI_RED
               "label: too long (max 11 characters for FAT)\r\n"
               KYBL_ANSI_RESET);
        return KYBL_ERR_INVALID_ARG;
    }
    /* Reject characters FatFs will refuse — give the user the actual reason
       instead of a generic "Invalid argument" from kyblFS_strerror later. */
    static const char *reject = "*+,./:;<=>?[\\]|\"";
    for (const char *p = new_label; *p; p++) {
        if ((unsigned char)*p < 0x20 || strchr(reject, *p)) {
            printf(KYBL_ANSI_RED
                   "label: '%c' is not allowed in FAT volume labels\r\n"
                   KYBL_ANSI_RESET, *p);
            return KYBL_ERR_INVALID_ARG;
        }
    }

    int r = kyblFS_set_label(new_label);
    if (r == KYBLFS_OK) {
        printf("Volume label set to '%s'.\r\n", new_label);
    } else {
        printf(KYBL_ANSI_RED "label: %s\r\n" KYBL_ANSI_RESET, kyblFS_strerror(r));
    }
    return KYBL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Command descriptors
 * ══════════════════════════════════════════════════════════════════════════ */
const kybl_program_t cmd_mount   = { "mount",   "",                "Mount the SD card",             run_mount  };
const kybl_program_t cmd_umount  = { "umount",  "",                "Unmount the SD card",           run_umount };
const kybl_program_t cmd_format  = { "format",  "[label]",         "Format SD as FAT32 (DESTRUCTIVE)", run_format };
const kybl_program_t cmd_sdinfo  = { "sdinfo",  "",                "Show SD card + FS info",        run_sdinfo };
const kybl_program_t cmd_df      = { "df",      "",                "Show disk usage",               run_df     };
const kybl_program_t cmd_ls      = { "ls",      "[path]",          "List directory contents",       run_ls     };
const kybl_program_t cmd_cat     = { "cat",     "<path>",          "Print file contents",           run_cat    };
const kybl_program_t cmd_write   = { "write",   "<path> <text...>","Write text to a file (truncate)", run_write};
const kybl_program_t cmd_append  = { "append",  "<path> <text...>","Append text to a file",         run_append };
const kybl_program_t cmd_rm      = { "rm",      "<path>",          "Remove file or empty directory", run_rm    };
const kybl_program_t cmd_mkdir   = { "mkdir",   "<path>",          "Create a directory",            run_mkdir  };
const kybl_program_t cmd_touch   = { "touch",   "<path>",          "Create empty file",             run_touch  };
const kybl_program_t cmd_stat    = { "stat",    "<path>",          "Show file/dir info",            run_stat   };
const kybl_program_t cmd_mv      = { "mv",      "<from> <to>",     "Rename / move",                 run_mv     };
const kybl_program_t cmd_label   = { "label",   "[new_name]",      "Show or set the volume label",  run_label  };

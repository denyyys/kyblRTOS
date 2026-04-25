#include "commands.h"
#include "wifi_manager.h"
#include "kyblrtos.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/timer.h"

#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  SHARED UTILITIES
 * ════════════════════════════════════════════════════════════════════════════ */

/* Read a line from USB serial, masking each character as '*'.
   Used for password entry. Returns number of chars read. */
static int read_masked_line(char *buf, int max) {
    int n = 0;
    buf[0] = '\0';
    for (;;) {
        int c = getchar_timeout_us(30000000); /* 30s timeout */
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == '\r' || c == '\n') { printf("\r\n"); break; }
        if ((c == 0x08 || c == 0x7F) && n > 0) {
            n--;
            buf[n] = '\0';
            printf("\b \b");
            fflush(stdout);
            continue;
        }
        if (isprint(c) && n < max - 1) {
            buf[n++] = (char)c;
            buf[n]   = '\0';
            printf("*");
            fflush(stdout);
        }
    }
    return n;
}

/* Read a plain line (for AP number prompts etc.) */
static int read_plain_line(char *buf, int max) {
    int n = 0;
    buf[0] = '\0';
    for (;;) {
        int c = getchar_timeout_us(30000000);
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == '\r' || c == '\n') { printf("\r\n"); break; }
        if ((c == 0x08 || c == 0x7F) && n > 0) {
            n--; buf[n] = '\0';
            printf("\b \b"); fflush(stdout); continue;
        }
        if (isprint(c) && n < max - 1) {
            buf[n++] = (char)c; buf[n] = '\0';
            printf("%c", c); fflush(stdout);
        }
    }
    return n;
}

/* Check WiFi is up before running a net command */
static bool require_wifi(void) {
    if (!wifi_is_connected()) {
        printf(KYBL_ANSI_RED "Not connected." KYBL_ANSI_RESET
               " Use 'wifi menu' or 'wifi connect <ssid> [pass]'\r\n");
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  WIFI MENU  –  full-screen live AP scanner
 * ════════════════════════════════════════════════════════════════════════════ */
#define MENU_W  74   /* total table width */

static void menu_draw_header(bool scanning) {
    printf("\033[2J\033[H");   /* clear + home */
    printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN
           "  kyblRTOS — WiFi Scanner" KYBL_ANSI_RESET);
    if (scanning)
        printf("  " KYBL_ANSI_YELLOW "[Scanning...]" KYBL_ANSI_RESET);
    printf("\r\n");

    /* Top border */
    printf("  \xe2\x94\x8c");                               /* ┌ */
    for (int i = 0; i < 3; i++)  printf("\xe2\x94\x80");   /* ─── */
    printf("\xe2\x94\xac");                                  /* ┬ */
    for (int i = 0; i < 30; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xac");
    for (int i = 0; i < 13; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xac");
    for (int i = 0; i < 4; i++)  printf("\xe2\x94\x80");
    printf("\xe2\x94\xac");
    for (int i = 0; i < 10; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x90\r\n");                              /* ┐ */

    /* Column headers */
    printf("  \xe2\x94\x82" KYBL_ANSI_BOLD " # " KYBL_ANSI_RESET
           "\xe2\x94\x82" KYBL_ANSI_BOLD " %-28s " KYBL_ANSI_RESET
           "\xe2\x94\x82" KYBL_ANSI_BOLD " %-11s " KYBL_ANSI_RESET
           "\xe2\x94\x82" KYBL_ANSI_BOLD " Ch " KYBL_ANSI_RESET
           "\xe2\x94\x82" KYBL_ANSI_BOLD " %-8s " KYBL_ANSI_RESET
           "\xe2\x94\x82\r\n",
           "SSID", "Signal", "Security");

    /* Separator */
    printf("  \xe2\x94\x9c");
    for (int i = 0; i < 3; i++)  printf("\xe2\x94\x80");
    printf("\xe2\x94\xbc");
    for (int i = 0; i < 30; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xbc");
    for (int i = 0; i < 13; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xbc");
    for (int i = 0; i < 4; i++)  printf("\xe2\x94\x80");
    printf("\xe2\x94\xbc");
    for (int i = 0; i < 10; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xa4\r\n");
}

static void menu_draw_rows(int count) {
    if (count == 0) {
        printf("  \xe2\x94\x82" KYBL_ANSI_YELLOW
               "  No networks found. Press R to scan again."
               KYBL_ANSI_RESET
               "                                       \xe2\x94\x82\r\n");
        return;
    }
    for (int i = 0; i < count && i < 18; i++) {
        const wifi_ap_t *ap = wifi_ap_get(i);
        char ssid_trunc[29];
        strncpy(ssid_trunc, ap->ssid, 28); ssid_trunc[28] = '\0';
        const char *col = wifi_rssi_color(ap->rssi);

        printf("  \xe2\x94\x82 " KYBL_ANSI_CYAN "%2d" KYBL_ANSI_RESET
               " \xe2\x94\x82 %-28s"
               " \xe2\x94\x82 %s%s" KYBL_ANSI_RESET " %4ddBm"
               " \xe2\x94\x82 %2d "
               " \xe2\x94\x82 %s \xe2\x94\x82\r\n",
               i + 1,
               ssid_trunc,
               col, wifi_rssi_bar(ap->rssi), ap->rssi,
               ap->channel,
               wifi_auth_str(ap->auth_mode));
    }
}

static void menu_draw_footer(void) {
    /* Bottom border */
    printf("  \xe2\x94\x94");
    for (int i = 0; i < 3; i++)  printf("\xe2\x94\x80");
    printf("\xe2\x94\xb4");
    for (int i = 0; i < 30; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xb4");
    for (int i = 0; i < 13; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\xb4");
    for (int i = 0; i < 4; i++)  printf("\xe2\x94\x80");
    printf("\xe2\x94\xb4");
    for (int i = 0; i < 10; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x98\r\n");

    printf("  " KYBL_ANSI_BOLD
           "[C]" KYBL_ANSI_RESET "=Connect  "
           KYBL_ANSI_BOLD "[R]" KYBL_ANSI_RESET "=Refresh  "
           KYBL_ANSI_BOLD "[Q]" KYBL_ANSI_RESET "=Quit\r\n");

    if (wifi_is_connected()) {
        char ssid[WIFI_SSID_MAX], ip[24];
        wifi_get_conn_ssid(ssid, sizeof(ssid));
        wifi_get_ip_str(ip, sizeof(ip));
        printf("  " KYBL_ANSI_GREEN "● Connected:" KYBL_ANSI_RESET
               " %s  IP: %s  RSSI: %ddBm\r\n",
               ssid, ip, wifi_get_rssi());
    }
}

/* The scan result's auth_mode is a small IE bitmask (uint8_t: 0=open, 2=WPA,
   4=WPA2, 6=WPA/WPA2 mixed), NOT one of the 32-bit CYW43_AUTH_* constants
   that cyw43_arch_wifi_connect expects. Map it here so the menu connect path
   works — that's why 'wifi connect <ssid> <pass>' succeeded (it passes auth=0
   which falls through to CYW43_AUTH_WPA2_AES_PSK) while 'wifi menu' did not. */
static uint32_t menu_scan_auth_to_cyw43(uint8_t scan_auth) {
    switch (scan_auth) {
        case 0:  return CYW43_AUTH_OPEN;
        case 2:  return CYW43_AUTH_WPA_TKIP_PSK;
        case 4:  return CYW43_AUTH_WPA2_AES_PSK;
        case 6:  return CYW43_AUTH_WPA2_MIXED_PSK;
        default: return CYW43_AUTH_WPA2_AES_PSK;   /* safest default */
    }
}

static void menu_do_connect(int count) {
    if (count == 0) { printf("No APs to connect to.\r\n"); return; }

    printf("\r\n  AP number (1-%d): ", count);
    fflush(stdout);

    char nbuf[8];
    read_plain_line(nbuf, sizeof(nbuf));
    int sel = atoi(nbuf) - 1;
    if (sel < 0 || sel >= count) {
        printf(KYBL_ANSI_RED "  Invalid selection.\r\n" KYBL_ANSI_RESET);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    const wifi_ap_t *ap = wifi_ap_get(sel);
    char pass[WIFI_PASS_MAX] = {0};

    /* scan_auth 0 = open (no password); everything else needs one */
    bool is_open = (ap->auth_mode == 0);
    if (!is_open) {
        printf("  Password for '%s': ", ap->ssid);
        fflush(stdout);
        read_masked_line(pass, sizeof(pass));
    }

    printf("  " KYBL_ANSI_YELLOW "Connecting to '%s'..." KYBL_ANSI_RESET "\r\n",
           ap->ssid);
    fflush(stdout);

    if (wifi_is_connected()) wifi_disconnect();

    /* Map scan IE bitmask → CYW43_AUTH_* constant expected by the driver. */
    uint32_t cyw43_auth = menu_scan_auth_to_cyw43(ap->auth_mode);
    int err = wifi_connect(ap->ssid, pass, cyw43_auth, WIFI_CONN_TIMEOUT);

    /* If that fails on WPA/WPA2-mixed or an odd-report AP, retry with the
       most permissive WPA2 default — much more common than TKIP-only. */
    if (err != 0 && !is_open && cyw43_auth != CYW43_AUTH_WPA2_AES_PSK) {
        printf("  " KYBL_ANSI_YELLOW "Retrying as WPA2-AES..." KYBL_ANSI_RESET "\r\n");
        fflush(stdout);
        if (wifi_is_connected()) wifi_disconnect();
        err = wifi_connect(ap->ssid, pass, CYW43_AUTH_WPA2_AES_PSK, WIFI_CONN_TIMEOUT);
    }

    if (err == 0) {
        char ip[24]; wifi_get_ip_str(ip, sizeof(ip));
        printf("  " KYBL_ANSI_GREEN "✓ Connected! IP: %s\r\n" KYBL_ANSI_RESET, ip);
        /* First successful connect? Spin up the link watchdog so we auto-
           reconnect on drops (AP reboot, RSSI dip, lease expiry, ...). */
        if (!wifi_watchdog_is_running()) wifi_watchdog_start(5000);
    } else {
        printf("  " KYBL_ANSI_RED "✗ Failed (err %d). Wrong password?\r\n"
               KYBL_ANSI_RESET, err);
    }
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/* Redraw the complete menu in one pass (header → rows → footer).
   Called once per input cycle instead of twice (once with scanning=true before
   rescan, once with scanning=false after) — that double clear was what made
   the whole screen flicker-refresh every few seconds. */
static void menu_full_redraw(int count, bool scanning) {
    menu_draw_header(scanning);
    menu_draw_rows(count);
    menu_draw_footer();
    fflush(stdout);
}

static int run_wifi_menu(void) {
    if (wifi_get_state() == WIFI_STATE_NOT_INIT) {
        printf("Initialising WiFi...\r\n"); fflush(stdout);
        if (wifi_manager_init() != 0) {
            printf(KYBL_ANSI_RED "WiFi init failed.\r\n" KYBL_ANSI_RESET);
            return KYBL_ERR_NOT_FOUND;
        }
    }

    /* Initial scan */
    menu_draw_header(true);
    printf("  Scanning for networks...\r\n");
    fflush(stdout);
    int count = wifi_scan_blocking(WIFI_SCAN_TIMEOUT);

    /* Auto-refresh interval. 6 s was way too aggressive — the screen repainted
       before the user could even read the list, much less pick a number. 30 s
       is the standard for WiFi scanners; plus 'R' forces an immediate rescan. */
    const uint32_t AUTO_REFRESH_MS = 30000;

    /* First paint with the current results (not with "scanning" flag so the
       user sees the APs, not a spinner). */
    menu_full_redraw(count, false);

    for (;;) {
        /* Poll for a key press until AUTO_REFRESH_MS elapses. 500 ms ticks so
           we yield to other tasks often without redrawing the whole screen. */
        uint32_t elapsed = 0;
        int key = PICO_ERROR_TIMEOUT;
        while (elapsed < AUTO_REFRESH_MS) {
            key = getchar_timeout_us(500000);  /* 500 ms poll */
            if (key != PICO_ERROR_TIMEOUT) break;
            elapsed += 500;
        }

        char k;
        bool timed_out = (key == PICO_ERROR_TIMEOUT);
        if (timed_out) {
            k = 'r';                           /* auto-refresh */
        } else {
            k = tolower((unsigned char)key);
        }

        if (k == 'q') break;
        if (k == 'c') {
            menu_do_connect(count);
            menu_full_redraw(count, false);    /* restore the list */
            continue;
        }
        if (k == 'r' || timed_out) {
            /* Keep old results visible — only swap the header line to show
               the scanning indicator, don't wipe the whole table. */
            printf("\033[1;1H");                /* cursor to row 1 col 1 */
            printf("\033[2K");                  /* erase just that line */
            printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN
                   "  kyblRTOS — WiFi Scanner" KYBL_ANSI_RESET
                   "  " KYBL_ANSI_YELLOW "[Scanning...]" KYBL_ANSI_RESET "\r\n");
            fflush(stdout);

            count = wifi_scan_blocking(WIFI_SCAN_TIMEOUT);
            menu_full_redraw(count, false);
            continue;
        }
        /* Ignore other keys — redraw not needed. */
    }

    /* Restore normal screen */
    printf("\033[2J\033[H");
    printf(KYBL_ANSI_BOLD KYBL_ANSI_GREEN
           "  kyblRTOS v" KYBL_VERSION_STR KYBL_ANSI_RESET
           "  — returned from wifi menu\r\n\r\n");
    return KYBL_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  WIFI COMMAND  –  subcommand dispatcher
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_wifi(int argc, char *argv[]) {
    /* No subcommand → show usage */
    if (argc < 2) {
        printf("Usage:\r\n");
        printf("  wifi menu                      — interactive AP scanner\r\n");
        printf("  wifi scan                      — quick scan, print table\r\n");
        printf("  wifi connect <ssid> [pass]     — connect to network\r\n");
        printf("  wifi disconnect                — disconnect\r\n");
        printf("  wifi status                    — show connection details\r\n");
        printf("  wifi check                     — verify connectivity (ping GW)\r\n");
        printf("  wifi watchdog <on|off|status>  — auto-reconnect supervisor\r\n");
        return KYBL_OK;
    }

    const char *sub = argv[1];

    /* ── menu ── */
    if (strcmp(sub, "menu") == 0) return run_wifi_menu();

    /* ── scan ── */
    if (strcmp(sub, "scan") == 0) {
        if (wifi_get_state() == WIFI_STATE_NOT_INIT) wifi_manager_init();
        printf("Scanning..."); fflush(stdout);
        int n = wifi_scan_blocking(WIFI_SCAN_TIMEOUT);
        printf("\r\n");
        if (n <= 0) { printf("No networks found.\r\n"); return KYBL_OK; }
        printf(KYBL_ANSI_BOLD "  %-3s %-28s %-12s %3s  %-8s\r\n" KYBL_ANSI_RESET,
               "#", "SSID", "Signal", "Ch", "Security");
        printf("  ───────────────────────────────────────────────────────\r\n");
        for (int i = 0; i < n; i++) {
            const wifi_ap_t *ap = wifi_ap_get(i);
            printf("  %-3d %-28s %s%s" KYBL_ANSI_RESET " %4ddBm  %2d  %s\r\n",
                   i + 1, ap->ssid,
                   wifi_rssi_color(ap->rssi), wifi_rssi_bar(ap->rssi), ap->rssi,
                   ap->channel, wifi_auth_str(ap->auth_mode));
        }
        printf("\r\n");
        return KYBL_OK;
    }

    /* ── connect ── */
    if (strcmp(sub, "connect") == 0) {
        if (argc < 3) { printf("Usage: wifi connect <ssid> [password]\r\n"); return KYBL_ERR_INVALID_ARG; }
        if (wifi_get_state() == WIFI_STATE_NOT_INIT) wifi_manager_init();
        const char *ssid = argv[2];
        const char *pass = (argc >= 4) ? argv[3] : "";
        printf("Connecting to '%s'... ", ssid); fflush(stdout);
        if (wifi_is_connected()) wifi_disconnect();
        int err = wifi_connect(ssid, pass, 0, WIFI_CONN_TIMEOUT);
        if (err == 0) {
            char ip[24]; wifi_get_ip_str(ip, sizeof(ip));
            printf(KYBL_ANSI_GREEN "OK\r\n" KYBL_ANSI_RESET);
            printf("  IP: %s\r\n", ip);
            /* Auto-start watchdog on first successful connect. */
            if (!wifi_watchdog_is_running()) wifi_watchdog_start(5000);
        } else {
            printf(KYBL_ANSI_RED "FAILED (err %d)\r\n" KYBL_ANSI_RESET, err);
        }
        return KYBL_OK;
    }

    /* ── watchdog ── */
    if (strcmp(sub, "watchdog") == 0) {
        if (argc < 3) {
            printf("Usage: wifi watchdog <on|off|status>\r\n");
            printf("  watchdog  : %s\r\n",
                   wifi_watchdog_is_running() ? KYBL_ANSI_GREEN "running" KYBL_ANSI_RESET
                                              : KYBL_ANSI_YELLOW "stopped" KYBL_ANSI_RESET);
            printf("  drops     : %lu\r\n", (unsigned long)wifi_watchdog_drop_count());
            printf("  reconnect : %lu\r\n", (unsigned long)wifi_watchdog_reconnect_count());
            printf("  failures  : %lu\r\n", (unsigned long)wifi_watchdog_fail_count());
            return KYBL_OK;
        }
        const char *op = argv[2];
        if (strcmp(op, "on") == 0) {
            if (wifi_watchdog_is_running()) {
                printf("Watchdog already running.\r\n");
            } else if (wifi_watchdog_start(5000) == 0) {
                printf(KYBL_ANSI_GREEN "Watchdog started (5s poll).\r\n" KYBL_ANSI_RESET);
            } else {
                printf(KYBL_ANSI_RED "Watchdog start failed.\r\n" KYBL_ANSI_RESET);
            }
        } else if (strcmp(op, "off") == 0) {
            wifi_watchdog_stop();
            printf("Watchdog stop requested.\r\n");
        } else if (strcmp(op, "status") == 0) {
            printf("  watchdog  : %s\r\n",
                   wifi_watchdog_is_running() ? KYBL_ANSI_GREEN "running" KYBL_ANSI_RESET
                                              : KYBL_ANSI_YELLOW "stopped" KYBL_ANSI_RESET);
            printf("  drops     : %lu\r\n", (unsigned long)wifi_watchdog_drop_count());
            printf("  reconnect : %lu\r\n", (unsigned long)wifi_watchdog_reconnect_count());
            printf("  failures  : %lu\r\n", (unsigned long)wifi_watchdog_fail_count());
        } else {
            printf(KYBL_ANSI_RED "Unknown watchdog op: %s\r\n" KYBL_ANSI_RESET, op);
            return KYBL_ERR_INVALID_ARG;
        }
        return KYBL_OK;
    }

    /* ── disconnect ── */
    if (strcmp(sub, "disconnect") == 0) {
        wifi_disconnect();
        printf("Disconnected.\r\n");
        return KYBL_OK;
    }

    /* ── status / check ── */
    if (strcmp(sub, "status") == 0 || strcmp(sub, "check") == 0) {
        bool check = (strcmp(sub, "check") == 0);

        if (!wifi_is_connected()) {
            printf("WiFi: " KYBL_ANSI_RED "Not connected\r\n" KYBL_ANSI_RESET);
            return KYBL_OK;
        }

        char ssid[WIFI_SSID_MAX], ip[24], gw[24], mask[24], dns[24];
        wifi_get_conn_ssid(ssid, sizeof(ssid));
        wifi_get_ip_str  (ip,   sizeof(ip));
        wifi_get_gw_str  (gw,   sizeof(gw));
        wifi_get_mask_str(mask, sizeof(mask));
        wifi_get_dns_str (dns,  sizeof(dns));
        int16_t rssi = wifi_get_rssi();

        printf(KYBL_ANSI_BOLD "\r\n  WiFi Status\r\n" KYBL_ANSI_RESET);
        printf("  ─────────────────────────────────\r\n");
        printf("  SSID     : %s\r\n", ssid);
        printf("  Signal   : %s%s" KYBL_ANSI_RESET " %d dBm\r\n",
               wifi_rssi_color(rssi), wifi_rssi_bar(rssi), rssi);
        printf("  IP       : " KYBL_ANSI_CYAN "%s" KYBL_ANSI_RESET "\r\n", ip);
        printf("  Mask     : %s\r\n", mask);
        printf("  Gateway  : %s\r\n", gw);
        printf("  DNS      : %s\r\n", dns);

        if (check) {
            /* Ping gateway to verify actual connectivity */
            printf("  Gateway  : pinging %s...", gw);
            fflush(stdout);
            /* We'll call ping internally — forward declare it */
            extern int kybl_ping_once(const char *host_str, int count, bool quiet, int32_t *avg_ms);
            int32_t avg = -1;
            int r = kybl_ping_once(gw, 3, true, &avg);
            if (r == 0 && avg >= 0)
                printf(KYBL_ANSI_GREEN " OK (%ld ms avg)\r\n" KYBL_ANSI_RESET, (long)avg);
            else
                printf(KYBL_ANSI_RED " unreachable\r\n" KYBL_ANSI_RESET);
        }
        printf("\r\n");
        return KYBL_OK;
    }

    printf(KYBL_ANSI_RED "Unknown wifi subcommand: %s\r\n" KYBL_ANSI_RESET, sub);
    return KYBL_ERR_INVALID_ARG;
}

const kybl_program_t cmd_wifi = {
    .name        = "wifi",
    .usage       = "<menu|scan|connect|disconnect|status|check>",
    .description = "WiFi management",
    .entry       = run_wifi,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  PING  –  ICMP echo using lwIP raw PCB
 * ════════════════════════════════════════════════════════════════════════════ */
#define PING_ID         0xEB1C
#define PING_DATA_LEN   32
#define PING_TIMEOUT_MS 3000

typedef struct {
    SemaphoreHandle_t sem;
    ip_addr_t         from;
    uint32_t          send_us;
    uint32_t          recv_us;
    uint8_t           ttl;
    bool              success;
    uint16_t          seq;
} ping_ctx_t;

static struct raw_pcb    *s_ping_pcb  = NULL;
static ping_ctx_t        *s_ping_ctx  = NULL;
/* Both the user shell and the wifi watchdog can call kybl_ping_once. The
   ICMP raw PCB and s_ping_ctx are shared singletons, so concurrent callers
   would corrupt each other's reply matching. Serialize entry. */
static SemaphoreHandle_t  s_ping_lock = NULL;

static uint8_t ping_recv_cb(void *arg, struct raw_pcb *pcb,
                             struct pbuf *p, const ip_addr_t *addr) {
    (void)pcb; (void)arg;
    ping_ctx_t *ctx = s_ping_ctx;
    if (!ctx || ctx->success) { pbuf_free(p); return 1; }

    if (p->tot_len < (u16_t)(PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr))) {
        pbuf_free(p); return 0;
    }

    struct ip_hdr *iph = (struct ip_hdr *)p->payload;
    u16_t iphlen = (u16_t)IPH_HL_BYTES(iph);

    if (p->tot_len < iphlen + sizeof(struct icmp_echo_hdr)) {
        pbuf_free(p); return 0;
    }

    struct icmp_echo_hdr *iecho =
        (struct icmp_echo_hdr *)((uint8_t *)p->payload + iphlen);

    if (ICMPH_TYPE(iecho) == ICMP_ER &&
        lwip_ntohs(iecho->id)    == PING_ID &&
        lwip_ntohs(iecho->seqno) == ctx->seq) {

        ctx->recv_us = time_us_32();
        ctx->ttl     = IPH_TTL(iph);
        ip_addr_copy(ctx->from, *addr);
        ctx->success = true;

        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(ctx->sem, &woken);
        pbuf_free(p);
        portYIELD_FROM_ISR(woken);
        return 1;
    }

    pbuf_free(p);
    return 0;
}

static int ping_send_one(const ip_addr_t *target, uint16_t seq) {
    size_t plen = sizeof(struct icmp_echo_hdr) + PING_DATA_LEN;
    struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)plen, PBUF_RAM);
    if (!p) return -1;

    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id     = lwip_htons(PING_ID);
    iecho->seqno  = lwip_htons(seq);

    uint8_t *data = (uint8_t *)iecho + sizeof(struct icmp_echo_hdr);
    for (size_t i = 0; i < PING_DATA_LEN; i++) data[i] = (uint8_t)('A' + i);

    iecho->chksum = inet_chksum(iecho, (u16_t)plen);

    s_ping_ctx->send_us = time_us_32();
    cyw43_arch_lwip_begin();
    err_t err = raw_sendto(s_ping_pcb, p, target);
    cyw43_arch_lwip_end();
    pbuf_free(p);
    return (err == ERR_OK) ? 0 : -1;
}

/* Non-blocking probe of the ping subsystem mutex. Used by the wifi
   watchdog so it can decide to skip a probe cycle when the shell is
   already running a ping, rather than block 10 s on the mutex (which
   would falsely look like a network failure). */
bool kybl_ping_is_busy(void) {
    if (!s_ping_lock) return false;
    if (xSemaphoreTake(s_ping_lock, 0) == pdTRUE) {
        xSemaphoreGive(s_ping_lock);
        return false;
    }
    return true;
}

/* Public: ping host_str count times. avg_ms set if quiet=true.
   Returns 0 if at least one reply received. */
int kybl_ping_once(const char *host_str, int count, bool quiet, int32_t *avg_ms) {
    ip_addr_t target;

    /* Lazy-init the serialization mutex. First call comes from the shell
       task before the watchdog starts (the watchdog is only spun up after
       a successful wifi_connect, which the user has to issue), so no race
       on creation. */
    if (!s_ping_lock) s_ping_lock = xSemaphoreCreateMutex();
    if (s_ping_lock && xSemaphoreTake(s_ping_lock, pdMS_TO_TICKS(10000)) != pdTRUE) {
        if (!quiet) printf("ping: busy\r\n");
        return -1;
    }

    int rc = -1;
    int received = 0;
    uint64_t total_us = 0;
    ping_ctx_t ctx;
    ctx.sem = NULL;

    /* Resolve hostname if needed */
    if (!ip4addr_aton(host_str, ip_2_ip4(&target))) {
        /* DNS resolve */
        extern err_t kybl_dns_resolve(const char *name, ip_addr_t *out);
        if (kybl_dns_resolve(host_str, &target) != ERR_OK) {
            if (!quiet)
                printf(KYBL_ANSI_RED "ping: cannot resolve '%s'\r\n" KYBL_ANSI_RESET, host_str);
            goto out;
        }
        IP_SET_TYPE_VAL(target, IPADDR_TYPE_V4);
    } else {
        IP_SET_TYPE_VAL(target, IPADDR_TYPE_V4);
    }

    /* Create raw PCB once */
    cyw43_arch_lwip_begin();
    if (!s_ping_pcb) {
        s_ping_pcb = raw_new(IP_PROTO_ICMP);
        if (s_ping_pcb) {
            raw_recv(s_ping_pcb, ping_recv_cb, NULL);
            raw_bind(s_ping_pcb, IP_ADDR_ANY);
        }
    }
    cyw43_arch_lwip_end();
    if (!s_ping_pcb) {
        if (!quiet) printf("ping: PCB alloc failed\r\n");
        goto out;
    }

    ctx.sem = xSemaphoreCreateBinary();
    if (!ctx.sem) goto out;

    s_ping_ctx = &ctx;

    if (!quiet)
        printf("PING %s (%s): %d bytes\r\n",
               host_str, ipaddr_ntoa(&target), PING_DATA_LEN);

    static uint16_t s_seq = 0;

    for (int i = 0; i < count; i++) {
        ctx.success = false;
        ctx.seq     = ++s_seq;

        if (ping_send_one(&target, ctx.seq) != 0) {
            if (!quiet) printf("ping: send error\r\n");
            continue;
        }

        bool got = (xSemaphoreTake(ctx.sem, pdMS_TO_TICKS(PING_TIMEOUT_MS)) == pdTRUE);

        if (got && ctx.success) {
            uint32_t us = ctx.recv_us - ctx.send_us;
            total_us += us;
            received++;
            if (!quiet)
                printf("reply from %s: icmp_seq=%u ttl=%u time=%.1f ms\r\n",
                       ipaddr_ntoa(&ctx.from), ctx.seq, ctx.ttl,
                       (float)us / 1000.0f);
        } else {
            if (!quiet)
                printf("Request timeout for icmp_seq %u\r\n", ctx.seq);
        }

        if (i < count - 1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    s_ping_ctx = NULL;

    if (!quiet && count > 1) {
        int lost = count - received;
        printf("\n--- %s ping statistics ---\r\n", host_str);
        printf("%d packets tx, %d received, %d%% loss",
               count, received, (lost * 100) / count);
        if (received)
            printf(", avg %.1f ms", (float)(total_us / received) / 1000.0f);
        printf("\r\n");
    }

    if (avg_ms && received)
        *avg_ms = (int32_t)((total_us / received) / 1000);
    else if (avg_ms)
        *avg_ms = -1;

    rc = (received > 0) ? 0 : -1;

out:
    if (ctx.sem) vSemaphoreDelete(ctx.sem);
    s_ping_ctx = NULL;
    if (s_ping_lock) xSemaphoreGive(s_ping_lock);
    return rc;
}

static int run_ping(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ping <host|ip> [count]\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    if (!require_wifi()) return KYBL_ERR_INVALID_ARG;

    int count = (argc >= 3) ? atoi(argv[2]) : 4;
    if (count < 1 || count > 20) count = 4;

    return kybl_ping_once(argv[1], count, false, NULL);
}

const kybl_program_t cmd_ping = {
    .name        = "ping",
    .usage       = "<host|ip> [count]",
    .description = "ICMP echo ping",
    .entry       = run_ping,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  DNS RESOLVE  –  shared helper + nslookup command
 * ════════════════════════════════════════════════════════════════════════════ */
static SemaphoreHandle_t s_dns_sem    = NULL;
static ip_addr_t         s_dns_result;
static bool              s_dns_ok     = false;

static void dns_found_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name; (void)arg;
    s_dns_ok = (addr != NULL);
    if (addr) ip_addr_copy(s_dns_result, *addr);
    if (s_dns_sem) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s_dns_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

err_t kybl_dns_resolve(const char *name, ip_addr_t *out) {
    if (!s_dns_sem) s_dns_sem = xSemaphoreCreateBinary();
    if (!s_dns_sem) return ERR_MEM;

    s_dns_ok = false;
    ip_addr_set_zero(&s_dns_result);

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(name, &s_dns_result, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        /* Already cached */
        if (out) ip_addr_copy(*out, s_dns_result);
        return ERR_OK;
    }
    if (err != ERR_INPROGRESS) return err;

    /* Wait for async result */
    if (xSemaphoreTake(s_dns_sem, pdMS_TO_TICKS(5000)) != pdTRUE) return ERR_TIMEOUT;

    if (!s_dns_ok) return ERR_VAL;
    if (out) ip_addr_copy(*out, s_dns_result);
    return ERR_OK;
}

static int run_nslookup(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: nslookup <hostname>\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    if (!require_wifi()) return KYBL_ERR_INVALID_ARG;

    char dns_s[24]; wifi_get_dns_str(dns_s, sizeof(dns_s));
    printf("Server: %s\r\n", dns_s);
    printf("Resolving '%s'... ", argv[1]); fflush(stdout);

    ip_addr_t result;
    err_t err = kybl_dns_resolve(argv[1], &result);

    if (err == ERR_OK) {
        printf(KYBL_ANSI_GREEN "OK\r\n" KYBL_ANSI_RESET);
        printf("  " KYBL_ANSI_CYAN "%s" KYBL_ANSI_RESET " → %s\r\n",
               argv[1], ipaddr_ntoa(&result));
    } else {
        printf(KYBL_ANSI_RED "FAILED" KYBL_ANSI_RESET " (err %d)\r\n", err);
    }
    return KYBL_OK;
}

const kybl_program_t cmd_nslookup = {
    .name        = "nslookup",
    .usage       = "<hostname>",
    .description = "DNS hostname lookup",
    .entry       = run_nslookup,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  NETSTAT  –  network interface + connection info
 * ════════════════════════════════════════════════════════════════════════════ */
static int run_netstat(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf(KYBL_ANSI_BOLD "\r\n  Network Status\r\n" KYBL_ANSI_RESET);
    printf("  ──────────────────────────────────────────\r\n");

    if (!wifi_is_connected()) {
        printf("  WiFi    : " KYBL_ANSI_RED "Not connected\r\n" KYBL_ANSI_RESET);
        printf("\r\n");
        return KYBL_OK;
    }

    char ssid[WIFI_SSID_MAX], ip[24], gw[24], mask[24];
    char dns0[24], dns1[24];
    wifi_get_conn_ssid(ssid, sizeof(ssid));
    wifi_get_ip_str   (ip,   sizeof(ip));
    wifi_get_gw_str   (gw,   sizeof(gw));
    wifi_get_mask_str (mask, sizeof(mask));
    wifi_get_dns_str  (dns0, sizeof(dns0));
    int16_t rssi = wifi_get_rssi();

    /* Second DNS server */
    cyw43_arch_lwip_begin();
    const ip_addr_t *dns1_addr = dns_getserver(1);
    snprintf(dns1, sizeof(dns1), "%s",
             (dns1_addr && !ip_addr_isany(dns1_addr))
             ? ipaddr_ntoa(dns1_addr) : "N/A");
    cyw43_arch_lwip_end();

    /* BSSID */
    uint8_t bssid[6] = {0};
    cyw43_wifi_get_bssid(&cyw43_state, bssid);

    printf("  WiFi    : " KYBL_ANSI_GREEN "● Connected\r\n" KYBL_ANSI_RESET);
    printf("  SSID    : %s\r\n", ssid);
    printf("  BSSID   : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    printf("  Signal  : %s%s" KYBL_ANSI_RESET " %d dBm\r\n",
           wifi_rssi_color(rssi), wifi_rssi_bar(rssi), rssi);
    printf("  ──────────────────────────────────────────\r\n");
    printf("  IP      : " KYBL_ANSI_CYAN "%-16s" KYBL_ANSI_RESET
           "  Mask: %s\r\n", ip, mask);
    printf("  Gateway : %-16s\r\n", gw);
    printf("  DNS[0]  : %-16s\r\n", dns0);
    printf("  DNS[1]  : %-16s\r\n", dns1);
    printf("  ──────────────────────────────────────────\r\n");

    /* Heap snapshot */
    printf("  Heap    : %u free / %u total bytes\r\n",
           (unsigned)xPortGetFreeHeapSize(), configTOTAL_HEAP_SIZE);
    printf("  Tasks   : %u active\r\n\r\n", (unsigned)uxTaskGetNumberOfTasks());
    return KYBL_OK;
}

const kybl_program_t cmd_netstat = {
    .name        = "netstat",
    .usage       = "",
    .description = "Show network interface and connection info",
    .entry       = run_netstat,
};

/* ════════════════════════════════════════════════════════════════════════════
 *  TRACEROUTE  –  ICMP with incrementing TTL
 * ════════════════════════════════════════════════════════════════════════════ */
#define TRACE_MAX_HOPS  20
#define TRACE_ID        0xEB1D

typedef struct {
    SemaphoreHandle_t sem;
    ip_addr_t         from;
    uint32_t          send_us;
    uint32_t          recv_us;
    bool              success;
    bool              is_dest;
    uint16_t          seq;
} trace_hop_t;

static struct raw_pcb *s_trace_pcb = NULL;
static trace_hop_t    *s_trace_ctx = NULL;

static uint8_t trace_recv_cb(void *arg, struct raw_pcb *pcb,
                              struct pbuf *p, const ip_addr_t *addr) {
    (void)arg; (void)pcb;
    trace_hop_t *ctx = s_trace_ctx;
    if (!ctx || ctx->success) { pbuf_free(p); return 1; }

    if (p->tot_len < (u16_t)PBUF_IP_HLEN) { pbuf_free(p); return 0; }

    struct ip_hdr *outer_ip = (struct ip_hdr *)p->payload;
    u16_t outer_iphlen = (u16_t)IPH_HL_BYTES(outer_ip);

    if (p->tot_len < outer_iphlen + sizeof(struct icmp_echo_hdr)) {
        pbuf_free(p); return 0;
    }

    struct icmp_echo_hdr *icmph =
        (struct icmp_echo_hdr *)((uint8_t *)p->payload + outer_iphlen);
    u8_t type = ICMPH_TYPE(icmph);

    /* ICMP Time Exceeded (type 11) — intermediate hop */
    if (type == 11) {
        /* Inner IP header starts after outer IP + ICMP header */
        uint8_t *inner = (uint8_t *)icmph + sizeof(struct icmp_echo_hdr);
        if (p->tot_len < outer_iphlen + sizeof(struct icmp_echo_hdr)
                        + PBUF_IP_HLEN + 8) {
            pbuf_free(p); return 0;
        }
        struct ip_hdr *inner_ip = (struct ip_hdr *)inner;
        u16_t inner_iphlen = (u16_t)IPH_HL_BYTES(inner_ip);
        struct icmp_echo_hdr *inner_icmp =
            (struct icmp_echo_hdr *)(inner + inner_iphlen);

        if (lwip_ntohs(inner_icmp->id)    == TRACE_ID &&
            lwip_ntohs(inner_icmp->seqno) == ctx->seq) {
            ctx->recv_us  = time_us_32();
            ctx->is_dest  = false;
            ctx->success  = true;
            ip_addr_copy(ctx->from, *addr);
            BaseType_t woken = pdFALSE;
            xSemaphoreGiveFromISR(ctx->sem, &woken);
            pbuf_free(p);
            portYIELD_FROM_ISR(woken);
            return 1;
        }
    }

    /* ICMP Echo Reply (type 0) — destination reached */
    if (type == ICMP_ER &&
        lwip_ntohs(icmph->id)    == TRACE_ID &&
        lwip_ntohs(icmph->seqno) == ctx->seq) {
        ctx->recv_us = time_us_32();
        ctx->is_dest = true;
        ctx->success = true;
        ip_addr_copy(ctx->from, *addr);
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(ctx->sem, &woken);
        pbuf_free(p);
        portYIELD_FROM_ISR(woken);
        return 1;
    }

    pbuf_free(p); return 0;
}

static int run_traceroute(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: traceroute <host|ip> [max_hops]\r\n");
        return KYBL_ERR_INVALID_ARG;
    }
    if (!require_wifi()) return KYBL_ERR_INVALID_ARG;

    int max_hops = (argc >= 3) ? atoi(argv[2]) : TRACE_MAX_HOPS;
    if (max_hops < 1 || max_hops > 30) max_hops = TRACE_MAX_HOPS;

    ip_addr_t target;
    if (!ip4addr_aton(argv[1], ip_2_ip4(&target))) {
        printf("Resolving '%s'... ", argv[1]); fflush(stdout);
        if (kybl_dns_resolve(argv[1], &target) != ERR_OK) {
            printf(KYBL_ANSI_RED "failed\r\n" KYBL_ANSI_RESET);
            return KYBL_ERR_INVALID_ARG;
        }
        IP_SET_TYPE_VAL(target, IPADDR_TYPE_V4);
        printf("%s\r\n", ipaddr_ntoa(&target));
    } else {
        IP_SET_TYPE_VAL(target, IPADDR_TYPE_V4);
    }

    /* Create raw ICMP PCB */
    cyw43_arch_lwip_begin();
    if (!s_trace_pcb) {
        s_trace_pcb = raw_new(IP_PROTO_ICMP);
        if (s_trace_pcb) {
            raw_recv(s_trace_pcb, trace_recv_cb, NULL);
            raw_bind(s_trace_pcb, IP_ADDR_ANY);
        }
    }
    cyw43_arch_lwip_end();
    if (!s_trace_pcb) { printf("traceroute: PCB alloc failed\r\n"); return -1; }

    printf("traceroute to %s (%s), %d hops max\r\n",
           argv[1], ipaddr_ntoa(&target), max_hops);

    trace_hop_t ctx;
    ctx.sem = xSemaphoreCreateBinary();
    if (!ctx.sem) return -1;
    s_trace_ctx = &ctx;

    static uint16_t t_seq = 0;

    for (int hop = 1; hop <= max_hops; hop++) {
        ctx.success = false;
        ctx.seq     = ++t_seq;

        /* Set TTL on the PCB */
        cyw43_arch_lwip_begin();
        s_trace_pcb->ttl = (u8_t)hop;

        /* Build ICMP echo packet */
        size_t plen = sizeof(struct icmp_echo_hdr) + 32;
        struct pbuf *pb = pbuf_alloc(PBUF_IP, (u16_t)plen, PBUF_RAM);
        err_t send_err = ERR_MEM;
        if (pb) {
            struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)pb->payload;
            ICMPH_TYPE_SET(iecho, ICMP_ECHO);
            ICMPH_CODE_SET(iecho, 0);
            iecho->chksum = 0;
            iecho->id     = lwip_htons(TRACE_ID);
            iecho->seqno  = lwip_htons(ctx.seq);
            uint8_t *d    = (uint8_t *)iecho + sizeof(struct icmp_echo_hdr);
            for (size_t i = 0; i < 32; i++) d[i] = (uint8_t)i;
            iecho->chksum = inet_chksum(iecho, (u16_t)plen);
            ctx.send_us   = time_us_32();
            send_err      = raw_sendto(s_trace_pcb, pb, &target);
            pbuf_free(pb);
        }
        cyw43_arch_lwip_end();

        printf("  %2d  ", hop);
        fflush(stdout);

        if (send_err != ERR_OK) {
            printf("send error\r\n");
            continue;
        }

        bool got = (xSemaphoreTake(ctx.sem, pdMS_TO_TICKS(3000)) == pdTRUE);

        if (got && ctx.success) {
            float ms = (float)(ctx.recv_us - ctx.send_us) / 1000.0f;
            printf(KYBL_ANSI_CYAN "%-18s" KYBL_ANSI_RESET "  %.2f ms\r\n",
                   ipaddr_ntoa(&ctx.from), ms);
            if (ctx.is_dest) {
                printf("  Destination reached.\r\n");
                break;
            }
        } else {
            printf("* * *  (timeout)\r\n");
        }
    }

    s_trace_ctx = NULL;
    vSemaphoreDelete(ctx.sem);
    printf("\r\n");
    return KYBL_OK;
}

const kybl_program_t cmd_traceroute = {
    .name        = "traceroute",
    .usage       = "<host|ip> [hops]",
    .description = "ICMP traceroute (TTL-based hop tracing)",
    .entry       = run_traceroute,
};

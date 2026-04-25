#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define WIFI_MAX_APS        24
#define WIFI_SSID_MAX       33
#define WIFI_PASS_MAX       64
#define WIFI_SCAN_TIMEOUT   8000    /* ms */
#define WIFI_CONN_TIMEOUT   15000   /* ms */

/* ── Scanned AP descriptor ────────────────────────────────────────────────── */
typedef struct {
    char     ssid[WIFI_SSID_MAX];
    uint8_t  bssid[6];
    int16_t  rssi;
    uint8_t  channel;
    uint32_t auth_mode;
} wifi_ap_t;

/* ── Driver state ─────────────────────────────────────────────────────────── */
typedef enum {
    WIFI_STATE_NOT_INIT  = 0,
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */
int  wifi_manager_init(void);               /* call once before scheduler     */

/* ── Scan ─────────────────────────────────────────────────────────────────── */
int              wifi_scan_blocking(uint32_t timeout_ms);
int              wifi_ap_count(void);
const wifi_ap_t *wifi_ap_get(int idx);

/* ── Connect / disconnect ─────────────────────────────────────────────────── */
int  wifi_connect(const char *ssid, const char *pass, uint32_t auth, uint32_t timeout_ms);
void wifi_disconnect(void);
int  wifi_reconnect(void);       /* reuse last-known creds */

/* ── Watchdog (auto-reconnect) ────────────────────────────────────────────── */
int  wifi_watchdog_start(uint32_t poll_ms);
void wifi_watchdog_stop(void);
bool wifi_watchdog_is_running(void);
uint32_t wifi_watchdog_drop_count(void);       /* link drops observed */
uint32_t wifi_watchdog_reconnect_count(void);  /* successful reconnects */
uint32_t wifi_watchdog_fail_count(void);       /* failed reconnect attempts */

/* ── Status ───────────────────────────────────────────────────────────────── */
wifi_state_t wifi_get_state(void);
bool         wifi_is_connected(void);
void         wifi_get_ip_str  (char *buf, size_t len);
void         wifi_get_gw_str  (char *buf, size_t len);
void         wifi_get_mask_str(char *buf, size_t len);
void         wifi_get_dns_str (char *buf, size_t len);
void         wifi_get_conn_ssid(char *buf, size_t len);
int16_t      wifi_get_rssi(void);

/* ── Display helpers ──────────────────────────────────────────────────────── */
const char *wifi_auth_str(uint32_t auth_mode);
const char *wifi_rssi_bar(int16_t rssi);       /* 4-char UTF-8 bar string    */
const char *wifi_rssi_color(int16_t rssi);     /* ANSI colour prefix          */

#endif /* WIFI_MANAGER_H */

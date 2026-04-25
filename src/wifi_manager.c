#include "wifi_manager.h"
#include "kyblrtos.h"

#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* ── Internal state ───────────────────────────────────────────────────────── */
static wifi_state_t s_state     = WIFI_STATE_NOT_INIT;
static wifi_ap_t    s_aps[WIFI_MAX_APS];
static int          s_ap_count  = 0;
static volatile bool s_scan_done = false;
static char         s_conn_ssid[WIFI_SSID_MAX] = {0};

/* Last-known good credentials, kept so the watchdog can silently reconnect
   when the AP drops us (RSSI dip, AP reboot, channel change, lease expiry). */
static char     s_last_ssid[WIFI_SSID_MAX] = {0};
static char     s_last_pass[WIFI_PASS_MAX] = {0};
static uint32_t s_last_auth = 0;
static bool     s_has_last  = false;
/* Set true whenever the user explicitly calls wifi_disconnect() so the
   watchdog knows NOT to reconnect (user intent beats auto-reconnect). */
static volatile bool s_user_disconnected = false;

/* Watchdog state */
static TaskHandle_t s_watchdog_task = NULL;
static volatile bool s_watchdog_stop = false;
static uint32_t s_wd_poll_ms    = 5000;
static uint32_t s_wd_drops      = 0;
static uint32_t s_wd_reconnects = 0;
static uint32_t s_wd_fails      = 0;

/* Consecutive watchdog samples where link reports JOIN+IP but RSSI is
   implausible (0 / < -100 dBm). After this many in a row we assume a
   silent de-auth and tear the link down. ~15 seconds at the default
   5 s poll. */
#define WIFI_WD_BAD_RSSI_LIMIT 3
static int s_wd_bad_rssi_samples = 0;

/* Active L3 liveness probe — every poll cycle ping the gateway and verify
   the round-trip works. The chip's join_state and even RSSI can both lie
   for a long time after a silent de-auth; the only ironclad test is
   actually putting a packet on the wire and getting one back.

   2 consecutive failures → force reauth. With the default 5 s poll that
   gives us ~10 s detection latency in the worst case. We deliberately
   skip cycles where the shell is using the ping subsystem (kybl_ping_is_busy)
   rather than block on its mutex — blocking would both delay the watchdog
   and inflate the failure count when the user is just running `ping`. */
#define WIFI_WD_L3_FAIL_LIMIT  2
static int s_wd_l3_fails = 0;

/* Forward decls — defined in net_commands.c. We deliberately declare them
   here rather than pulling a header in to keep the wifi/net layers loosely
   coupled (net_commands depends on wifi_manager, not the other way around).
   kybl_ping_once takes its own internal mutex; kybl_ping_is_busy lets us
   try-acquire it without blocking. */
extern int  kybl_ping_once   (const char *host_str, int count, bool quiet, int32_t *avg_ms);
extern bool kybl_ping_is_busy(void);

/* ── Scan callback (called from CYW43 IRQ context) ───────────────────────── */
static int scan_result_cb(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!r) {
        /* NULL result signals scan complete */
        s_scan_done = true;
        return 0;
    }
    if (s_ap_count >= WIFI_MAX_APS || r->ssid_len == 0) return 0;

    /* Deduplicate by SSID — keep strongest signal */
    for (int i = 0; i < s_ap_count; i++) {
        if (strncmp(s_aps[i].ssid, (const char *)r->ssid, r->ssid_len) == 0
                && strlen(s_aps[i].ssid) == r->ssid_len) {
            if (r->rssi > s_aps[i].rssi) {
                s_aps[i].rssi    = r->rssi;
                s_aps[i].channel = r->channel;
            }
            return 0;
        }
    }

    wifi_ap_t *ap = &s_aps[s_ap_count++];
    memset(ap, 0, sizeof(*ap));
    memcpy(ap->ssid, r->ssid, r->ssid_len);
    ap->ssid[r->ssid_len] = '\0';
    memcpy(ap->bssid, r->bssid, 6);
    ap->rssi      = r->rssi;
    ap->channel   = r->channel;
    ap->auth_mode = r->auth_mode;
    return 0;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */
int wifi_manager_init(void) {
    if (s_state != WIFI_STATE_NOT_INIT) return 0;
    if (cyw43_arch_init() != 0) return -1;
    cyw43_arch_enable_sta_mode();
    /* Disable WiFi power-save. The driver default is CYW43_DEFAULT_PM
       == CYW43_PERFORMANCE_PM, which is PM2 with a 200 ms tail-sleep
       (see pico-sdk/lib/cyw43-driver/src/cyw43.h). In PM2 the radio
       sleeps deep enough between beacons that some APs de-authenticate
       us as "idle"; the chip can take many seconds — sometimes minutes
       — to notice the de-auth, during which link_status keeps returning
       LINK_JOIN even though no packet survives a round-trip. The Pico
       is mains-powered for kyblRTOS, so the ~25 mA cost is irrelevant
       and the link stays solid. */
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    s_state = WIFI_STATE_IDLE;
    return 0;
}

/* ── Scan ─────────────────────────────────────────────────────────────────── */
int wifi_scan_blocking(uint32_t timeout_ms) {
    if (s_state == WIFI_STATE_NOT_INIT) return -1;

    s_ap_count  = 0;
    s_scan_done = false;
    s_state     = WIFI_STATE_SCANNING;

    cyw43_wifi_scan_options_t opts = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_result_cb);
    if (err) {
        s_state = wifi_is_connected() ? WIFI_STATE_CONNECTED : WIFI_STATE_IDLE;
        return -1;
    }

    uint32_t elapsed = 0;
    while (!s_scan_done && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }

    /* Small settle time for any trailing results */
    vTaskDelay(pdMS_TO_TICKS(100));
    s_state = wifi_is_connected() ? WIFI_STATE_CONNECTED : WIFI_STATE_IDLE;
    return s_ap_count;
}

int wifi_ap_count(void) { return s_ap_count; }

const wifi_ap_t *wifi_ap_get(int idx) {
    if (idx < 0 || idx >= s_ap_count) return NULL;
    return &s_aps[idx];
}

/* ── Connect / disconnect ─────────────────────────────────────────────────── */
int wifi_connect(const char *ssid, const char *pass, uint32_t auth, uint32_t timeout_ms) {
    if (s_state == WIFI_STATE_NOT_INIT) return -1;
    s_state = WIFI_STATE_CONNECTING;

    /* Open network: pass = NULL, auth = CYW43_AUTH_OPEN */
    const char *pw    = (pass && pass[0]) ? pass : NULL;
    uint32_t    amode = pw ? (auth ? auth : CYW43_AUTH_WPA2_AES_PSK) : CYW43_AUTH_OPEN;

    int err = cyw43_arch_wifi_connect_timeout_ms(ssid, pw, amode, timeout_ms);
    if (err == 0) {
        strncpy(s_conn_ssid, ssid, WIFI_SSID_MAX - 1);
        s_conn_ssid[WIFI_SSID_MAX - 1] = '\0';

        /* Remember these creds so the watchdog can silently re-auth on drop. */
        strncpy(s_last_ssid, ssid, WIFI_SSID_MAX - 1);
        s_last_ssid[WIFI_SSID_MAX - 1] = '\0';
        if (pw) {
            strncpy(s_last_pass, pw, WIFI_PASS_MAX - 1);
            s_last_pass[WIFI_PASS_MAX - 1] = '\0';
        } else {
            s_last_pass[0] = '\0';
        }
        s_last_auth = amode;
        s_has_last  = true;
        s_user_disconnected = false;

        /* Re-assert no-powersave; some firmware paths can reset PM after
           an association. Cheap and idempotent. */
        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
        s_wd_bad_rssi_samples = 0;
        s_wd_l3_fails         = 0;

        s_state = WIFI_STATE_CONNECTED;
        return 0;
    }
    s_state = WIFI_STATE_FAILED;
    return err;
}

void wifi_disconnect(void) {
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    s_conn_ssid[0] = '\0';
    s_state = WIFI_STATE_IDLE;
    /* User asked to leave the network — block the watchdog from dragging us
       back on until the next explicit wifi_connect(). */
    s_user_disconnected = true;
}

int wifi_reconnect(void) {
    if (!s_has_last) return -1;
    return wifi_connect(s_last_ssid,
                        s_last_pass[0] ? s_last_pass : NULL,
                        s_last_auth,
                        WIFI_CONN_TIMEOUT);
}

/* ── Watchdog ─────────────────────────────────────────────────────────────── */
static void wifi_watchdog_task(void *arg) {
    (void)arg;
    /* grace period after first connect — don't flap immediately */
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (!s_watchdog_stop) {
        vTaskDelay(pdMS_TO_TICKS(s_wd_poll_ms));
        if (s_watchdog_stop) break;

        /* Respect explicit user disconnect. */
        if (s_user_disconnected) continue;

        /* If we've never had a successful connect, nothing to do. */
        if (!s_has_last) continue;

        bool need_reconnect = false;
        const char *reason = NULL;

        /* ── 1. Cheap link-layer check ─────────────────────────────────
           cyw43_wifi_link_status returns DOWN/JOIN/FAIL/NONET/BADAUTH for
           the STA interface (NOIP and UP are only reported by the
           tcpip-aware superset). Combine "JOIN" with an IP-presence
           check to catch DHCP-lease expiry — an AP that still sees us
           but won't renew our lease leaves us effectively netless. */
        int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        bool associated = (link == CYW43_LINK_JOIN);
        bool has_ip = false;
        {
            const ip4_addr_t *ip = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
            has_ip = (ip && ip->addr != 0);
        }
        if (!associated) { need_reconnect = true; reason = "link down"; }
        else if (!has_ip)  { need_reconnect = true; reason = "no DHCP lease"; }

        /* ── 2. RSSI plausibility ──────────────────────────────────────
           cyw43_wifi_get_rssi IOCTLs the chip and returns ~0 once the
           radio has stopped hearing the AP. Anything weaker than
           -100 dBm is also implausible — the BCM43439 needs >-95 dBm
           to decode a beacon. Tolerate transient bad samples (radio
           doing other things) but escalate after several in a row. */
        if (!need_reconnect) {
            int32_t rssi_raw = 0;
            int rssi_err = cyw43_wifi_get_rssi(&cyw43_state, &rssi_raw);
            bool rssi_ok = (rssi_err == 0 && rssi_raw < 0 && rssi_raw > -100);
            if (rssi_ok) {
                s_wd_bad_rssi_samples = 0;
            } else {
                s_wd_bad_rssi_samples++;
                if (s_wd_bad_rssi_samples >= WIFI_WD_BAD_RSSI_LIMIT) {
                    s_wd_bad_rssi_samples = 0;
                    need_reconnect = true;
                    reason = "RSSI lost";
                }
            }
        }

        /* ── 3. Authoritative L3 round-trip probe ──────────────────────
           Every poll, ping the gateway. The chip's join_state and RSSI
           can both keep returning fine values for many minutes after
           the AP silently stops talking to us — that's the "ping fails
           but wifi says connected" failure mode. The only ironclad check
           is putting a real packet on the wire and getting one back.

           If the shell is mid-`ping` we skip this cycle rather than
           block on the ping mutex — blocking would (a) delay the
           watchdog by up to 10 s and (b) inflate the failure count
           even though the user IS verifying connectivity their own
           way. */
        if (!need_reconnect && !kybl_ping_is_busy()) {
            char gw_str[24] = {0};
            wifi_get_gw_str(gw_str, sizeof(gw_str));
            if (gw_str[0] && strcmp(gw_str, "0.0.0.0") != 0) {
                int32_t avg = -1;
                int pr = kybl_ping_once(gw_str, 1, true, &avg);
                if (pr == 0) {
                    s_wd_l3_fails = 0;
                } else {
                    s_wd_l3_fails++;
                    if (s_wd_l3_fails >= WIFI_WD_L3_FAIL_LIMIT) {
                        s_wd_l3_fails = 0;
                        need_reconnect = true;
                        reason = "gateway unreachable";
                    }
                }
            }
        }

        if (!need_reconnect) continue;

        /* Make our action visible — silent watchdog reconnects make it
           impossible to tell whether wifi is actually being supervised. */
        printf("\r\n" KYBL_ANSI_YELLOW "[wifi-wd]" KYBL_ANSI_RESET
               " %s — reconnecting...\r\n",
               reason ? reason : "unknown reason");

        /* We were connected but aren't now → try to bring it back.
           Exponential-ish backoff: 1, 2, 4, 8, 16, 30s capped. */
        s_wd_drops++;
        uint32_t backoff_ms = 1000;
        bool reauth_ok = false;
        for (int attempt = 0; attempt < 8 && !s_watchdog_stop && !s_user_disconnected; attempt++) {
            /* leave cleanly before retrying so the driver resets its state */
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            vTaskDelay(pdMS_TO_TICKS(500));

            int err = cyw43_arch_wifi_connect_timeout_ms(
                s_last_ssid,
                s_last_pass[0] ? s_last_pass : NULL,
                s_last_auth,
                WIFI_CONN_TIMEOUT);

            if (err == 0) {
                s_state = WIFI_STATE_CONNECTED;
                strncpy(s_conn_ssid, s_last_ssid, WIFI_SSID_MAX - 1);
                s_conn_ssid[WIFI_SSID_MAX - 1] = '\0';
                /* Make sure power-save stays off across reassociations. */
                cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
                s_wd_bad_rssi_samples = 0;
                s_wd_l3_fails         = 0;
                s_wd_reconnects++;
                reauth_ok = true;
                break;
            }

            s_wd_fails++;
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            if (backoff_ms < 30000) backoff_ms *= 2;
            if (backoff_ms > 30000) backoff_ms = 30000;
        }
        if (reauth_ok) {
            char ip[24]; wifi_get_ip_str(ip, sizeof(ip));
            printf(KYBL_ANSI_GREEN "[wifi-wd]" KYBL_ANSI_RESET
                   " reconnected, IP %s\r\n", ip);
        } else if (!s_user_disconnected && !s_watchdog_stop) {
            printf(KYBL_ANSI_RED "[wifi-wd]" KYBL_ANSI_RESET
                   " gave up after 8 attempts — try 'wifi connect' manually\r\n");
        }
    }

    s_watchdog_task = NULL;
    vTaskDelete(NULL);
}

int wifi_watchdog_start(uint32_t poll_ms) {
    if (s_watchdog_task) return 0;                    /* already running */
    if (poll_ms < 1000) poll_ms = 1000;
    if (poll_ms > 60000) poll_ms = 60000;
    s_wd_poll_ms    = poll_ms;
    s_watchdog_stop = false;

    BaseType_t ok = xTaskCreate(wifi_watchdog_task, "wifi_wd",
                                1024, NULL, 2, &s_watchdog_task);
    return (ok == pdPASS) ? 0 : -1;
}

void wifi_watchdog_stop(void) {
    if (!s_watchdog_task) return;
    s_watchdog_stop = true;
    /* The task sees the flag on its next poll tick and self-deletes. */
}

bool wifi_watchdog_is_running(void) { return s_watchdog_task != NULL; }
uint32_t wifi_watchdog_drop_count(void)      { return s_wd_drops; }
uint32_t wifi_watchdog_reconnect_count(void) { return s_wd_reconnects; }
uint32_t wifi_watchdog_fail_count(void)      { return s_wd_fails; }

/* ── Status ───────────────────────────────────────────────────────────────── */
wifi_state_t wifi_get_state(void) { return s_state; }

bool wifi_is_connected(void) {
    return cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN;
}

static struct netif *get_sta_netif(void) {
    return &cyw43_state.netif[CYW43_ITF_STA];
}

void wifi_get_ip_str(char *buf, size_t len) {
    snprintf(buf, len, "%s", ip4addr_ntoa(netif_ip4_addr(get_sta_netif())));
}
void wifi_get_gw_str(char *buf, size_t len) {
    snprintf(buf, len, "%s", ip4addr_ntoa(netif_ip4_gw(get_sta_netif())));
}
void wifi_get_mask_str(char *buf, size_t len) {
    snprintf(buf, len, "%s", ip4addr_ntoa(netif_ip4_netmask(get_sta_netif())));
}
void wifi_get_dns_str(char *buf, size_t len) {
    const ip_addr_t *dns = dns_getserver(0);
    snprintf(buf, len, "%s", (dns && !ip_addr_isany(dns)) ? ipaddr_ntoa(dns) : "N/A");
}
void wifi_get_conn_ssid(char *buf, size_t len) {
    strncpy(buf, s_conn_ssid, len - 1);
    buf[len - 1] = '\0';
}
int16_t wifi_get_rssi(void) {
    int32_t r = 0;
    cyw43_wifi_get_rssi(&cyw43_state, &r);
    return (int16_t)r;
}

/* ── Display helpers ──────────────────────────────────────────────────────── */
const char *wifi_auth_str(uint32_t auth) {
    switch (auth) {
        case CYW43_AUTH_OPEN:           return "Open    ";
        case CYW43_AUTH_WPA_TKIP_PSK:   return "WPA     ";
        case CYW43_AUTH_WPA2_AES_PSK:   return "WPA2    ";
        case CYW43_AUTH_WPA2_MIXED_PSK: return "WPA/WPA2";
        default:                         return "Unknown ";
    }
}

const char *wifi_rssi_bar(int16_t rssi) {
    if (rssi >= -50) return "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88"; /* ████ */
    if (rssi >= -60) return "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x91"; /* ███░ */
    if (rssi >= -70) return "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x91\xe2\x96\x91"; /* ██░░ */
    if (rssi >= -80) return "\xe2\x96\x88\xe2\x96\x91\xe2\x96\x91\xe2\x96\x91"; /* █░░░ */
    return                   "\xe2\x96\x91\xe2\x96\x91\xe2\x96\x91\xe2\x96\x91"; /* ░░░░ */
}

const char *wifi_rssi_color(int16_t rssi) {
    if (rssi >= -60) return KYBL_ANSI_GREEN;
    if (rssi >= -70) return KYBL_ANSI_YELLOW;
    return                  KYBL_ANSI_RED;
}

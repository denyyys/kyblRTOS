#include "sd_spi.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ── Pin map (matches the board wiring) ───────────────────────────────────── */
#define SD_SPI_PORT     spi0
#define SD_PIN_MISO     4   /* GP4  → SD DO  */
#define SD_PIN_CS       5   /* GP5  → SD CS  (software-driven) */
#define SD_PIN_SCK      2   /* GP2  → SD CLK */
#define SD_PIN_MOSI     3   /* GP3  → SD DI  */
#define SD_PIN_ACT_LED  1   /* GP1  → red activity LED (active high) */

/* ── Activity LED helpers ─────────────────────────────────────────────────────
   The LED is asserted ONLY during real SPI bus traffic — every public op
   (init, read, write, sync) brackets its work with sd_led_on()/sd_led_off().
   The kyblFS mutex serialises everything above us, so no nesting is possible
   here and a plain pin write is the cheapest correct primitive. */
static inline void sd_led_on (void) { gpio_put(SD_PIN_ACT_LED, 1); }
static inline void sd_led_off(void) { gpio_put(SD_PIN_ACT_LED, 0); }

static void sd_led_init(void) {
    gpio_init(SD_PIN_ACT_LED);
    gpio_set_dir(SD_PIN_ACT_LED, GPIO_OUT);
    gpio_put(SD_PIN_ACT_LED, 0);
}

/* ── SPI clock speeds ─────────────────────────────────────────────────────── */
#define SD_SPI_SLOW_HZ   400000      /* 400 kHz during init                   */
#define SD_SPI_FAST_HZ   12500000    /* 12.5 MHz after init                   */

/* ── SD command set (SPI mode) ────────────────────────────────────────────── */
#define CMD0    0   /* GO_IDLE_STATE           */
#define CMD1    1   /* SEND_OP_COND  (MMC)     */
#define CMD8    8   /* SEND_IF_COND            */
#define CMD9    9   /* SEND_CSD                */
#define CMD10   10  /* SEND_CID                */
#define CMD12   12  /* STOP_TRANSMISSION       */
#define CMD13   13  /* SEND_STATUS             */
#define CMD16   16  /* SET_BLOCKLEN            */
#define CMD17   17  /* READ_SINGLE_BLOCK       */
#define CMD18   18  /* READ_MULTIPLE_BLOCK     */
#define CMD23   23  /* SET_BLOCK_COUNT (MMC)   */
#define CMD24   24  /* WRITE_BLOCK             */
#define CMD25   25  /* WRITE_MULTIPLE_BLOCK    */
#define CMD55   55  /* APP_CMD                 */
#define CMD58   58  /* READ_OCR                */
#define ACMD23  23  /* SET_WR_BLK_ERASE_COUNT  */
#define ACMD41  41  /* SD_SEND_OP_COND         */

/* R1 response bits */
#define R1_IDLE           0x01
#define R1_ERASE_RESET    0x02
#define R1_ILLEGAL_CMD    0x04
#define R1_COM_CRC_ERR    0x08
#define R1_ERASE_SEQ_ERR  0x10
#define R1_ADDR_ERR       0x20
#define R1_PARAM_ERR      0x40
/* bit 7 always 0 */

/* Data tokens */
#define DATA_TOKEN_SINGLE_READ    0xFE
#define DATA_TOKEN_SINGLE_WRITE   0xFE
#define DATA_TOKEN_MULTI_WRITE    0xFC
#define DATA_TOKEN_STOP_TRAN      0xFD

/* ── Internal state ───────────────────────────────────────────────────────── */
static bool            s_initialized = false;
static sd_card_type_t  s_card_type   = SD_CARD_UNKNOWN;
static uint32_t        s_sector_cnt  = 0;

/* ── CS / SPI helpers ─────────────────────────────────────────────────────── */
static inline void cs_low (void) { asm volatile("nop \n nop \n nop"); gpio_put(SD_PIN_CS, 0); asm volatile("nop \n nop \n nop"); }
static inline void cs_high(void) { asm volatile("nop \n nop \n nop"); gpio_put(SD_PIN_CS, 1); asm volatile("nop \n nop \n nop"); }

static uint8_t spi_xchg(uint8_t b) {
    uint8_t rx = 0xFF;
    spi_write_read_blocking(SD_SPI_PORT, &b, &rx, 1);
    return rx;
}

static void spi_write_n(const uint8_t *buf, size_t n) {
    spi_write_blocking(SD_SPI_PORT, buf, n);
}
static void spi_read_n(uint8_t *buf, size_t n) {
    /* Sending 0xFF while reading */
    const uint8_t dummy = 0xFF;
    for (size_t i = 0; i < n; i++) {
        spi_write_read_blocking(SD_SPI_PORT, &dummy, &buf[i], 1);
    }
}

/* Wait for card to be ready (DO goes high = 0xFF).
   Return true on ready within timeout. */
static bool wait_ready(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (spi_xchg(0xFF) == 0xFF) return true;
    } while (!time_reached(deadline));
    return false;
}

/* Send command, return R1 response byte (or 0xFF on timeout). */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg) {
    /* ACMDs are CMD55 + CMDx */
    if (cmd & 0x80) {
        cmd &= 0x7F;
        uint8_t r = send_cmd(CMD55, 0);
        if (r > 1) return r;
    }

    /* Flush any prior transaction */
    cs_high();
    spi_xchg(0xFF);
    cs_low();
    if (!wait_ready(500)) return 0xFF;

    /* Command frame: cmd(6) | arg(32) | crc(7,1) */
    uint8_t frame[6];
    frame[0] = 0x40 | (cmd & 0x3F);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)(arg);
    /* Only CMD0 / CMD8 need a valid CRC in SPI mode */
    if (cmd == CMD0)      frame[5] = 0x95;
    else if (cmd == CMD8) frame[5] = 0x87;
    else                   frame[5] = 0x01;
    spi_write_n(frame, 6);

    /* CMD12 emits a stuff byte that must be discarded */
    if (cmd == CMD12) spi_xchg(0xFF);

    /* Poll R1 up to 10 times */
    uint8_t r;
    for (int i = 0; i < 10; i++) {
        r = spi_xchg(0xFF);
        if ((r & 0x80) == 0) return r;
    }
    return 0xFF;
}

static bool read_data_block(uint8_t *buf, uint32_t n) {
    /* Wait for data token */
    uint8_t token = 0xFF;
    absolute_time_t deadline = make_timeout_time_ms(200);
    do {
        token = spi_xchg(0xFF);
        if (token != 0xFF) break;
    } while (!time_reached(deadline));
    if (token != DATA_TOKEN_SINGLE_READ) return false;

    spi_read_n(buf, n);
    /* Discard CRC */
    spi_xchg(0xFF);
    spi_xchg(0xFF);
    return true;
}

static bool write_data_block(const uint8_t *buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    spi_xchg(token);
    if (token == DATA_TOKEN_STOP_TRAN) return true;

    spi_write_n(buf, 512);
    spi_xchg(0xFF);          /* CRC hi */
    spi_xchg(0xFF);          /* CRC lo */

    /* Data response: xxx0sss1 where sss=010 accept, 101 crc, 110 write err */
    uint8_t resp = spi_xchg(0xFF);
    if ((resp & 0x1F) != 0x05) return false;
    return true;
}

/* ── Public API ───────────────────────────────────────────────────────────── */
static int sd_spi_init_impl(void) {
    s_initialized = false;
    s_card_type   = SD_CARD_UNKNOWN;
    s_sector_cnt  = 0;

    /* GPIO + SPI peripheral init */
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);

    spi_init(SD_SPI_PORT, SD_SPI_SLOW_HZ);
    gpio_set_function(SD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    /* SD wants mode 0, 8-bit, MSB-first (spi_init defaults). */

    /* 80+ dummy clocks with CS high to wake card into SPI mode */
    cs_high();
    for (int i = 0; i < 10; i++) spi_xchg(0xFF);

    /* CMD0 → idle */
    cs_low();
    uint8_t r = 0xFF;
    for (int retry = 0; retry < 10; retry++) {
        r = send_cmd(CMD0, 0);
        if (r == R1_IDLE) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (r != R1_IDLE) { cs_high(); spi_xchg(0xFF); return SD_ERR_NO_CARD; }

    /* CMD8 → probe voltage / v2 */
    r = send_cmd(CMD8, 0x1AA);
    uint8_t r7[4] = {0};
    bool is_v2 = false;
    if (r == R1_IDLE) {
        /* Read remaining 4 bytes of R7 */
        spi_read_n(r7, 4);
        if (r7[2] == 0x01 && r7[3] == 0xAA) is_v2 = true;
    }

    /* ACMD41 loop with HCS bit set on v2 */
    uint32_t acmd_arg = is_v2 ? 0x40000000 : 0;
    absolute_time_t deadline = make_timeout_time_ms(2000);
    do {
        r = send_cmd(ACMD41 | 0x80, acmd_arg);
        if (r == 0) break;
        if (time_reached(deadline)) { cs_high(); spi_xchg(0xFF); return SD_ERR_TIMEOUT; }
        vTaskDelay(pdMS_TO_TICKS(5));
    } while (1);

    /* Check CCS (high-capacity bit) via CMD58 on v2 */
    if (is_v2) {
        r = send_cmd(CMD58, 0);
        if (r != 0) { cs_high(); spi_xchg(0xFF); return SD_ERR_IO; }
        uint8_t ocr[4];
        spi_read_n(ocr, 4);
        s_card_type = (ocr[0] & 0x40) ? SD_CARD_V2_HC : SD_CARD_V2_SC;
    } else {
        s_card_type = SD_CARD_V1;
    }

    /* Byte-addressed SDSC: force 512B block length */
    if (s_card_type != SD_CARD_V2_HC) {
        r = send_cmd(CMD16, 512);
        if (r != 0) { cs_high(); spi_xchg(0xFF); return SD_ERR_IO; }
    }

    /* Read CSD to figure out total capacity */
    r = send_cmd(CMD9, 0);
    if (r != 0) { cs_high(); spi_xchg(0xFF); return SD_ERR_IO; }
    uint8_t csd[16];
    if (!read_data_block(csd, 16)) { cs_high(); spi_xchg(0xFF); return SD_ERR_IO; }

    if ((csd[0] >> 6) == 1) {
        /* CSD v2 — SDHC/SDXC */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16)
                        | ((uint32_t)csd[8] << 8)
                        |  (uint32_t)csd[9];
        s_sector_cnt = (c_size + 1) * 1024u;
    } else {
        /* CSD v1 — SDSC */
        uint32_t c_size       = (((uint32_t)(csd[6] & 0x03)) << 10)
                              | (((uint32_t)csd[7]) << 2)
                              | (((uint32_t)csd[8]) >> 6);
        uint32_t c_size_mult  = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
        uint32_t read_bl_len  = csd[5] & 0x0F;
        uint32_t block_nr     = (c_size + 1) * (1u << (c_size_mult + 2));
        uint32_t block_len    = 1u << read_bl_len;
        s_sector_cnt          = (block_nr * block_len) / 512u;
    }

    cs_high();
    spi_xchg(0xFF);

    /* Switch to fast SPI clock now that init handshake is done */
    spi_set_baudrate(SD_SPI_PORT, SD_SPI_FAST_HZ);

    s_initialized = true;
    return SD_OK;
}

int sd_spi_init(void) {
    /* The activity-LED pin has to come up before any SPI happens so that
       it can light during the init handshake itself. After that, every
       public SD op brackets its bus traffic with sd_led_on/off, giving
       a precise visible indication of when the card is actually being
       talked to. */
    sd_led_init();
    sd_led_on();
    int rc = sd_spi_init_impl();
    sd_led_off();
    return rc;
}

void sd_spi_deinit(void) {
    if (s_initialized) {
        spi_deinit(SD_SPI_PORT);
        s_initialized = false;
    }
}

bool sd_spi_is_ready(void)      { return s_initialized; }
sd_card_type_t sd_spi_card_type(void) { return s_card_type; }
uint32_t sd_spi_sector_count(void)    { return s_sector_cnt; }

static inline uint32_t lba_to_addr(uint32_t lba) {
    /* SDHC / SDXC are addressed in sectors; SDSC in bytes */
    return (s_card_type == SD_CARD_V2_HC) ? lba : (lba * 512u);
}

int sd_spi_read_blocks(uint32_t lba, uint8_t *buf, uint32_t count) {
    if (!s_initialized) return SD_ERR_NOT_INIT;
    if (count == 0)     return SD_ERR_PARAM;

    sd_led_on();
    cs_low();
    int rc = SD_OK;

    if (count == 1) {
        if (send_cmd(CMD17, lba_to_addr(lba)) != 0) { rc = SD_ERR_IO; goto done; }
        if (!read_data_block(buf, 512))             { rc = SD_ERR_IO; goto done; }
    } else {
        if (send_cmd(CMD18, lba_to_addr(lba)) != 0) { rc = SD_ERR_IO; goto done; }
        for (uint32_t i = 0; i < count; i++) {
            if (!read_data_block(buf + i * 512, 512)) { rc = SD_ERR_IO; break; }
        }
        /* Stop transmission regardless */
        send_cmd(CMD12, 0);
    }

done:
    cs_high();
    spi_xchg(0xFF);
    sd_led_off();
    return rc;
}

int sd_spi_write_blocks(uint32_t lba, const uint8_t *buf, uint32_t count) {
    if (!s_initialized) return SD_ERR_NOT_INIT;
    if (count == 0)     return SD_ERR_PARAM;

    sd_led_on();
    cs_low();
    int rc = SD_OK;

    if (count == 1) {
        if (send_cmd(CMD24, lba_to_addr(lba)) != 0) { rc = SD_ERR_IO; goto done; }
        if (!write_data_block(buf, DATA_TOKEN_SINGLE_WRITE)) { rc = SD_ERR_IO; goto done; }
    } else {
        /* ACMD23 pre-erase hint — optional but speeds up writes on most cards */
        send_cmd(ACMD23 | 0x80, count);
        if (send_cmd(CMD25, lba_to_addr(lba)) != 0) { rc = SD_ERR_IO; goto done; }
        for (uint32_t i = 0; i < count; i++) {
            if (!write_data_block(buf + i * 512, DATA_TOKEN_MULTI_WRITE)) { rc = SD_ERR_IO; break; }
        }
        /* Send stop-tran token */
        if (!wait_ready(500)) { rc = SD_ERR_TIMEOUT; goto done; }
        spi_xchg(DATA_TOKEN_STOP_TRAN);
        /* Card goes busy → wait */
        if (!wait_ready(1000)) { rc = SD_ERR_TIMEOUT; goto done; }
    }

    /* Wait for internal write to complete */
    if (!wait_ready(1000)) rc = SD_ERR_TIMEOUT;

done:
    cs_high();
    spi_xchg(0xFF);
    sd_led_off();
    return rc;
}

int sd_spi_sync(void) {
    if (!s_initialized) return SD_ERR_NOT_INIT;
    sd_led_on();
    cs_low();
    bool ok = wait_ready(1000);
    cs_high();
    spi_xchg(0xFF);
    sd_led_off();
    return ok ? SD_OK : SD_ERR_TIMEOUT;
}

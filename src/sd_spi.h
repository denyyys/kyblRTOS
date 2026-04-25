#ifndef SD_SPI_H
#define SD_SPI_H

/* ── Low-level SPI SD card driver ─────────────────────────────────────────────
 * Sits underneath FatFs. Handles SD init sequence, CMD/ACMD issuing, and
 * block-level read/write. All the dirty SPI work is confined here so that
 * switching to a different storage medium (NAND, internal flash, SDIO) is
 * a matter of swapping this single file.
 *
 * Pin wiring (kyblRTOS board):
 *   SD CS    → GP5   (software-driven)
 *   SD MOSI  → GP3   (SPI0 TX)
 *   SD CLK   → GP2   (SPI0 SCK)
 *   SD MISO  → GP4   (SPI0 RX)
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SD_OK            = 0,
    SD_ERR_NO_CARD   = -1,
    SD_ERR_TIMEOUT   = -2,
    SD_ERR_CRC       = -3,
    SD_ERR_PARAM     = -4,
    SD_ERR_IO        = -5,
    SD_ERR_NOT_INIT  = -6,
} sd_err_t;

typedef enum {
    SD_CARD_UNKNOWN  = 0,
    SD_CARD_V1,                /* SDSC v1.x (byte-addressed)                    */
    SD_CARD_V2_SC,             /* SDSC v2.x (byte-addressed, but block-I/O)     */
    SD_CARD_V2_HC,             /* SDHC / SDXC (sector-addressed)                */
} sd_card_type_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */
int  sd_spi_init(void);            /* full SD init sequence; ok = 0           */
void sd_spi_deinit(void);
bool sd_spi_is_ready(void);

/* ── Card identity / geometry ─────────────────────────────────────────────── */
sd_card_type_t sd_spi_card_type(void);
uint32_t sd_spi_sector_count(void);    /* total 512B sectors; 0 if unknown   */

/* ── Block I/O (512-byte sectors) ─────────────────────────────────────────── */
int sd_spi_read_blocks (uint32_t lba,       uint8_t *buf, uint32_t count);
int sd_spi_write_blocks(uint32_t lba, const uint8_t *buf, uint32_t count);
int sd_spi_sync(void);                 /* flush write buffer                  */

#endif /* SD_SPI_H */

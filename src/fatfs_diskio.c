/*-----------------------------------------------------------------------*/
/*  FatFs ↔ sd_spi bridge for kyblRTOS                                   */
/*                                                                       */
/*  FatFs calls down into disk_*() functions; we pass them on to the     */
/*  sd_spi SPI SD-card driver. This is the single glue file that knows   */
/*  about both layers — switch media by writing a new diskio.            */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"

#define SD_PDRV     0   /* only one drive: the SPI SD slot */

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != SD_PDRV) return STA_NOINIT;
    int r = sd_spi_init();
    return (r == SD_OK) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != SD_PDRV) return STA_NOINIT;
    return sd_spi_is_ready() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != SD_PDRV)       return RES_PARERR;
    if (!sd_spi_is_ready())    return RES_NOTRDY;
    int r = sd_spi_read_blocks((uint32_t)sector, buff, (uint32_t)count);
    return (r == SD_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != SD_PDRV)       return RES_PARERR;
    if (!sd_spi_is_ready())    return RES_NOTRDY;
    int r = sd_spi_write_blocks((uint32_t)sector, buff, (uint32_t)count);
    return (r == SD_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != SD_PDRV)    return RES_PARERR;
    if (!sd_spi_is_ready()) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return (sd_spi_sync() == SD_OK) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)sd_spi_sector_count();
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;

    case GET_BLOCK_SIZE:
        /* Erase block in units of sectors. 1 = unknown/default. */
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/* FatFs calls get_fattime() to stamp file create/modify dates.
   We don't have an RTC yet — return a fixed 2026-01-01 00:00:00. */
DWORD get_fattime(void) {
    /* bit layout (31..0): YYYY-1980(7) MMMM(4) DDDDD(5) HHHHH(5) MMMMMM(6) SSSSS(5) */
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)1  << 21)   /* month = 1 */
         | ((DWORD)1  << 16)   /* day   = 1 */
         | ((DWORD)0  << 11)
         | ((DWORD)0  <<  5)
         | ((DWORD)0  <<  0);
}

#ifndef KYBLFS_H
#define KYBLFS_H

/*---------------------------------------------------------------------------/
/  kyblFS — the kyblRTOS virtual file system layer
/
/  Thin, thread-safe wrapper over FatFs. Every call grabs a single
/  recursive FreeRTOS mutex before touching FatFs, so concurrent tasks
/  cannot collide on the SPI bus or corrupt the FAT. Handles (files,
/  dirs) are opaque — the caller holds a pointer and never sees FIL
/  or DIR directly. This means swapping the underlying FS (SPIFFS,
/  LittleFS, ROMFS) is a one-implementation-file change; user apps
/  keep using kyblFS_* unchanged.
/---------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Opaque handles ──────────────────────────────────────────────────────── */
typedef struct kybl_file kybl_file_t;
typedef struct kybl_dir  kybl_dir_t;

/* ── Open-flags bitmask ─────────────────────────────────────────────────── */
#define KYBLFS_READ         0x01    /* open for reading                      */
#define KYBLFS_WRITE        0x02    /* open for writing                      */
#define KYBLFS_CREATE       0x04    /* create if missing                     */
#define KYBLFS_CREATE_NEW   0x08    /* fail if exists                        */
#define KYBLFS_TRUNCATE     0x10    /* truncate to zero on open              */
#define KYBLFS_APPEND       0x20    /* seek to EOF on open                   */

/* ── File/dir attribute bits ────────────────────────────────────────────── */
#define KYBLFS_ATTR_RO      0x01
#define KYBLFS_ATTR_HIDDEN  0x02
#define KYBLFS_ATTR_SYSTEM  0x04
#define KYBLFS_ATTR_DIR     0x10
#define KYBLFS_ATTR_ARCHIVE 0x20

/* ── File info struct returned by stat / readdir ─────────────────────────── */
typedef struct {
    char     name[256];      /* long name                                   */
    uint32_t size;           /* file size in bytes                          */
    uint16_t date;           /* FAT packed date                             */
    uint16_t time;           /* FAT packed time                             */
    uint8_t  attr;           /* KYBLFS_ATTR_* bitmask                       */
} kybl_finfo_t;

/* ── Return codes ────────────────────────────────────────────────────────── */
#define KYBLFS_OK              0
#define KYBLFS_ERR_IO         -1
#define KYBLFS_ERR_NO_FILE    -2
#define KYBLFS_ERR_DENIED     -3
#define KYBLFS_ERR_EXISTS     -4
#define KYBLFS_ERR_INVALID    -5
#define KYBLFS_ERR_NOT_READY  -6
#define KYBLFS_ERR_NO_FS      -7
#define KYBLFS_ERR_FULL       -8
#define KYBLFS_ERR_LOCKED     -9
#define KYBLFS_ERR_NOMEM      -10
#define KYBLFS_ERR_TIMEOUT    -11
#define KYBLFS_ERR_OTHER      -99

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
int   kyblFS_init  (void);                  /* create mutex + register disk */
int   kyblFS_mount (void);                  /* mount the SD volume          */
int   kyblFS_unmount(void);
bool  kyblFS_is_mounted(void);

/* Format the card (destroys all data). `label` may be NULL. */
int   kyblFS_format(const char *label);

/* ── File I/O ────────────────────────────────────────────────────────────── */
kybl_file_t *kyblFS_open (const char *path, int flags);
int   kyblFS_close (kybl_file_t *f);
int   kyblFS_read  (kybl_file_t *f,       void *buf, size_t n);  /* bytes read, <0=err  */
int   kyblFS_write (kybl_file_t *f, const void *buf, size_t n);  /* bytes written       */
int   kyblFS_seek  (kybl_file_t *f, uint32_t offset);
uint32_t kyblFS_tell(kybl_file_t *f);
uint32_t kyblFS_size(kybl_file_t *f);
int   kyblFS_sync  (kybl_file_t *f);
bool  kyblFS_eof   (kybl_file_t *f);

/* ── Path ops ────────────────────────────────────────────────────────────── */
int   kyblFS_unlink(const char *path);
int   kyblFS_rename(const char *from, const char *to);
int   kyblFS_mkdir (const char *path);
int   kyblFS_stat  (const char *path, kybl_finfo_t *info);
int   kyblFS_touch (const char *path);       /* create empty or update mtime */

/* ── Directory ───────────────────────────────────────────────────────────── */
kybl_dir_t *kyblFS_opendir(const char *path);
int   kyblFS_readdir (kybl_dir_t *d, kybl_finfo_t *info);  /* 1=item, 0=end, <0=err */
int   kyblFS_closedir(kybl_dir_t *d);

/* ── Volume info ─────────────────────────────────────────────────────────── */
int   kyblFS_statvfs(uint64_t *total_bytes, uint64_t *free_bytes);
int   kyblFS_label    (char *buf, size_t len);  /* volume label into buf    */
int   kyblFS_set_label(const char *label);      /* write new label (≤11ch)  */

/* ── Utility ─────────────────────────────────────────────────────────────── */
const char *kyblFS_strerror(int err);

#endif /* KYBLFS_H */

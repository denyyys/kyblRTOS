/*---------------------------------------------------------------------------/
/  kyblFS — thread-safe virtual filesystem layer
/---------------------------------------------------------------------------*/

#include "kyblFS.h"
#include "ff.h"
#include "diskio.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Opaque handle bodies ──────────────────────────────────────────────── */
struct kybl_file { FIL fp; };
struct kybl_dir  { DIR dp; };

/* ── Internal state ────────────────────────────────────────────────────── */
static FATFS              s_fs;
static bool               s_mounted  = false;
static bool               s_inited   = false;
static SemaphoreHandle_t  s_lock     = NULL;

#define LOCK_TIMEOUT_MS 5000

/* ── Lock helpers ──────────────────────────────────────────────────────── */
static inline bool fs_lock(void) {
    if (!s_lock) return false;
    return xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}
static inline void fs_unlock(void) {
    if (s_lock) xSemaphoreGiveRecursive(s_lock);
}

/* ── FatFs ↔ kyblFS error translation ──────────────────────────────────── */
static int xlate_err(FRESULT fr) {
    switch (fr) {
    case FR_OK:                 return KYBLFS_OK;
    case FR_DISK_ERR:           return KYBLFS_ERR_IO;
    case FR_INT_ERR:            return KYBLFS_ERR_IO;
    case FR_NOT_READY:          return KYBLFS_ERR_NOT_READY;
    case FR_NO_FILE:            return KYBLFS_ERR_NO_FILE;
    case FR_NO_PATH:            return KYBLFS_ERR_NO_FILE;
    case FR_INVALID_NAME:       return KYBLFS_ERR_INVALID;
    case FR_DENIED:             return KYBLFS_ERR_DENIED;
    case FR_EXIST:              return KYBLFS_ERR_EXISTS;
    case FR_INVALID_OBJECT:     return KYBLFS_ERR_INVALID;
    case FR_WRITE_PROTECTED:    return KYBLFS_ERR_DENIED;
    case FR_INVALID_DRIVE:      return KYBLFS_ERR_INVALID;
    case FR_NOT_ENABLED:        return KYBLFS_ERR_NO_FS;
    case FR_NO_FILESYSTEM:      return KYBLFS_ERR_NO_FS;
    case FR_MKFS_ABORTED:       return KYBLFS_ERR_OTHER;
    case FR_TIMEOUT:            return KYBLFS_ERR_TIMEOUT;
    case FR_LOCKED:             return KYBLFS_ERR_LOCKED;
    case FR_NOT_ENOUGH_CORE:    return KYBLFS_ERR_NOMEM;
    case FR_TOO_MANY_OPEN_FILES:return KYBLFS_ERR_FULL;
    case FR_INVALID_PARAMETER:  return KYBLFS_ERR_INVALID;
    default:                    return KYBLFS_ERR_OTHER;
    }
}

/* ── Fill kybl_finfo_t from FatFs FILINFO ──────────────────────────────── */
static void fill_info(kybl_finfo_t *out, const FILINFO *fi) {
    strncpy(out->name, fi->fname, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->size = (uint32_t)fi->fsize;
    out->date = fi->fdate;
    out->time = fi->ftime;
    out->attr = fi->fattrib;
}

/* ── Translate kyblFS flag bits → FatFs mode bits ──────────────────────── */
static BYTE xlate_flags(int flags) {
    BYTE m = 0;
    if (flags & KYBLFS_READ)       m |= FA_READ;
    if (flags & KYBLFS_WRITE)      m |= FA_WRITE;
    if (flags & KYBLFS_CREATE_NEW) m |= FA_CREATE_NEW;
    else if (flags & KYBLFS_TRUNCATE && flags & KYBLFS_CREATE) m |= FA_CREATE_ALWAYS;
    else if (flags & KYBLFS_CREATE)  m |= FA_OPEN_ALWAYS;
    else if (flags & KYBLFS_APPEND)  m |= FA_OPEN_APPEND;
    else                             m |= FA_OPEN_EXISTING;
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════════ */
int kyblFS_init(void) {
    if (s_inited) return KYBLFS_OK;
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) return KYBLFS_ERR_NOMEM;
    s_inited = true;
    return KYBLFS_OK;
}

int kyblFS_mount(void) {
    if (!s_inited) { int r = kyblFS_init(); if (r) return r; }
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_mount(&s_fs, "", 1 /* immediate mount */);
    if (fr == FR_OK) s_mounted = true;
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_unmount(void) {
    if (!s_inited || !s_mounted) return KYBLFS_OK;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_mount(NULL, "", 0);
    s_mounted = false;
    fs_unlock();
    return xlate_err(fr);
}

bool kyblFS_is_mounted(void) { return s_mounted; }

int kyblFS_format(const char *label) {
    if (!s_inited) { int r = kyblFS_init(); if (r) return r; }
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;

    /* Unmount first to make sure the disk is idle */
    if (s_mounted) { f_mount(NULL, "", 0); s_mounted = false; }

    /* 4 KB working buffer for mkfs */
    void *work = pvPortMalloc(FF_MAX_SS * 8);
    if (!work) { fs_unlock(); return KYBLFS_ERR_NOMEM; }

    MKFS_PARM opt = {
        .fmt      = FM_FAT | FM_FAT32 | FM_SFD,
        .n_fat    = 1,
        .align    = 0,
        .n_root   = 0,
        .au_size  = 0,
    };
    FRESULT fr = f_mkfs("", &opt, work, FF_MAX_SS * 8);
    vPortFree(work);

    /* f_setlabel needs a registered FATFS work-area for the volume. We
       de-registered s_fs above (f_mount(NULL,…)) so we have to re-register
       it before setting the label or the call no-ops with FR_NOT_ENABLED.
       Use delayed-mount form (3rd arg = 0) so we don't re-touch the disk
       — f_setlabel will lazy-mount internally on the first sector access. */
    if (fr == FR_OK && label && label[0]) {
        f_mount(&s_fs, "", 0);
        FRESULT lr = f_setlabel(label);
        f_mount(NULL, "", 0);
        (void)lr;   /* don't fail the format if labelling fails */
    }
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_set_label(const char *label) {
    if (!s_mounted || !label) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_setlabel(label);
    fs_unlock();
    return xlate_err(fr);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  FILE I/O
 * ══════════════════════════════════════════════════════════════════════════ */
kybl_file_t *kyblFS_open(const char *path, int flags) {
    if (!s_mounted) return NULL;
    if (!fs_lock()) return NULL;

    kybl_file_t *f = pvPortMalloc(sizeof(*f));
    if (!f) { fs_unlock(); return NULL; }
    memset(f, 0, sizeof(*f));

    FRESULT fr = f_open(&f->fp, path, xlate_flags(flags));
    if (fr != FR_OK) {
        vPortFree(f);
        fs_unlock();
        return NULL;
    }

    if (flags & KYBLFS_APPEND) {
        /* FA_OPEN_APPEND already seeks to EOF, but be explicit for
           CREATE|APPEND combos too. */
        f_lseek(&f->fp, f_size(&f->fp));
    }
    fs_unlock();
    return f;
}

int kyblFS_close(kybl_file_t *f) {
    if (!f) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_close(&f->fp);
    vPortFree(f);
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_read(kybl_file_t *f, void *buf, size_t n) {
    if (!f || !buf) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    UINT br = 0;
    FRESULT fr = f_read(&f->fp, buf, (UINT)n, &br);
    fs_unlock();
    if (fr != FR_OK) return xlate_err(fr);
    return (int)br;
}

int kyblFS_write(kybl_file_t *f, const void *buf, size_t n) {
    if (!f || !buf) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    UINT bw = 0;
    FRESULT fr = f_write(&f->fp, buf, (UINT)n, &bw);
    fs_unlock();
    if (fr != FR_OK) return xlate_err(fr);
    return (int)bw;
}

int kyblFS_seek(kybl_file_t *f, uint32_t offset) {
    if (!f) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_lseek(&f->fp, offset);
    fs_unlock();
    return xlate_err(fr);
}

uint32_t kyblFS_tell(kybl_file_t *f) {
    if (!f) return 0;
    if (!fs_lock()) return 0;
    uint32_t pos = (uint32_t)f_tell(&f->fp);
    fs_unlock();
    return pos;
}

uint32_t kyblFS_size(kybl_file_t *f) {
    if (!f) return 0;
    if (!fs_lock()) return 0;
    uint32_t sz = (uint32_t)f_size(&f->fp);
    fs_unlock();
    return sz;
}

int kyblFS_sync(kybl_file_t *f) {
    if (!f) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_sync(&f->fp);
    fs_unlock();
    return xlate_err(fr);
}

bool kyblFS_eof(kybl_file_t *f) {
    if (!f) return true;
    if (!fs_lock()) return true;
    bool eof = (f_eof(&f->fp) != 0);
    fs_unlock();
    return eof;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  PATH OPS
 * ══════════════════════════════════════════════════════════════════════════ */
int kyblFS_unlink(const char *path) {
    if (!s_mounted) return KYBLFS_ERR_NO_FS;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_unlink(path);
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_rename(const char *from, const char *to) {
    if (!s_mounted) return KYBLFS_ERR_NO_FS;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_rename(from, to);
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_mkdir(const char *path) {
    if (!s_mounted) return KYBLFS_ERR_NO_FS;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_mkdir(path);
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_stat(const char *path, kybl_finfo_t *info) {
    if (!s_mounted || !info) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FILINFO fi;
    FRESULT fr = f_stat(path, &fi);
    if (fr == FR_OK) fill_info(info, &fi);
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_touch(const char *path) {
    if (!s_mounted) return KYBLFS_ERR_NO_FS;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_OPEN_ALWAYS | FA_WRITE);
    if (fr == FR_OK) f_close(&fp);
    fs_unlock();
    return xlate_err(fr);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  DIRECTORY
 * ══════════════════════════════════════════════════════════════════════════ */
kybl_dir_t *kyblFS_opendir(const char *path) {
    if (!s_mounted) return NULL;
    if (!fs_lock()) return NULL;

    kybl_dir_t *d = pvPortMalloc(sizeof(*d));
    if (!d) { fs_unlock(); return NULL; }
    memset(d, 0, sizeof(*d));

    FRESULT fr = f_opendir(&d->dp, path);
    if (fr != FR_OK) {
        vPortFree(d);
        fs_unlock();
        return NULL;
    }
    fs_unlock();
    return d;
}

int kyblFS_readdir(kybl_dir_t *d, kybl_finfo_t *info) {
    if (!d || !info) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FILINFO fi;
    FRESULT fr = f_readdir(&d->dp, &fi);
    if (fr != FR_OK) { fs_unlock(); return xlate_err(fr); }
    if (fi.fname[0] == '\0') { fs_unlock(); return 0; }     /* end */
    fill_info(info, &fi);
    fs_unlock();
    return 1;
}

int kyblFS_closedir(kybl_dir_t *d) {
    if (!d) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    FRESULT fr = f_closedir(&d->dp);
    vPortFree(d);
    fs_unlock();
    return xlate_err(fr);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  VOLUME INFO
 * ══════════════════════════════════════════════════════════════════════════ */
int kyblFS_statvfs(uint64_t *total_bytes, uint64_t *free_bytes) {
    if (!s_mounted) return KYBLFS_ERR_NO_FS;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;

    FATFS *fs;
    DWORD free_clust = 0;
    FRESULT fr = f_getfree("", &free_clust, &fs);
    if (fr == FR_OK) {
        uint64_t tot_sect  = (uint64_t)(fs->n_fatent - 2) * fs->csize;
        uint64_t free_sect = (uint64_t)free_clust * fs->csize;
        if (total_bytes) *total_bytes = tot_sect  * FF_MIN_SS;
        if (free_bytes)  *free_bytes  = free_sect * FF_MIN_SS;
    }
    fs_unlock();
    return xlate_err(fr);
}

int kyblFS_label(char *buf, size_t len) {
    if (!s_mounted || !buf || len < 1) return KYBLFS_ERR_INVALID;
    if (!fs_lock()) return KYBLFS_ERR_TIMEOUT;
    char lbl[12] = {0};
    FRESULT fr = f_getlabel("", lbl, NULL);
    if (fr == FR_OK) {
        strncpy(buf, lbl, len - 1);
        buf[len - 1] = '\0';
    }
    fs_unlock();
    return xlate_err(fr);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  UTILITY
 * ══════════════════════════════════════════════════════════════════════════ */
const char *kyblFS_strerror(int err) {
    switch (err) {
    case KYBLFS_OK:             return "OK";
    case KYBLFS_ERR_IO:         return "I/O error";
    case KYBLFS_ERR_NO_FILE:    return "No such file or directory";
    case KYBLFS_ERR_DENIED:     return "Access denied";
    case KYBLFS_ERR_EXISTS:     return "Already exists";
    case KYBLFS_ERR_INVALID:    return "Invalid argument";
    case KYBLFS_ERR_NOT_READY:  return "Disk not ready";
    case KYBLFS_ERR_NO_FS:      return "No filesystem";
    case KYBLFS_ERR_FULL:       return "Full / too many open files";
    case KYBLFS_ERR_LOCKED:     return "File locked";
    case KYBLFS_ERR_NOMEM:      return "Out of memory";
    case KYBLFS_ERR_TIMEOUT:    return "Timeout";
    default:                    return "Unknown error";
    }
}

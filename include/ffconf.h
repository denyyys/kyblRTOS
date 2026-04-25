/*---------------------------------------------------------------------------/
/  kyblRTOS — FatFs configuration
/
/  This replaces the ffconf.h that ships with FatFs. It is kept on the
/  project include path BEFORE the vendored FatFs source directory so the
/  FatFs build picks up these settings instead of its defaults.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF      5380    /* FatFs R0.15a — must match FF_DEFINED in ff.h */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/
#define FF_FS_READONLY      0   /* Read/write mode                           */
#define FF_FS_MINIMIZE      0   /* Full feature set                          */
#define FF_USE_FIND         1   /* f_findfirst / f_findnext                  */
#define FF_USE_MKFS         1   /* f_mkfs                                    */
#define FF_USE_FASTSEEK     0
#define FF_USE_EXPAND       0
#define FF_USE_CHMOD        1
#define FF_USE_LABEL        1
#define FF_USE_FORWARD      0
#define FF_USE_STRFUNC      1   /* f_gets / f_putc / f_puts / f_printf       */
#define FF_PRINT_LLI        1
#define FF_PRINT_FLOAT      1
#define FF_STRF_ENCODE      3   /* UTF-8 for string functions                */

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/
#define FF_CODE_PAGE        437
#define FF_USE_LFN          1   /* Static LFN buffer — kyblFS serializes    */
#define FF_MAX_LFN          255
#define FF_LFN_UNICODE      0   /* ANSI / OEM in API                         */
#define FF_LFN_BUF          255
#define FF_SFN_BUF          12
#define FF_FS_RPATH         2   /* Relative paths + f_getcwd                 */

/*---------------------------------------------------------------------------/
/ Drive / Volume Configurations
/---------------------------------------------------------------------------*/
#define FF_VOLUMES          1
#define FF_STR_VOLUME_ID    0
#define FF_VOLUME_STRS      "RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION  0
#define FF_MIN_SS           512
#define FF_MAX_SS           512
#define FF_LBA64            0
#define FF_MIN_GPT          0x10000000
#define FF_USE_TRIM         0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/
#define FF_FS_TINY          0
#define FF_FS_EXFAT         0
#define FF_FS_NORTC         1   /* No RTC → use fixed date below             */
#define FF_NORTC_MON        1
#define FF_NORTC_MDAY       1
#define FF_NORTC_YEAR       2026
#define FF_FS_NOFSINFO      0
#define FF_FS_LOCK          4   /* Max 4 simultaneously open files           */
#define FF_FS_REENTRANT     0   /* kyblFS wraps everything in its own mutex */
#define FF_FS_TIMEOUT       1000
#define FF_SYNC_t           HANDLE

/* end of ffconf.h */

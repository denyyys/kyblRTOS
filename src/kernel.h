#ifndef KERNEL_H
#define KERNEL_H

#include "kyblrtos.h"

/**
 * Initialise the kernel program registry.
 * Registers all built-in commands automatically.
 */
void kernel_init(void);

/**
 * Register a program / command in the registry.
 * Returns KYBL_OK on success, KYBL_ERR_FULL if the table is full,
 * or KYBL_ERR_INVALID_ARG if any required field is NULL.
 */
int kernel_register(const kybl_program_t *prog);

/**
 * Look up a program by name.  Returns a pointer into the registry
 * or NULL if not found.
 */
const kybl_program_t *kernel_find(const char *name);

/**
 * Execute a registered program by name with the given argument vector.
 * Returns the program's return value, or KYBL_ERR_NOT_FOUND.
 */
int kernel_exec(const char *name, int argc, char *argv[]);

/**
 * Iterate over all registered programs.
 * Set *idx to 0 before the first call; advances *idx each call.
 * Returns NULL when the list is exhausted.
 */
const kybl_program_t *kernel_next(int *idx);

/**
 * Return the number of currently registered programs / commands.
 */
int kernel_count(void);

/* ── Future SD-card loader stub ───────────────────────────────────────────── */
/**
 * (Future) Load a compiled kyblRTOS program from an SD card path,
 * register it, and optionally execute it.
 * Not implemented — placeholder for SD-card support.
 */
int kernel_load_from_sd(const char *path);

#endif /* KERNEL_H */

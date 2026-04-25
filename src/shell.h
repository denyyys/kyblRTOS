#ifndef SHELL_H
#define SHELL_H

#include "FreeRTOS.h"
#include "task.h"

/**
 * FreeRTOS task entry point for the interactive USB serial shell.
 * Create this task from main() before starting the scheduler.
 *
 * Recommended stack size : 1024 words
 * Recommended priority   : 2
 */
void shell_task(void *params);

#endif /* SHELL_H */

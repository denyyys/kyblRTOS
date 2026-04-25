#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ── Scheduler ────────────────────────────────────────────────────────────── */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* RP2040 port manages this */
#define configUSE_TICKLESS_IDLE                 0

/* ── Clock ────────────────────────────────────────────────────────────────── */
#define configCPU_CLOCK_HZ                      125000000UL
#define configTICK_RATE_HZ                      1000UL          /* 1 ms tick  */

/* ── Tasks ────────────────────────────────────────────────────────────────── */
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                256             /* words       */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8

/* ── Memory ───────────────────────────────────────────────────────────────── */
/* Pico has 264 KB SRAM. Leave room for .data/.bss (pico-sdk + cyw43 + lwIP   */
/* pools), core-1 stack, and the main stack — reserve 128 KB for FreeRTOS.    */
#define configTOTAL_HEAP_SIZE                   (128 * 1024)
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* ── Hook functions ───────────────────────────────────────────────────────── */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1

/* ── Run-time stats ───────────────────────────────────────────────────────── */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1

/* ── Co-routines ──────────────────────────────────────────────────────────── */
#define configUSE_CO_ROUTINES                   0

/* ── Software timers ──────────────────────────────────────────────────────── */
#define configUSE_TIMERS                        1
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* ── RP2040 SMP (dual-core) ───────────────────────────────────────────────── */
/* Run FreeRTOS on both cores; shell on core-0, user tasks may use core-1     */

/* ── Interrupt priorities ─────────────────────────────────────────────────── */
#define configKERNEL_INTERRUPT_PRIORITY         ( 255 )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 191 )
#define configMAX_API_CALL_INTERRUPT_PRIORITY   ( 191 )

/* ── API inclusions ───────────────────────────────────────────────────────── */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_xSemaphoreGetMutexHolder        1

#endif /* FREERTOS_CONFIG_H */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

#include "shell.h"
#include "kernel.h"
#include "led_display.h"
#include "wifi_manager.h"
#include "kyblFS.h"
#include "kyblrtos.h"

#define SHELL_STACK_WORDS   2048
#define SHELL_PRIORITY      2

/* Count of malloc-failures we've seen — surfaced to the shell via 'mem'. */
volatile uint32_t g_malloc_fail_count = 0;

/* Non-fatal: flash all 4 LEDs briefly so the user notices, then return.
   The caller of pvPortMalloc receives NULL and must handle it — turning
   every heap miss into a permanent hang (as the old infinite-loop hook
   did) made things like 'fragcheck' require a hard reset. */
void vApplicationMallocFailedHook(void) {
    g_malloc_fail_count++;
    for (int i = 0; i < 4; i++) {
        gpio_put(KYBL_LED_PIN_BIT0, 1);
        gpio_put(KYBL_LED_PIN_BIT1, 1);
        gpio_put(KYBL_LED_PIN_BIT2, 1);
        gpio_put(KYBL_LED_PIN_BIT3, 1);
        busy_wait_us(40000);
        gpio_put(KYBL_LED_PIN_BIT0, 0);
        gpio_put(KYBL_LED_PIN_BIT1, 0);
        gpio_put(KYBL_LED_PIN_BIT2, 0);
        gpio_put(KYBL_LED_PIN_BIT3, 0);
        busy_wait_us(40000);
    }
}

/* Stack overflow is truly unrecoverable — keep the blink-forever sentinel. */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name;
    for (;;) { led_display_set(0x05); sleep_ms(200); led_display_set(0x0A); sleep_ms(200); }
}

int main(void) {
    stdio_init_all();
    led_display_init();

    for (int i = 0; i <= 15; i++) { led_display_set((uint8_t)i); sleep_ms(40); }
    led_display_clear();

    /* wifi_manager_init() must NOT be called here — cyw43_arch_init()
       requires the FreeRTOS scheduler to already be running when using
       pico_cyw43_arch_lwip_freertos. It is called lazily from shell_task. */
    kernel_init();
    /* kyblFS just creates the guarding mutex here — actual SD bring-up and
       f_mount() happen under the scheduler via the 'mount' shell command. */
    kyblFS_init();

    xTaskCreate(shell_task, "Shell", SHELL_STACK_WORDS, NULL, SHELL_PRIORITY, NULL);
    vTaskStartScheduler();

    for (;;) {}
    return 0;
}

#include "led_display.h"
#include "kyblrtos.h"
#include "hardware/gpio.h"

/* Internal state */
static uint8_t s_current_value = 0;

static const uint8_t LED_PINS[4] = {
    KYBL_LED_PIN_BIT0,
    KYBL_LED_PIN_BIT1,
    KYBL_LED_PIN_BIT2,
    KYBL_LED_PIN_BIT3,
};

/* ── Public API ────────────────────────────────────────────────────────────── */

void led_display_init(void) {
    for (int i = 0; i < 4; i++) {
        gpio_init(LED_PINS[i]);
        gpio_set_dir(LED_PINS[i], GPIO_OUT);
        gpio_put(LED_PINS[i], 0);
    }
    s_current_value = 0;
}

void led_display_set(uint8_t value) {
    s_current_value = value & 0x0F;
    for (int i = 0; i < 4; i++) {
        gpio_put(LED_PINS[i], (s_current_value >> i) & 1);
    }
}

void led_display_clear(void) {
    led_display_set(0);
}

uint8_t led_display_get(void) {
    return s_current_value;
}

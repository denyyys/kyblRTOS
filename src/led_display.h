#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include <stdint.h>

/**
 * Initialise the four LED GPIO pins as outputs (all off).
 * Must be called once before any other led_display_* function.
 */
void led_display_init(void);

/**
 * Show the lower 4 bits of `value` on the four LEDs.
 *   GP16 = bit 0 (LSB)
 *   GP17 = bit 1
 *   GP18 = bit 2
 *   GP19 = bit 3 (MSB of nibble)
 *
 * Values 0-15 are valid; higher bits are silently masked.
 */
void led_display_set(uint8_t value);

/**
 * Turn all four LEDs off (equivalent to led_display_set(0)).
 */
void led_display_clear(void);

/**
 * Return the nibble currently displayed (0-15).
 */
uint8_t led_display_get(void);

#endif /* LED_DISPLAY_H */

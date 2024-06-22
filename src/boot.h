#ifndef BOOT_H
#define BOOT_H
#define MAX_INPUT_LENGTH 100

// binary LED's
#define GPIO15 15
#define GPIO14 14
#define GPIO13 13
#define GPIO12 12

// status LED's
#define LED_BLUE 26
#define LED_RED 27
#define LED_GREEN 28

#include "pico/stdlib.h"

void boot();
void LED_init();

#endif
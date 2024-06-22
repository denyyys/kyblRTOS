#include "boot.h"

void LED_init() {

    //status LED's
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);

    //binary LED's
    gpio_init(GPIO15);
    gpio_init(GPIO14);
    gpio_init(GPIO13);
    gpio_init(GPIO12);
    gpio_set_dir(GPIO15, GPIO_OUT);
    gpio_set_dir(GPIO14, GPIO_OUT);
    gpio_set_dir(GPIO13, GPIO_OUT);
    gpio_set_dir(GPIO12, GPIO_OUT);

}

void boot() {
    LED_init();
}
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

void LCD_init_boot() {

    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));

    lcd_init();
}

int boot() {
    LED_init();
    LCD_init_boot();
}
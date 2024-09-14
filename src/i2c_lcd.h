#ifndef I2C_LCD_H
#define I2C_LCD_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"

#define I2C_PORT i2c0
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define LCD_ADDR 0x27
#define MAX_LINES      2
#define MAX_CHARS      16

void i2c_write_byte(uint8_t val);
void lcd_toggle_enable(uint8_t val);
void lcd_send_byte(uint8_t val, int mode);
void lcd_clear(void);
void lcd_set_cursor(int line, int position);
static void inline lcd_char(char val);
void lcd_string(const char *s);
void lcd_init();


#endif 

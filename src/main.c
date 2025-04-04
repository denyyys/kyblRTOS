#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "command.h"
#include "boot.h"
#include "queue.h"
#include "i2c_lcd.h"

QueueHandle_t led_queue;
QueueHandle_t lcd_boot_status_queue;

void status_led(void *pvParameters)
{
    while (1) {
        while (1) {
        char command[MAX_INPUT_LENGTH];
        
        // wait for commands from other tasks
        if (xQueueReceive(led_queue, command, portMAX_DELAY)) {
            if (strcmp(command, "enter") == 0) {

                gpio_put(LED_BLUE, 1); 
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_put(LED_BLUE, 0);

            } else if(strcmp(command, "boot_ok") == 0) {

                gpio_put(LED_GREEN, 1);
                vTaskDelay(pdMS_TO_TICKS(2500));
                gpio_put(LED_GREEN, 0);

            } else if(strcmp(command, "boot_error") == 0) {

                gpio_put(LED_RED, 1);

            }
        }
    }
}
}

void input_task(void *pvParameters) 
{
    char input[MAX_INPUT_LENGTH];
    int input_pos = 0;

    while (1) {
        int ch = getchar_timeout_us(0);

        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\r' || ch == '\n') {
                xQueueSendToBack(led_queue, "enter", portMAX_DELAY); // send command to status_led task
                if (input_pos > 0) {
                    input[input_pos] = '\0';

                    // Process input
                    char *command = strtok(input, " ");
                    char *arguments = strtok(NULL, "");

                    if (command != NULL) {
                        printf("\n");
                        if (strcmp(command, "exit") == 0) {
                            break;
                        }
                     
            
                        // Execute the command
                        if (!execute_command(command, arguments)) {
                            printf("unknown command.\n");
                            fflush(stdout);
                        }
                    }
                    input_pos = 0;
                }
                printf("\n# ");
                fflush(stdout);

            } else if (input_pos < MAX_INPUT_LENGTH - 1) {
                input[input_pos++] = ch;
                putchar(ch);
                fflush(stdout);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}


void lcd_task()
{
    while(1){
        char command[MAX_INPUT_LENGTH];

        if (xQueueReceive(lcd_boot_status_queue, command, portMAX_DELAY)) {
            if (strcmp(command, "boot_ok") == 0) {

                lcd_set_cursor(0, 0);
                lcd_string("boot ok !");
                vTaskDelay(pdMS_TO_TICKS(2500));

            } else if(strcmp(command, "boot_error") == 0) {

                lcd_set_cursor(0, 0);
                lcd_string("boot error!");
                vTaskDelay(pdMS_TO_TICKS(2500));
            
        }
    }
            lcd_clear();
            lcd_set_cursor(1, 0);
            lcd_string("kyblRTOS v0.2.43");
    }
}


int main()
{
    stdio_init_all();

    boot();

    led_queue = xQueueCreate(10, sizeof(char[MAX_INPUT_LENGTH]));
    if (led_queue == NULL) {
        printf("[debug: Failed to create input queue for LED]\n");
        return 1;
    }

    lcd_boot_status_queue = xQueueCreate(10, sizeof(char[MAX_INPUT_LENGTH]));
    if (lcd_boot_status_queue == NULL) {
        printf("[debug: Failed to create input queue for LCD]\n");
        return 1;
    }

    xTaskCreate(status_led, "Status_LED_Task", 256, NULL, 1, NULL);
    xTaskCreate(lcd_task, "LCD_Task", 256, NULL, 1, NULL);
    xTaskCreate(input_task, "INPUT_Task", 256, NULL, 1, NULL);

    if(!boot){
        xQueueSendToBack(led_queue, "boot_error", portMAX_DELAY);
        xQueueSendToBack(lcd_boot_status_queue, "boot_error", portMAX_DELAY);
        }else {
        xQueueSendToBack(led_queue, "boot_ok", portMAX_DELAY);
        xQueueSendToBack(lcd_boot_status_queue, "boot_ok", portMAX_DELAY);
    }

    vTaskStartScheduler();

    while(1){};
    
}
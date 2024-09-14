#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "command.h"
#include "boot.h"
#include "queue.h"
#include "i2c_lcd.h"

QueueHandle_t led_queue;
QueueHandle_t lcd_queue;

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
                vTaskDelay(pdMS_TO_TICKS(5000));
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
                xQueueSendToBack(led_queue, "enter", portMAX_DELAY); // send command to blue_action_led task
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
    char *loop_message[] = {
                    "kyblRTOS", "Indev 0.2.20",
                    "developed by", "Kybl Enterprise",
                    "chceme", "huleni",
                    "toto je to", "co to ma byt",
                    "hlavo", "bramborova"
            };

    char user_message[50] = "";

    bool lcd_stop = false;

    while(1){

        if (xQueueReceive(lcd_queue, &user_message, 0) == pdPASS) {
            // If the message is "stop", clear the user message and set flag to stop looping
            if (strcmp(user_message, "stop") == 0) {
                strcpy(user_message, "");
                lcd_stop = true;
            } else {
                lcd_stop = false; // Reset flag if a new user message is received
                lcd_string(user_message);
            }
        }

        if (!lcd_stop && strlen(loop_message) > 0) {

            for (int m = 0; m < sizeof(loop_message) / sizeof(loop_message[0]); m += MAX_LINES) {
                for (int line = 0; line < MAX_LINES; line++) {
                    lcd_set_cursor(line, (MAX_CHARS / 2) - strlen(loop_message[m + line]) / 2);
                    lcd_string(loop_message[m + line]);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            lcd_clear();
            }
        
        }
    }
}


int main()
{
    stdio_init_all();

    boot();

    led_queue = xQueueCreate(10, sizeof(char[MAX_INPUT_LENGTH]));
    lcd_queue = xQueueCreate(QUEUE_LENGTH, ITEM_SIZE);
    if (led_queue == NULL) {
        printf("[debug: Failed to create input queue]\n");
        return 1;
    }

    xTaskCreate(input_task, "INPUT_Task", 256, NULL, 1, NULL);
    xTaskCreate(status_led, "Status_LED_Task", 256, NULL, 1, NULL);

    if(!boot){
        xQueueSendToBack(led_queue, "boot_error", portMAX_DELAY);
        }else {
        xQueueSendToBack(led_queue, "boot_ok", portMAX_DELAY);
    }

    xTaskCreate(lcd_task, "LCD_Task", 256, NULL, 1, NULL);


    vTaskStartScheduler();

    while(1){};
    
}
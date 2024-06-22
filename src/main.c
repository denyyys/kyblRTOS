#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "command.h"

void serial_test()
{
    const uint LED_BLUE = 26;
    gpio_set_dir(LED_BLUE, GPIO_OUT);

    while (1){
        printf("hulpero\n");
        gpio_put(LED_BLUE, 1);
        vTaskDelay(1500);
        gpio_put(LED_BLUE, 0);
        vTaskDelay(1500);
    }
}

TaskHandle_t blue_led_task_handle = NULL;

void blue_action_led(void *pvParameters){

    printf("[debug: BLUE LED task started]\n");
    fflush(stdout);

    const uint LED_BLUE = 26;
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        gpio_put(LED_BLUE, 1);
        vTaskDelay(100); 
        gpio_put(LED_BLUE, 0);
        printf("[debug: BLUE LED blinked]\n");
        fflush(stdout);
    }
}

void input_task(void *pvParameters) {

    char input[MAX_INPUT_LENGTH];
    int input_pos = 0;

    while (1) {
        int ch = getchar_timeout_us(0);

        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\r' || ch == '\n') {
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

                    // Notify BLUE LED task to blink
                    if (blue_led_task_handle != NULL) {
                        printf("[debug: Sending notification to BLUE LED task]\n");
                        fflush(stdout);
                        xTaskNotifyGive(blue_led_task_handle);
                    } else {
                        printf("[debug: BLUE LED task handle is NULL]\n");
                        fflush(stdout);
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


int main()
{
    stdio_init_all();

    xTaskCreate(input_task, "INPUT_Task", 256, NULL, 1, NULL);
    xTaskCreate(blue_action_led, "Blue_LED_Task", 256, NULL, 1, NULL);

    //xTaskCreate(serial_test, "serial test", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while(1){};

}
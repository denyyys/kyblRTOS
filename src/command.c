#include "FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "task.h"
#include "queue.h"
#include "command.h"
#include "boot.h"

extern QueueHandle_t lcd_queue;

// Define a struct for command dictionary entries
typedef struct {
    char *name;
    void (*func)(char *args);
} Command;

// Command dictionary
Command commands[] = {
    {"help", help},
    {"echo", echo},
    {"ver", ver},
    {"clear", clear},
    {"bin", bin},
    {"lcd", lcd_command},
    {NULL, NULL} // Sentinel value to mark the end of the array
};

// Function implementations
void help(char *args) {
    printf("available commands:\n");
    printf("    lcd <argument>: [lcd 'message'] / [lcd stop]");
    printf("    echo <argument>: prints back the argument\n");
    printf("    bin <number>: display number in binary on LED's\n");
    printf("    clear: clear the screen\n");
    printf("    ver: display installed version\n");
    printf("    exit: shutdown the OS\n");
}

void echo(char *args) {
    if (args != NULL) {
        printf("%s\n", args);
    } else {
        printf("\n");
    }
}

void clear(char *args) {
    printf("\033[2J\033[H");
}

void ver(char *args) {
    if(args != NULL && strcmp(args,"skero") == 0){
        printf("hulim hhc rn\n");
    } else {
    printf("kyblRTOS Indev 0.2.4\n");
    }
}

void bin(char *args) {
    int number = atoi(args);

    if(number < 0 || number > 15){
        printf("error: number must be 0 - 15");
        return;
    }

    gpio_put(GPIO15, (number & (1 << 3)) ? 1 : 0); // MSB - GPIO15
    gpio_put(GPIO14, (number & (1 << 2)) ? 1 : 0);
    gpio_put(GPIO13, (number & (1 << 1)) ? 1 : 0);
    gpio_put(GPIO12, (number & (1 << 0)) ? 1 : 0); // LSB - GPIO12

    printf("executed.");
}

void lcd_command(char *args) {

    if (args != NULL) {
        char message[50];
        snprintf(message, sizeof(message), "%s", args);

        // Check if the command is to stop the loop
        if (strcmp(message, "stop") == 0) {
            xQueueSend(lcd_queue, &message, portMAX_DELAY); // Send "stop" command
        } else {
            xQueueSend(lcd_queue, &message, portMAX_DELAY); // Send user message to display
        }
    } else {
        printf("Invalid command or no arguments provided.\n");
    }
}


// Execute command
int execute_command(const char *command, char *args) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(command, commands[i].name) == 0) {
            commands[i].func(args);
            return 1; // Command found and executed
        }
    }
    return 0; // Command not found
}
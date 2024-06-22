#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "command.h"

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
    {NULL, NULL} // Sentinel value to mark the end of the array
};

// Function implementations
void help(char *args) {
    printf("available commands:\n");
    printf("    echo <argument>: prints back the argument\n");
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
    printf("kyblRTOS Indev 0.1.12\n");
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
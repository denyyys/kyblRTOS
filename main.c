#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "command.h"

int main() {
    boot();

    char input[MAX_INPUT_LENGTH];

    while (1) {
        printf("# ");
        if (fgets(input, MAX_INPUT_LENGTH, stdin) == NULL) {
            break; // Break the loop if input is NULL (EOF)
        }

        if (input[strlen(input) - 1] == '\n') {
            input[strlen(input) - 1] = '\0';
        }

        // Split the input into command and arguments
        char *command = strtok(input, " ");
        char *arguments = strtok(NULL, "");

        // Handle empty input
        if (command == NULL) {
            continue;
        }

        // Special handling for the "exit" command
        if (strcmp(command, "exit") == 0) {
            break;
        }

        // Execute the command
        if (!execute_command(command, arguments)) {
            printf("unknown command.\n");
        }
    }

    return 0;
}

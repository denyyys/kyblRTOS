#ifndef COMMAND_H
#define COMMAND_H
#define MAX_INPUT_LENGTH 100

#include <string.h>

void help();
void echo(char *text);
void clear();
void ver();
int execute_command(const char *command, char *args);

#endif
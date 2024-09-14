#ifndef COMMAND_H
#define COMMAND_H
#define MAX_INPUT_LENGTH 100

#include <string.h>

void help();
void echo(char *text);
void clear();
void ver();
void bin();
void lcd_command(char *args); 
int execute_command(const char *command, char *args);

#endif
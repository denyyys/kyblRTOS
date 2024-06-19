#include "boot.h"

void memory(){
    system("cls");
    printf("booting");
    sleep(3);
    printf("......\n");
    sleep(1);
}

void welcome(){
    printf("boot successfull...\n\n");
    printf("Kybl Enterprise 2024\n");
    printf("-----------------\n");
    printf("kyblRTOS\n");
    printf("-----------------\n\n");
}

void boot(){
    memory();
    welcome();
}
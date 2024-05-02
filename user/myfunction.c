#include "user/user.h"

void perror(const char* str){
    printf("%s\n", str);
    exit(-1);
}
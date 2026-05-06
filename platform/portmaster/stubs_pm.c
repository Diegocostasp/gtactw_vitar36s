#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

void debugPrintf(char *text, ...) {
    va_list args;
    va_start(args, text);
    vprintf(text, args);
    va_end(args);
}

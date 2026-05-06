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

void so_flush_caches(void *mod) {
#if defined(__GNUC__)
    /*
     * Placeholder inicial.
     * Depois vamos limpar o range correto do módulo carregado.
     */
    __builtin___clear_cache((char *)0x60000000, (char *)0x70000000);
#endif
}

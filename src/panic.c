#include <stdio.h>
#include <stdlib.h>

#include "panic.h"

void sob_panic(const char * msg) {
    fputs("PANIC: ", stdout);
    fputs(msg, stdout);
    fputc('\n', stdout);
    exit(1);
}


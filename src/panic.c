#include <stdio.h>
#include <stdlib.h> /* for exit */
#include <stdarg.h>

#include "panic.h"

void sob_panic(const char * file, unsigned long line, const char * msg, ...) {
    va_list vl;
    va_start(vl, msg);

    fprintf(stderr, "PANIC (BUG!) @ %s:%lu : ", file, line);  /* no newline */
    vfprintf(stderr, msg, vl);
    fprintf(stderr, "\n");
    exit(-1);

    va_end(vl);
}


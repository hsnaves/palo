
#include <stdio.h>
#include <stdarg.h>
#include "common/utils.h"

/* Functions. */

void report_error(const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}


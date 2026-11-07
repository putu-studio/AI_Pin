#include <stdio.h>
#include <stdarg.h>
#include "app_out.h"
#include "app_display.h"

void out_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s", buf);
    fflush(stdout);
    app_display_append(buf);
}
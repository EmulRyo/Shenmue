#include <stdarg.h>
#include <stdio.h>
#include "core/log.h"

// Set before the threads start (an int, read by all of them)
static volatile LogLevel g_level = LOGLEVEL_DEBUG;

void Log_SetLevel(LogLevel level) {
    g_level = level;
}

LogLevel Log_GetLevel(void) {
    return g_level;
}

bool Log_Enabled(LogLevel level) {
    return level != LOGLEVEL_NONE && level <= g_level;
}

void Log_Write(LogLevel level, const char* format, ...) {
    if (!Log_Enabled(level))
        return;
    va_list args;
    va_start(args, format);
    vfprintf(level <= LOGLEVEL_WARNING ? stderr : stdout, format, args);
    va_end(args);
}

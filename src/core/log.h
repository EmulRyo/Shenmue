#ifndef __LOG_H__
#define __LOG_H__

#include <stdbool.h>

// Messages of the application to the console, by level: the errors and the warnings to stderr, the rest to stdout. The
// messages above the level are dropped (e.g. the tests keep only the errors and the warnings of the formats). The
// format is the one of printf, and the message ends its line with its own \n (a line can be written in several calls)

typedef enum {
    LOGLEVEL_NONE,
    LOGLEVEL_ERROR,      // Something failed: a file that can't be read, invalid data, a memory error
    LOGLEVEL_WARNING,    // Something is missing or skipped, but the work goes on
    LOGLEVEL_INFO,       // What was loaded or done
    LOGLEVEL_DEBUG,      // Details: headers and entries of the files
} LogLevel;

// The highest level written, LOGLEVEL_DEBUG at first
void        Log_SetLevel(LogLevel level);
LogLevel    Log_GetLevel(void);
bool        Log_Enabled(LogLevel level);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void        Log_Write(LogLevel level, const char* format, ...);

#define Log_Error(...)      Log_Write(LOGLEVEL_ERROR, __VA_ARGS__)
#define Log_Warning(...)    Log_Write(LOGLEVEL_WARNING, __VA_ARGS__)
#define Log_Info(...)       Log_Write(LOGLEVEL_INFO, __VA_ARGS__)
#define Log_Debug(...)      Log_Write(LOGLEVEL_DEBUG, __VA_ARGS__)

#endif

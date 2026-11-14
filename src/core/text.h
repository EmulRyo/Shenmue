#ifndef __TEXT_H__
#define __TEXT_H__

#include <stdbool.h>
#include <stdint.h>

// The same text, ignoring the case of the ASCII letters
bool        Text_EqualsIgnoreCase(const char* a, const char* b);
// Four characters of an ID (letters, digits, underscores), as stored in memory, e.g. 'AKIR'
bool        Text_IsFourCC(uint32_t value);
// A time as minutes, seconds and centiseconds, e.g. "1:05.20". In a buffer of TextFormat (raylib): valid until it is
// called 4 more times
const char* Text_FormatCentiseconds(uint32_t centiseconds);
const char* Text_FormatTime(float seconds);

#endif

#include <ctype.h>
#include <raylib.h>
#include "core/text.h"

bool Text_EqualsIgnoreCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

bool Text_IsFourCC(uint32_t value) {
    for (int i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)(value >> (i * 8));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z')))
            return false;
    }
    return true;
}

const char* Text_FormatCentiseconds(uint32_t centiseconds) {
    return TextFormat("%u:%02u.%02u", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

const char* Text_FormatTime(float seconds) {
    int centiseconds = (int)(seconds * 100.0f);
    return Text_FormatCentiseconds(centiseconds > 0 ? (uint32_t)centiseconds : 0);
}

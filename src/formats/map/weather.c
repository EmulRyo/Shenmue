#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/map/weather.h"

#define WEATHER_HEADER_SIZE 8
#define WEATHER_KEY_MASK    0xFFFFFFu   // Of the keys of the curves, without their intensity
#define WEATHER_DAY_MASK    0xFFFF0000u // Of the entries of FCAS, without their events and unknown byte

static const char* const g_curveIds[WEATHER_CURVE_COUNT] = { "CLUD", "RAIN", "SNOW" };

bool WEATHER_Read(const uint8_t* data, size_t size, WEATHER_File* file) {
    memset(file, 0, sizeof(*file));
    if (data == NULL || size < WEATHER_HEADER_SIZE || memcmp(data, "WDAT", 4) != 0)
        return false;
    bool found[WEATHER_CURVE_COUNT + 1] = { false };
    for (size_t pos = WEATHER_HEADER_SIZE; pos + WEATHER_HEADER_SIZE <= size;) {
        uint32_t chunkSize = Bytes_ReadU32(data + pos + 4);
        if (chunkSize < WEATHER_HEADER_SIZE || chunkSize > size - pos || (chunkSize - WEATHER_HEADER_SIZE) % 4 != 0)
            return false;
        uint32_t count = (chunkSize - WEATHER_HEADER_SIZE) / 4;
        const uint8_t* entries = data + pos + WEATHER_HEADER_SIZE;
        for (int c = 0; c < WEATHER_CURVE_COUNT; c++) {
            if (memcmp(data + pos, g_curveIds[c], 4) == 0) {
                file->keyCounts[c] = count;
                file->keys[c] = entries;
                found[c] = true;
            }
        }
        if (memcmp(data + pos, "FCAS", 4) == 0) {
            file->dayCount = count;
            file->days = entries;
            found[WEATHER_CURVE_COUNT] = true;
        }
        pos += chunkSize;
    }
    for (int c = 0; c <= WEATHER_CURVE_COUNT; c++) {
        if (!found[c])
            return false;
    }
    file->size = size;
    return true;
}

bool WEATHER_Load(const char* filepath, WEATHER_File* file) {
    size_t size = 0;
    uint8_t* data = File_LoadToMemoryDecompressed(filepath, &size);
    if (data == NULL || !WEATHER_Read(data, size, file)) {
        free(data);
        memset(file, 0, sizeof(*file));
        return false;
    }
    file->data = data;
    return true;
}

void WEATHER_Free(WEATHER_File* file) {
    free(file->data);
    memset(file, 0, sizeof(*file));
}

static uint32_t DayKey(const WEATHER_Date* date) {
    return (uint32_t)(date->year - WEATHER_BASE_YEAR) << 26 | (uint32_t)date->month << 21 | (uint32_t)date->day << 16;
}

static void DayOfEntry(uint32_t entry, WEATHER_Date* date) {
    *date = (WEATHER_Date) { WEATHER_BASE_YEAR + (int)(entry >> 26), (int)(entry >> 21) & 15, (int)(entry >> 16) & 31, 0, 0 };
}

bool WEATHER_Day(const WEATHER_File* file, const WEATHER_Date* date, uint8_t* events, uint8_t* unknown) {
    if (date->year < WEATHER_BASE_YEAR)
        return false;
    uint32_t key = DayKey(date);
    for (uint32_t i = 0; i < file->dayCount; i++) {
        uint32_t entry = Bytes_ReadU32(file->days + i * 4);
        if ((entry & WEATHER_DAY_MASK) == key) {
            if (events != NULL)
                *events = (uint8_t)(entry >> 8);
            if (unknown != NULL)
                *unknown = (uint8_t)entry;
            return true;
        }
    }
    return false;
}

bool WEATHER_FirstDay(const WEATHER_File* file, WEATHER_Date* date) {
    if (file->dayCount == 0)
        return false;
    DayOfEntry(Bytes_ReadU32(file->days), date);
    return true;
}

bool WEATHER_LastDay(const WEATHER_File* file, WEATHER_Date* date) {
    if (file->dayCount == 0)
        return false;
    DayOfEntry(Bytes_ReadU32(file->days + (file->dayCount - 1) * 4), date);
    return true;
}

float WEATHER_Intensity(const WEATHER_File* file, WEATHER_Curve curve, const WEATHER_Date* date) {
    if (curve < 0 || curve >= WEATHER_CURVE_COUNT || date->year < WEATHER_BASE_YEAR)
        return 0.0f;
    uint32_t time = (uint32_t)(date->year - WEATHER_BASE_YEAR) << 20 | (uint32_t)date->month << 16 |
        (uint32_t)date->day << 11 | (uint32_t)date->hour << 6 | (uint32_t)date->minute;
    const uint8_t* keys = file->keys[curve];
    // The first two keys around the time (a few keys of CLUD and RAIN are out of order)
    for (uint32_t i = 0; i + 1 < file->keyCounts[curve]; i++) {
        uint32_t a = Bytes_ReadU32(keys + i * 4), b = Bytes_ReadU32(keys + (i + 1) * 4);
        uint32_t from = a & WEATHER_KEY_MASK, to = b & WEATHER_KEY_MASK;
        if (from <= time && time < to) {
            float t = (float)(time - from) / (float)(to - from);
            return ((a >> 24) + ((float)(b >> 24) - (float)(a >> 24)) * t) / 255.0f;
        }
    }
    return 0.0f;
}

static bool IsLeap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int WEATHER_DaysInMonth(int year, int month) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1] + (month == 2 && IsLeap(year));
}

int WEATHER_Weekday(const WEATHER_Date* date) {
    // Sakamoto's method
    static const int offsets[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int year = date->month < 3 ? date->year - 1 : date->year;
    int month = date->month >= 1 && date->month <= 12 ? date->month : 1;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + date->day) % 7;
}

void WEATHER_AddDays(WEATHER_Date* date, int days) {
    for (; days > 0; days--) {
        if (++date->day > WEATHER_DaysInMonth(date->year, date->month)) {
            date->day = 1;
            if (++date->month > 12) {
                date->month = 1;
                date->year++;
            }
        }
    }
    for (; days < 0; days++) {
        if (--date->day < 1) {
            if (--date->month < 1) {
                date->month = 12;
                date->year--;
            }
            date->day = WEATHER_DaysInMonth(date->year, date->month);
        }
    }
}

int WEATHER_CompareDays(const WEATHER_Date* a, const WEATHER_Date* b) {
    if (a->year != b->year)
        return a->year - b->year;
    if (a->month != b->month)
        return a->month - b->month;
    return a->day - b->day;
}

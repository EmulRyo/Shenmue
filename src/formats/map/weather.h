#ifndef __WEATHER_H__
#define __WEATHER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// WEATHER.BIN (misc/coldinit, misc/cold_s and sprite/weather.bin): the weather and the calendar of the game, from
// 29 November 1986. A WDAT header (8 bytes) and four chunks (ID and size, header included): CLUD, RAIN and SNOW are
// curves of the clouds, the rain and the falling snow (keys with a date, an hour and an intensity); FCAS has an entry per
// day with the events of the day, which the scene programs read to choose the variants of their map (the function 0x176
// of the game, operation 12): snow on the ground, Christmas, New Year

#define WEATHER_EVENT_SNOW      0x02    // Snow on the ground (the day of a heavy snowfall and a few days after it)
#define WEATHER_EVENT_CHRISTMAS 0x04    // 15 to 25 December
#define WEATHER_EVENT_NEW_YEAR  0x08    // 1 to 5 January
#define WEATHER_BASE_YEAR       1986    // Year 0 of the keys

typedef enum {
    WEATHER_CLOUDS,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_CURVE_COUNT,
} WEATHER_Curve;

typedef struct {
    int year;       // e.g. 1986
    int month;      // 1 to 12
    int day;        // 1 to 31
    int hour;       // 0 to 23
    int minute;     // 0 to 59
} WEATHER_Date;

typedef struct {
    uint8_t*        data;
    size_t          size;
    uint32_t        keyCounts[WEATHER_CURVE_COUNT];
    const uint8_t*  keys[WEATHER_CURVE_COUNT];  // uint32 each: intensity << 24 | year << 20 | month << 16 | day << 11 |
                                                // hour << 6 | minute (year 0 is 1986)
    uint32_t        dayCount;
    const uint8_t*  days;                       // FCAS: uint32 each: year << 26 | month << 21 | day << 16 | events << 8 |
                                                // unknown byte
} WEATHER_File;

// Reads the file from memory (it keeps the pointer: data must outlive the file, and isn't freed by WEATHER_Free)
bool WEATHER_Read(const uint8_t* data, size_t size, WEATHER_File* file);
// Loads a file (gzipped or not)
bool WEATHER_Load(const char* filepath, WEATHER_File* file);
void WEATHER_Free(WEATHER_File* file);
// The FCAS entry of a day: its events (WEATHER_EVENT_*) and its unknown byte. Returns false if the day has none
bool WEATHER_Day(const WEATHER_File* file, const WEATHER_Date* date, uint8_t* events, uint8_t* unknown);
// The first and the last day of FCAS. Return false if it is empty
bool WEATHER_FirstDay(const WEATHER_File* file, WEATHER_Date* date);
bool WEATHER_LastDay(const WEATHER_File* file, WEATHER_Date* date);
// The intensity of a curve at a date (0 to 1): the keys around it interpolated linearly, as the game does with their
// packed values. 0 outside the curve
float WEATHER_Intensity(const WEATHER_File* file, WEATHER_Curve curve, const WEATHER_Date* date);

// The calendar
int WEATHER_DaysInMonth(int year, int month);
// 0 (Sunday) to 6 (Saturday)
int WEATHER_Weekday(const WEATHER_Date* date);
// Moves the date by a number of days (negative to go back), keeping the hour
void WEATHER_AddDays(WEATHER_Date* date, int days);
// Compares the days of two dates (not the hours): negative, 0 or positive
int WEATHER_CompareDays(const WEATHER_Date* a, const WEATHER_Date* b);

#endif

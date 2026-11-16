#ifndef __SCNSKY_H__
#define __SCNSKY_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/map/mapinfo.h"
#include "formats/map/weather.h"
#include "scenario/scnData.h"

// The sky of a map: a panorama of the sprite folder (SCROLLnn.SPR, whose texture airNN covers 90 degrees around the
// camera and 45 degrees from the horizon down and up), drawn behind the map. The map gives it in its SCRL chunk, or its
// program loads it (ScrollSprite, 0x0B4), or the program lets the game choose it by the hour and the weather of
// WEATHER.BIN (0x175, operation 27): the weather module of the game (WAIR, "AppendWeatherAir()" in 1ST_READ.BIN)

#define SCNSKY_CHOOSE           27      // Operation of SD_GAME_SKY that chooses the sky by the hour and the weather
#define SCNSKY_FILE_SIZE        16      // e.g. "SCROLL61.SPR"

typedef enum {
    SCNSKY_NONE,        // The map has no sky
    SCNSKY_CHUNK,       // The SCRL chunk of its mapinfo.bin (-1 for none)
    SCNSKY_PROGRAM,     // The first SCROLLnn.SPR file that its program loads with ScrollSprite (0x0B4)
    SCNSKY_WEATHER,     // Chosen by the game from the hour and the weather (0x175, operation 27)
} SCNSky_Source;

// The skies chosen by the weather module: by the hour, unless it rains, snows or is cloudy
typedef enum {
    SCNSKY_DAWN,        // 5:30 to 7:30, SCROLL60
    SCNSKY_DAY,         // 7:30 to 16:30, SCROLL61
    SCNSKY_SUNSET,      // 16:30 to 18:00, SCROLL66
    SCNSKY_NIGHT,       // 18:00 to 5:30, SCROLL63
    SCNSKY_RAIN,        // Rain or snowfall of 0.1 or more, SCROLL64
    SCNSKY_CLOUDY,      // Clouds above 0.25, SCROLL65
    SCNSKY_WEATHER_COUNT,
} SCNSky_Weather;

typedef struct {
    SCNSky_Source   source;
    char            file[SCNSKY_FILE_SIZE];     // Of SCNSKY_CHUNK and SCNSKY_PROGRAM, e.g. "SCROLL23.SPR"
} SCNSky_Map;

// The sky of a map, from what its program does (scnData.h, NULL if it has no program) and from its SCRL chunk
void SCNSky_Find(const MAPINFO_File* file, const SD_Data* program, SCNSky_Map* sky);
// The sky that the weather module chooses at a date: from the clouds, the rain and the snowfall of the weather (NULL:
// clear), then from the hour
SCNSky_Weather SCNSky_WeatherSky(const WEATHER_File* weather, const WEATHER_Date* date);
// Its file, e.g. "SCROLL61.SPR"
const char* SCNSky_WeatherFile(SCNSky_Weather sky);
// Its name, e.g. "day"
const char* SCNSky_WeatherName(SCNSky_Weather sky);

#endif

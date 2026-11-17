#ifndef __SCNSKY_H__
#define __SCNSKY_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/map/mapinfo.h"
#include "formats/map/weather.h"
#include "scenario/scnData.h"

// The sky of a map: a panorama of the sprite folder (SCROLLnn.SPR, whose texture airNN covers 90 degrees around the
// camera and 45 degrees high, from just below the horizon, see skyRender.h), drawn behind the map. The map gives it in
// its SCRL chunk, or its program loads it (ScrollSprite, 0x0B4), or the program lets the game choose it by the hour and
// the weather of WEATHER.BIN (0x175, operation 27): the weather module of the game (WAIR, "AppendWeatherAir()" in
// 1ST_READ.BIN).
// Every frame, that module also sets the fog, the colors of the background and the light of the map from its tables of
// the hours, mixed by the weather, when the program lets it (0x175, operations 23 to 28: SCNSKY_AIR_*)

#define SCNSKY_CHOOSE           27      // Operation of SD_GAME_SKY that chooses the sky by the hour and the weather
#define SCNSKY_FILE_SIZE        16      // e.g. "SCROLL61.SPR"
// What the weather module sets every frame (bits of its flags, set by the operations 23 to 28 of SD_GAME_SKY with 1)
#define SCNSKY_AIR_LIGHT        0x0C    // Operation 25: the ambient light and the intensity of the light 0
#define SCNSKY_AIR_FOG          0x780   // Operation 23: the fog (start, end, density, color)
#define SCNSKY_AIR_BACK         0x1800  // Operation 24: the 4 colors of the background of the sky

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
    int             fogSet;                     // The set of the FOG chunk that the program applies (0x128), the
                                                // first one if several, -1 if none
    float           offset;                     // Degrees added to the angle of the camera from the horizon to place
                                                // the sky (the SCOF chunk, 0 without it): the sky goes up when negative
    uint32_t        air;                        // SCNSKY_AIR_* that the weather module sets in the map: those that the
                                                // program enables, all of them with the sky of the weather (the game
                                                // keeps them from map to map: e.g. Dobuita from Sakuragaoka)
} SCNSky_Map;

// The fog of the tables of the hours of the weather module, as the game sets it: linear with the logarithm of the depth
// from nearDistance to farDistance (the fog table of the PowerVR, code at 0x0C0BEC2C), times density, at most 1
typedef struct {
    float           nearDistance;
    float           farDistance;
    float           density;
    uint32_t        color;      // RGB (the high byte is 0)
    uint32_t        top;        // ARGB colors of the background at the top and at the bottom of the screen
    uint32_t        bottom;
} SCNSky_Fog;

// An entry of a table of the hours (100 bytes in 1ST_READ.BIN PAL, tables at 0x0C2AFE60)
typedef struct {
    uint32_t        time;           // Seconds of the day
    float           direction[3];   // Always (0, -0.71, -0.71), in the coordinates of the game
    float           light[4];       // 1, red, green, blue
    float           ambient;        // Gray level of the ambient light (operation 25)
    float           intensity;      // Intensity of the light 0 (operation 25)
    uint32_t        colors[3];      // Unknown colors with signed bytes
    SCNSky_Fog      fog[2];         // The second one (thicker) with the fourth curve of the weather, which is always 0
} SCNSky_Key;

typedef enum {
    SCNSKY_TABLE_CLEAR,
    SCNSKY_TABLE_CLOUDS,
    SCNSKY_TABLE_RAIN,
    SCNSKY_TABLE_SNOW,
    SCNSKY_TABLE_COUNT,
} SCNSky_Table;

// The values of the tables of the hours at a date, mixed by the weather
typedef struct {
    float           weights[SCNSKY_TABLE_COUNT];    // Of each table, 1 in total
    SCNSky_Fog      fog;
    float           direction[3];
    float           light[3];       // Red, green, blue
    float           ambient;
    float           intensity;
} SCNSky_Air;

// The sky of a map, from what its program does (scnData.h, NULL if it has no program) and from its SCRL chunk
void SCNSky_Find(const MAPINFO_File* file, const SD_Data* program, SCNSky_Map* sky);
// The sky that the weather module chooses at a date: from the clouds, the rain and the snowfall of the weather (NULL:
// clear), then from the hour
SCNSky_Weather SCNSky_WeatherSky(const WEATHER_File* weather, const WEATHER_Date* date);
// Its file, e.g. "SCROLL61.SPR"
const char* SCNSky_WeatherFile(SCNSky_Weather sky);
// Its name, e.g. "day"
const char* SCNSky_WeatherName(SCNSky_Weather sky);
// The fog and the light of the weather module at a date (code at 0x0C1BEF96): the clouds, the rain and the snowfall of
// the weather (NULL: clear) count three times (at most 1), the clouds less by the rain or the snowfall from 6:00 to
// 18:00 (fully until 16:30), and the clear sky the rest; each table is interpolated at the hour
void SCNSky_AirAt(const WEATHER_File* weather, const WEATHER_Date* date, SCNSky_Air* air);
// The entries of a table
const SCNSky_Key* SCNSky_TableKeys(SCNSky_Table table, uint32_t* count);

#endif

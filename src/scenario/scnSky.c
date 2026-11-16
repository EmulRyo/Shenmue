#include <stdio.h>
#include <string.h>
#include "scenario/scnSky.h"

#define SCNSKY_GAME_SCROLL  0x0B4   // ScrollSprite: (0, folder, file) loads a scroll

// The files of the skies of the weather module, in SCNSky_Weather order (table at 0x0C2AFE80 in 1ST_READ.BIN PAL)
static const char* const g_weatherFiles[SCNSKY_WEATHER_COUNT] = {
    "SCROLL60.SPR", "SCROLL61.SPR", "SCROLL66.SPR", "SCROLL63.SPR", "SCROLL64.SPR", "SCROLL65.SPR",
};
static const char* const g_weatherNames[SCNSKY_WEATHER_COUNT] = { "dawn", "day", "sunset", "night", "rain or snowfall", "cloudy" };

// The limits of the skies of the hours, in minutes of the day (0x0C1BF2A4)
#define SCNSKY_DAWN_START   330     // 5:30
#define SCNSKY_DAY_START    450     // 7:30
#define SCNSKY_SUNSET_START 990     // 16:30
#define SCNSKY_NIGHT_START  1080    // 18:00
// The intensities of the weather that change the sky (0x0C1BE850)
#define SCNSKY_RAIN_LIMIT   0.1f    // Rain or snowfall from this one
#define SCNSKY_CLOUD_LIMIT  0.25f   // Clouds above this one

// SCROLLnn.SPR, ignoring the case
static bool IsScroll(const char* name) {
    char upper[SCNSKY_FILE_SIZE];
    size_t length = strlen(name);
    if (length != 12)
        return false;
    for (size_t i = 0; i <= length; i++)
        upper[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 'a' + 'A') : name[i];
    return strncmp(upper, "SCROLL", 6) == 0 && strcmp(upper + 8, ".SPR") == 0;
}

void SCNSky_Find(const MAPINFO_File* file, const SD_Data* program, SCNSky_Map* sky) {
    memset(sky, 0, sizeof(*sky));
    const SD_Reference* scroll = NULL;
    for (uint32_t i = 0; program != NULL && i < program->count; i++) {
        const SD_Reference* reference = &program->references[i];
        if (reference->kind == SD_SKY && reference->number == SCNSKY_CHOOSE) {
            sky->source = SCNSKY_WEATHER;
            return;
        }
        if (scroll == NULL && reference->kind == SD_FILE && reference->game == SCNSKY_GAME_SCROLL && IsScroll(reference->name))
            scroll = reference;
    }
    if (scroll != NULL) {
        sky->source = SCNSKY_PROGRAM;
        snprintf(sky->file, sizeof(sky->file), "%s", scroll->name);
        return;
    }
    uint32_t value;
    if (MAPINFO_ChunkValue(file, "SCRL", &value) && (int32_t)value >= 0 && (int32_t)value <= 99) {
        sky->source = SCNSKY_CHUNK;
        snprintf(sky->file, sizeof(sky->file), "SCROLL%02d.SPR", (int)value);
    }
}

SCNSky_Weather SCNSky_WeatherSky(const WEATHER_File* weather, const WEATHER_Date* date) {
    if (weather != NULL) {
        if (WEATHER_Intensity(weather, WEATHER_RAIN, date) >= SCNSKY_RAIN_LIMIT ||
            WEATHER_Intensity(weather, WEATHER_SNOW, date) >= SCNSKY_RAIN_LIMIT)
            return SCNSKY_RAIN;
        if (WEATHER_Intensity(weather, WEATHER_CLOUDS, date) > SCNSKY_CLOUD_LIMIT)
            return SCNSKY_CLOUDY;
    }
    int minutes = date->hour * 60 + date->minute;
    if (minutes < SCNSKY_DAWN_START || minutes >= SCNSKY_NIGHT_START)
        return SCNSKY_NIGHT;
    if (minutes < SCNSKY_DAY_START)
        return SCNSKY_DAWN;
    return minutes < SCNSKY_SUNSET_START ? SCNSKY_DAY : SCNSKY_SUNSET;
}

const char* SCNSky_WeatherFile(SCNSky_Weather sky) {
    return sky < SCNSKY_WEATHER_COUNT ? g_weatherFiles[sky] : "";
}

const char* SCNSky_WeatherName(SCNSky_Weather sky) {
    return sky < SCNSKY_WEATHER_COUNT ? g_weatherNames[sky] : "?";
}

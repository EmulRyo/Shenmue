#include <math.h>
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

// The bits of the flags of the weather module set by the operations 23 to 28 of SD_GAME_SKY (the operation 27 chooses
// the sky)
static const uint32_t g_airBits[6] = { SCNSKY_AIR_FOG, SCNSKY_AIR_BACK, SCNSKY_AIR_LIGHT, 0x70, 0, 0x0F };
#define SCNSKY_AIR_FIRST    23
#define SCNSKY_DAY_SECONDS  86400

// The tables of the hours of the weather module, copied from 1ST_READ.BIN PAL (0x0C2AEEC0 to 0x0C2AFE60): a clear sky,
// the clouds, the rain and the snowfall
static const SCNSky_Key g_clear[12] = {
    { 0, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.86f, 0.85f, 0.9f }, 0.34f, 0.35f, { 0x15B7B9BB, 0xA86B6770, 0xA0FFFFFF }, { { 12.0f, 800.0f, 0.95f, 0x000C0C0E, 0x00000000, 0x90000000 }, { 12.0f, 800.0f, 2.95f, 0x000C0C0E, 0x00000000, 0x90000000 } } },
    { 10800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.91f, 0.9f, 0.96f }, 0.32f, 0.4f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 800.0f, 0.95f, 0x000C0C0E, 0x00000000, 0x90000000 }, { 12.0f, 800.0f, 2.95f, 0x000C0C0E, 0x00000000, 0x90000000 } } },
    { 21600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.9f, 0.94f, 0.98f }, 0.3f, 0.7f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 800.0f, 1.0f, 0x005B6399, 0xFF3E18D8, 0x00000000 }, { 10.0f, 800.0f, 2.8f, 0x005B6399, 0xFF3E18D8, 0x00000000 } } },
    { 25200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.94f, 0.97f, 1.0f }, 0.3f, 0.7f, { 0x15B7B9BB, 0xA8ABA7A0, 0xA0FFFFFF }, { { 10.0f, 800.0f, 0.7f, 0x006D6B97, 0x820040FF, 0x00000000 }, { 10.0f, 800.0f, 2.7f, 0x006D6B97, 0x820040FF, 0x00000000 } } },
    { 28800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.94f, 0.97f, 1.0f }, 0.3f, 0.9f, { 0x15B7B9BB, 0xA8ABA7A0, 0xA0FFFFFF }, { { 10.0f, 800.0f, 0.7f, 0x00737781, 0x00000000, 0xFFFFFFFF }, { 10.0f, 800.0f, 2.7f, 0x00737781, 0x00000000, 0xFFFFFFFF } } },
    { 43200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 1.0f, 0.98f, 0.97f }, 0.35f, 1.0f, { 0x15B7B9BB, 0xA8ACA7B0, 0xA0FFFFFF }, { { 15.0f, 800.0f, 0.7f, 0x007B8193, 0x00000000, 0x8BFFFFFF }, { 15.0f, 800.0f, 2.7f, 0x007B8193, 0x00000000, 0x8BFFFFFF } } },
    { 57600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 1.0f, 0.98f, 0.97f }, 0.3f, 0.9f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 15.0f, 800.0f, 0.8f, 0x007B8193, 0x76000000, 0x7FFFDBC3 }, { 15.0f, 800.0f, 2.8f, 0x007B8193, 0x76000000, 0x7FFFDBC3 } } },
    { 61200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 1.0f, 0.7f, 0.59f }, 0.35f, 0.9f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 15.0f, 800.0f, 0.75f, 0x009E5D68, 0x388A2C1C, 0x50FFDBC3 }, { 15.0f, 800.0f, 2.75f, 0x009E5D68, 0x388A2C1C, 0x50FFDBC3 } } },
    { 64800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.93f, 0.77f, 0.75f }, 0.38f, 0.55f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.5f, 800.0f, 0.75f, 0x00533845, 0x355C2624, 0x80A38F83 }, { 12.5f, 800.0f, 2.75f, 0x00533845, 0x355C2624, 0x80A38F83 } } },
    { 68400, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.86f, 0.85f, 0.9f }, 0.4f, 0.45f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 800.0f, 0.8f, 0x00081222, 0x002A202D, 0x8030343D }, { 10.0f, 800.0f, 0.8f, 0x00081222, 0x002A202D, 0x8030343D } } },
    { 75600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.86f, 0.85f, 0.9f }, 0.38f, 0.35f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 800.0f, 0.95f, 0x00081222, 0x00202630, 0x80222830 }, { 10.0f, 800.0f, 0.95f, 0x00081222, 0x00202630, 0x80222830 } } },
    { 82800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.86f, 0.85f, 0.9f }, 0.36f, 0.35f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 800.0f, 0.95f, 0x000C0C0E, 0x00000000, 0x90000000 }, { 12.0f, 800.0f, 2.95f, 0x000C0C0E, 0x00000000, 0x90000000 } } },
};
static const SCNSky_Key g_clouds[10] = {
    { 0, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.82f, 0.8f, 0.84f }, 0.34f, 0.35f, { 0x15B7B9BB, 0x905B5C5D, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.71f, 0x00212325, 0xA9000000, 0xF7000002 }, { 12.0f, 600.0f, 2.71f, 0x00212325, 0xA9000000, 0xF7000002 } } },
    { 10800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.82f, 0.8f, 0.84f }, 0.32f, 0.35f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.71f, 0x00212325, 0xA9000000, 0xF7000002 }, { 12.0f, 600.0f, 2.71f, 0x00212325, 0xA9000000, 0xF7000002 } } },
    { 18000, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.94f, 0.99f, 1.0f }, 0.33f, 0.45f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.51f, 0x002F2F31, 0xA9000000, 0xE10E000A }, { 12.0f, 600.0f, 2.51f, 0x002F2F31, 0xA9000000, 0xE10E000A } } },
    { 25200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.97f, 1.0f, 1.0f }, 0.35f, 0.8f, { 0x15B7B9BB, 0xA8ABA7A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.67f, 0x004F5155, 0x5D000000, 0xC822262C }, { 12.0f, 600.0f, 2.67f, 0x004F5155, 0x5D000000, 0xC822262C } } },
    { 43200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.35f, 0.9f, { 0x15B7B9BB, 0xA8ACA7B0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.71f, 0x004F5155, 0x3B000000, 0x5B000004 }, { 12.0f, 600.0f, 2.71f, 0x004F5155, 0x1B000000, 0x5B000004 } } },
    { 57600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.98f, 0.98f, 0.99f }, 0.35f, 0.8f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.76f, 0x004F4F4F, 0x54000000, 0x573A1400 }, { 12.0f, 600.0f, 2.76f, 0x004F4F4F, 0x54000000, 0x573A1400 } } },
    { 61200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.87f, 0.84f, 0.78f }, 0.35f, 0.6f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.74f, 0x00312F27, 0x54000000, 0x75441416 }, { 12.0f, 600.0f, 2.74f, 0x00312F27, 0x54000000, 0x75441416 } } },
    { 68400, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.82f, 0.8f, 0.84f }, 0.4f, 0.45f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.66f, 0x00212325, 0xA9000000, 0xF7000002 }, { 12.0f, 600.0f, 2.66f, 0x00212325, 0xA9000000, 0xF7000002 } } },
    { 75600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.82f, 0.8f, 0.84f }, 0.38f, 0.35f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.68f, 0x00212325, 0xA9000000, 0xF7000002 }, { 12.0f, 600.0f, 2.68f, 0x00212325, 0xA9000000, 0xF7000002 } } },
    { 82800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.82f, 0.8f, 0.84f }, 0.36f, 0.35f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.71f, 0x00212325, 0xA9000000, 0xF7000002 }, { 12.0f, 600.0f, 2.71f, 0x00212325, 0xA9000000, 0xF7000002 } } },
};
static const SCNSky_Key g_rain[9] = {
    { 0, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.76f, 0.78f }, 0.3f, 0.45f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 }, { 10.0f, 600.0f, 2.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 } } },
    { 10800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.76f, 0.78f }, 0.3f, 0.45f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 }, { 10.0f, 600.0f, 2.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 } } },
    { 18000, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.92f, 0.91f, 1.0f }, 0.33f, 0.55f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.87f, 0x0031313B, 0xFF000000, 0x5E000006 }, { 10.0f, 600.0f, 2.87f, 0x0031313B, 0xFF000000, 0x5E000006 } } },
    { 25200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.97f, 0.99f, 1.0f }, 0.36f, 0.75f, { 0x15B7B9BB, 0xA8ABA7A0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.9f, 0x00303842, 0x92000000, 0xCC2D3947 }, { 10.0f, 600.0f, 2.9f, 0x00303842, 0x92000000, 0xCC2D3947 } } },
    { 43200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.97f, 0.99f, 1.0f }, 0.36f, 0.8f, { 0x15B7B9BB, 0xA8ACA7B0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.94f, 0x005A5E6E, 0x61000000, 0xBF757783 }, { 10.0f, 600.0f, 2.94f, 0x005A5E6E, 0x61000000, 0xBF757783 } } },
    { 57600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.9f, 0.91f, 0.93f }, 0.36f, 0.75f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.86f, 0x002A2E3A, 0xB9020000, 0xC72D333B }, { 10.0f, 600.0f, 2.86f, 0x002A2E3A, 0xB9020000, 0xC72D333B } } },
    { 68400, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.76f, 0.78f }, 0.35f, 0.5f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.8f, 0x00172227, 0xFF000000, 0xB5000002 }, { 10.0f, 600.0f, 2.8f, 0x00172227, 0xFF000000, 0xB5000002 } } },
    { 75600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.76f, 0.78f }, 0.34f, 0.45f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.85f, 0x00141422, 0xFF000000, 0xBF000002 }, { 10.0f, 600.0f, 2.85f, 0x00141422, 0xFF000000, 0xBF000002 } } },
    { 82800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.76f, 0.78f }, 0.32f, 0.45f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 }, { 10.0f, 600.0f, 2.85f, 0x000F0F1F, 0xFF000000, 0xBF000002 } } },
};
static const SCNSky_Key g_snow[9] = {
    { 0, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.77f, 0.81f }, 0.34f, 0.35f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.81f, 0x00212127, 0xF5000000, 0xBC000004 }, { 12.0f, 600.0f, 2.81f, 0x00212127, 0xF5000000, 0xBC000004 } } },
    { 10800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.77f, 0.81f }, 0.32f, 0.35f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.81f, 0x00212127, 0xF5000000, 0xBC000004 }, { 12.0f, 600.0f, 2.81f, 0x00212127, 0xF5000000, 0xBC000004 } } },
    { 18000, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.92f, 0.95f, 1.0f }, 0.34f, 0.5f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.83f, 0x0031313B, 0xFF000000, 0x5E000012 }, { 12.0f, 600.0f, 2.83f, 0x0031313B, 0xFF000000, 0x5E000012 } } },
    { 25200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.97f, 0.99f, 1.0f }, 0.36f, 0.87f, { 0x15B7B9BB, 0xA8ABA7B0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.85f, 0x00545A6E, 0x580A1032, 0x7F7080A0 }, { 12.0f, 600.0f, 2.85f, 0x00545A6E, 0x580A1032, 0x7F7080A0 } } },
    { 43200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.97f, 0.99f, 1.0f }, 0.36f, 0.95f, { 0x15B7B9BB, 0xA8ABA7B0, 0xA0FFFFFF }, { { 12.0f, 600.0f, 0.9f, 0x00828CA6, 0x001032F3, 0xAFFFFFFF }, { 12.0f, 600.0f, 2.9f, 0x00828CA6, 0x001032F3, 0xAFFFFFFF } } },
    { 61200, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.89f, 0.89f, 0.86f }, 0.35f, 0.85f, { 0x15B7B9BB, 0xA89B97A0, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.7f, 0x005B5F6F, 0x200E1032, 0x916E7E8C }, { 10.0f, 600.0f, 2.7f, 0x005B5F6F, 0x200E1032, 0x916E7E8C } } },
    { 68400, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.77f, 0.81f }, 0.4f, 0.45f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.8f, 0x0027292D, 0xE8000000, 0xBA000004 }, { 10.0f, 600.0f, 2.8f, 0x0027292D, 0xE8000000, 0xBA000004 } } },
    { 75600, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.77f, 0.81f }, 0.38f, 0.35f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.68f, 0x00232527, 0xF5000000, 0xBC000004 }, { 10.0f, 600.0f, 2.68f, 0x00232527, 0xF5000000, 0xBC000004 } } },
    { 82800, { 0.0f, -0.71f, -0.71f }, { 1.0f, 0.76f, 0.77f, 0.81f }, 0.36f, 0.35f, { 0x15B7B9BB, 0x908B8C8D, 0xA0FFFFFF }, { { 10.0f, 600.0f, 0.81f, 0x00212127, 0xF5000000, 0xBC000004 }, { 10.0f, 600.0f, 2.81f, 0x00212127, 0xF5000000, 0xBC000004 } } },
};

static const struct { const SCNSky_Key* keys; uint32_t count; } g_tables[SCNSKY_TABLE_COUNT] = {
    { g_clear, sizeof(g_clear) / sizeof(g_clear[0]) }, { g_clouds, sizeof(g_clouds) / sizeof(g_clouds[0]) },
    { g_rain, sizeof(g_rain) / sizeof(g_rain[0]) }, { g_snow, sizeof(g_snow) / sizeof(g_snow[0]) },
};

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
        if (reference->kind == SD_SKY && reference->number == SCNSKY_CHOOSE)
            sky->source = SCNSKY_WEATHER;
        int op = reference->number - SCNSKY_AIR_FIRST;
        if (reference->kind == SD_SKY && op >= 0 && op < 6 && reference->value != SD_UNKNOWN && reference->value != 0)
            sky->air |= g_airBits[op];
        if (scroll == NULL && reference->kind == SD_FILE && reference->game == SCNSKY_GAME_SCROLL && IsScroll(reference->name))
            scroll = reference;
    }
    if (sky->source == SCNSKY_WEATHER) {
        sky->air |= SCNSKY_AIR_FOG | SCNSKY_AIR_BACK | SCNSKY_AIR_LIGHT;
        return;
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

const SCNSky_Key* SCNSky_TableKeys(SCNSky_Table table, uint32_t* count) {
    *count = table < SCNSKY_TABLE_COUNT ? g_tables[table].count : 0;
    return table < SCNSKY_TABLE_COUNT ? g_tables[table].keys : NULL;
}

// A color from a to b: each byte interpolated
static uint32_t LerpColor(uint32_t a, uint32_t b, float t) {
    uint32_t color = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float value = (float)((a >> shift) & 0xFF) + ((float)((b >> shift) & 0xFF) - (float)((a >> shift) & 0xFF)) * t;
        color |= (uint32_t)fminf(fmaxf(value + 0.5f, 0.0f), 255.0f) << shift;
    }
    return color;
}

static SCNSky_Fog LerpFog(const SCNSky_Fog* a, const SCNSky_Fog* b, float t) {
    return (SCNSky_Fog) { a->nearDistance + (b->nearDistance - a->nearDistance) * t, a->farDistance + (b->farDistance - a->farDistance) * t,
        a->density + (b->density - a->density) * t, LerpColor(a->color, b->color, t), LerpColor(a->top, b->top, t),
        LerpColor(a->bottom, b->bottom, t) };
}

// A table at a second of the day: its entry before it and the next one interpolated, the last one going to the first
// one of the next day (0x0C1BEEF8)
static void TableAt(SCNSky_Table table, uint32_t seconds, SCNSky_Key* key) {
    const SCNSky_Key* keys = g_tables[table].keys;
    uint32_t count = g_tables[table].count, i = 0;
    while (i + 1 < count && keys[i + 1].time <= seconds)
        i++;
    const SCNSky_Key* a = &keys[i];
    const SCNSky_Key* b = &keys[i + 1 < count ? i + 1 : 0];
    uint32_t end = i + 1 < count ? b->time : SCNSKY_DAY_SECONDS + b->time;
    float t = end > a->time ? (float)(seconds - a->time) / (float)(end - a->time) : 0.0f;
    *key = *a;
    for (int c = 0; c < 3; c++)
        key->direction[c] = a->direction[c] + (b->direction[c] - a->direction[c]) * t;
    for (int c = 0; c < 4; c++)
        key->light[c] = a->light[c] + (b->light[c] - a->light[c]) * t;
    key->ambient = a->ambient + (b->ambient - a->ambient) * t;
    key->intensity = a->intensity + (b->intensity - a->intensity) * t;
    for (int f = 0; f < 2; f++)
        key->fog[f] = LerpFog(&a->fog[f], &b->fog[f], t);
}

void SCNSky_AirAt(const WEATHER_File* weather, const WEATHER_Date* date, SCNSky_Air* air) {
    memset(air, 0, sizeof(*air));
    float clouds = 0.0f, rain = 0.0f, snow = 0.0f;
    if (weather != NULL) {
        clouds = fminf(WEATHER_Intensity(weather, WEATHER_CLOUDS, date) * 3.0f, 1.0f);
        rain = fminf(WEATHER_Intensity(weather, WEATHER_RAIN, date) * 3.0f, 1.0f);
        snow = fminf(WEATHER_Intensity(weather, WEATHER_SNOW, date) * 3.0f, 1.0f);
    }
    // By day, the rain or the snowfall replace the clouds
    float hour = (float)date->hour * 100.0f + (float)date->minute * 100.0f / 60.0f;
    float day = hour < 600.0f || hour >= 1800.0f ? 0.0f : hour > 1650.0f ? (1800.0f - hour) / 150.0f : 1.0f;
    clouds = fmaxf(clouds - fmaxf(rain, snow) * day, 0.0f);
    float clear = 1.0f - fmaxf(clouds, fmaxf(rain, snow));
    float total = clear + clouds + rain + snow;
    float weights[SCNSKY_TABLE_COUNT] = { clear / total, clouds / total, rain / total, snow / total };
    uint32_t seconds = (uint32_t)(date->hour * 3600 + date->minute * 60) % SCNSKY_DAY_SECONDS;
    float fogColor[4] = { 0 }, top[4] = { 0 }, bottom[4] = { 0 };
    for (int t = 0; t < SCNSKY_TABLE_COUNT; t++) {
        air->weights[t] = weights[t];
        if (weights[t] <= 0.0f)
            continue;
        SCNSky_Key key;
        TableAt((SCNSky_Table)t, seconds, &key);
        const SCNSky_Fog* fog = &key.fog[0];
        float w = weights[t];
        for (int c = 0; c < 3; c++) {
            air->direction[c] += key.direction[c] * w;
            air->light[c] += key.light[c + 1] * w;
        }
        air->ambient += key.ambient * w;
        air->intensity += key.intensity * w;
        air->fog.nearDistance += fog->nearDistance * w;
        air->fog.farDistance += fog->farDistance * w;
        air->fog.density += fog->density * w;
        for (int c = 0; c < 4; c++) {
            fogColor[c] += (float)((fog->color >> (c * 8)) & 0xFF) * w;
            top[c] += (float)((fog->top >> (c * 8)) & 0xFF) * w;
            bottom[c] += (float)((fog->bottom >> (c * 8)) & 0xFF) * w;
        }
    }
    for (int c = 0; c < 4; c++) {
        air->fog.color |= (uint32_t)fminf(fogColor[c] + 0.5f, 255.0f) << (c * 8);
        air->fog.top |= (uint32_t)fminf(top[c] + 0.5f, 255.0f) << (c * 8);
        air->fog.bottom |= (uint32_t)fminf(bottom[c] + 0.5f, 255.0f) << (c * 8);
    }
    air->fog.color &= 0xFFFFFF;
}

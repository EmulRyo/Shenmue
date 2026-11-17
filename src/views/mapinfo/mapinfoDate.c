// The date at which the view of the maps shows a map: the events of the day and what the program of the map shows then
#include "views/mapinfo/mapinfoViewInternal.h"

// The hours of the date: dawn (the sky of 5:30 to 7:30), morning and afternoon (the shops are open from 8:30 to 18:30 in
// scene/01/d000), sunset (the sky of 16:30 to 18:00), evening (the lights are on from 19:00) and night
static const int g_hours[][2] = { { 6, 30 }, { 10, 0 }, { 15, 0 }, { 17, 0 }, { 19, 30 }, { 23, 0 } };
#define MAPINFODATE_DEFAULT_HOUR 1  // 10:00

static const char* const g_weekdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* const g_months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// The models of the map whose package the program loads: variants of the map (e.g. MAPN and MAPY, the snow, of
// scene/01/jd00), shown only if the program loads their package at the date
void MAPINFODate_FindVariants(void) {
    for (uint32_t i = 0; i < g_map.mapModelCount && g_map.mapModelVariants != NULL; i++) {
        const char* package = g_map.mapModelPackages[i];
        g_map.mapModelVariants[i] = package[0] != '\0' && MAPINFOItems_ProgramCount(SD_PACKAGE, -1, package) > 0;
    }
}

static void RequestDate(void) {
    if (g_map.hasDate)
        MapDate_Request(&g_mapOptions.gameDate);
}

// Sets the date, within the days of the weather, at the hour of g_mapOptions.hour
void MAPINFODate_Set(WEATHER_Date date) {
    const WEATHER_File* weather = MapDate_Weather();
    WEATHER_Date first, last;
    if (weather != NULL && WEATHER_FirstDay(weather, &first) && WEATHER_LastDay(weather, &last)) {
        if (WEATHER_CompareDays(&date, &first) < 0)
            date = first;
        if (WEATHER_CompareDays(&date, &last) > 0)
            date = last;
    }
    date.hour = g_hours[g_mapOptions.hour][0];
    date.minute = g_hours[g_mapOptions.hour][1];
    g_mapOptions.gameDate = date;
    RequestDate();
}

// The first time, the first day of the weather without events (an ordinary day: the game starts on 29 November 1986,
// with snow on the ground), or 2 December 1986 without weather
void MAPINFODate_Init(void) {
    const WEATHER_File* weather = MapDate_Weather();
    if (!g_mapOptions.dateSet) {
        WEATHER_Date date = { 1986, 12, 2, 0, 0 }, first, last;
        if (weather != NULL && WEATHER_FirstDay(weather, &first) && WEATHER_LastDay(weather, &last)) {
            for (date = first; WEATHER_CompareDays(&date, &last) < 0 && MapDate_Events(&date) != 0;)
                WEATHER_AddDays(&date, 1);
        }
        g_mapOptions.hour = MAPINFODATE_DEFAULT_HOUR;
        g_mapOptions.dateSet = true;
        MAPINFODate_Set(date);
        return;
    }
    RequestDate();
}

#define MAPINFODATE_HOURS (int)(sizeof(g_hours) / sizeof(g_hours[0]))

void MAPINFODate_NextHour(void) {
    g_mapOptions.hour = (g_mapOptions.hour + 1) % MAPINFODATE_HOURS;
    MAPINFODate_Set(g_mapOptions.gameDate);
}

// e.g. "6:30, 10:00, 15:00, 17:00, 19:30 and 23:00"
const char* MAPINFODate_Hours(void) {
    static char text[96];
    int length = 0;
    for (int i = 0; i < MAPINFODATE_HOURS && length < (int)sizeof(text); i++)
        length += snprintf(text + length, sizeof(text) - length, "%s%d:%02d", i == 0 ? "" : i == MAPINFODATE_HOURS - 1 ?
            " and " : ", ", g_hours[i][0], g_hours[i][1]);
    return text;
}

void MAPINFODate_Move(int days) {
    WEATHER_Date date = g_mapOptions.gameDate;
    WEATHER_AddDays(&date, days);
    MAPINFODate_Set(date);
}

// The next day whose events differ from those of the date, or the first day after the last one
void MAPINFODate_NextEvents(void) {
    const WEATHER_File* weather = MapDate_Weather();
    WEATHER_Date date = g_mapOptions.gameDate, first, last;
    if (weather == NULL || !WEATHER_FirstDay(weather, &first) || !WEATHER_LastDay(weather, &last))
        return;
    uint8_t events = MapDate_Events(&date);
    do
        WEATHER_AddDays(&date, 1);
    while (WEATHER_CompareDays(&date, &last) <= 0 && MapDate_Events(&date) == events);
    MAPINFODate_Set(WEATHER_CompareDays(&date, &last) > 0 ? first : date);
}

// The events of a day, separated by commas, with their short names or not
static const char* EventsText(uint8_t events, bool shortNames) {
    static char text[96];
    const char* names[3][2] = { { "snow on the ground", "snow" }, { "Christmas", "Christmas" }, { "New Year", "New Year" } };
    const uint8_t bits[3] = { WEATHER_EVENT_SNOW, WEATHER_EVENT_CHRISTMAS, WEATHER_EVENT_NEW_YEAR };
    int length = 0;
    text[0] = '\0';
    for (int i = 0; i < 3; i++) {
        if (events & bits[i])
            length += snprintf(text + length, sizeof(text) - length, "%s%s", length > 0 ? ", " : "", names[i][shortNames]);
    }
    return length > 0 ? text : shortNames ? "" : "none";
}

// e.g. "Thu 11 Dec 1986, snow"
const char* MAPINFODate_Text(const WEATHER_Date* date) {
    const char* events = EventsText(MapDate_Events(date), true);
    return TextFormat("%s %d %s %d%s%s", g_weekdays[WEATHER_Weekday(date)], date->day,
        g_months[date->month >= 1 && date->month <= 12 ? date->month - 1 : 0], date->year, events[0] != '\0' ? ", " : "", events);
}

// The file of the sky of the map (e.g. "SCROLL61.SPR"), at the date for those whose sky the weather chooses. NULL if
// it has none
const char* MAPINFODate_SkyFile(void) {
    switch (g_map.sky.source) {
    case SCNSKY_WEATHER:
        return SCNSky_WeatherFile(SCNSky_WeatherSky(MapDate_Weather(), &g_mapOptions.gameDate));
    case SCNSKY_CHUNK:
    case SCNSKY_PROGRAM:
        return g_map.sky.file;
    default:
        return NULL;
    }
}

// The fog and the light that the weather module sets in the map at the date, if it sets some (g_map.sky.air)
bool MAPINFODate_Air(SCNSky_Air* air) {
    if ((g_map.sky.air & (SCNSKY_AIR_FOG | SCNSKY_AIR_LIGHT | SCNSKY_AIR_BACK)) == 0)
        return false;
    SCNSky_AirAt(MapDate_Weather(), &g_mapOptions.gameDate, air);
    return true;
}

// The fog of the map at the date: the one of the weather module if it sets it, or else the set of the FOG chunk that the
// program applies (0x128). Returns false if none, or if it has no density
bool MAPINFODate_Fog(MAPINFO_Fog* fog) {
    SCNSky_Air air;
    if ((g_map.sky.air & SCNSKY_AIR_FOG) && MAPINFODate_Air(&air))
        *fog = (MAPINFO_Fog) { true, air.fog.nearDistance, air.fog.farDistance, air.fog.density, air.fog.color };
    else if (g_map.sky.fogSet >= 0 && (uint32_t)g_map.sky.fogSet < g_map.file.fogSetCount)
        *fog = g_map.file.fogSets[g_map.sky.fogSet];
    else
        return false;
    return fog->density > 0.0f && fog->farDistance > fog->nearDistance;
}

// e.g. "SCROLL64.SPR (rain or snowfall), chosen by the hour and the weather"
const char* MAPINFODate_SkyText(void) {
    switch (g_map.sky.source) {
    case SCNSKY_WEATHER:
        return TextFormat("%s (%s), chosen by the hour and the weather", MAPINFODate_SkyFile(),
            SCNSky_WeatherName(SCNSky_WeatherSky(MapDate_Weather(), &g_mapOptions.gameDate)));
    case SCNSKY_CHUNK:
        return TextFormat("%s, of the SCRL chunk", g_map.sky.file);
    case SCNSKY_PROGRAM:
        return TextFormat("%s, loaded by the program", g_map.sky.file);
    default:
        return "none";
    }
}

// The display of the program at the date, if it is applied. While the display of a new date is computed, the one of
// the previous date
static const SCNDisplay* DateDisplay(void) {
    return g_mapOptions.date && g_map.hasDate ? MapDate_Display() : NULL;
}

typedef enum {
    DATE_UNCHANGED,     // The program doesn't show or hide it at the start
    DATE_SHOWN,
    DATE_HIDDEN,
    DATE_VARIES,        // Shown or hidden by the paths of the program (e.g. after a flag of the game)
    DATE_NOT_LOADED,    // A variant whose package the program doesn't load at the date
} DateState;

static DateState MapModelDate(uint32_t model) {
    const SCNDisplay* display = DateDisplay();
    if (display == NULL || g_map.mapModelSlots == NULL || model >= g_map.mapModelCount)
        return DATE_UNCHANGED;
    if (g_map.mapModelVariants[model] && !SCNDisplay_Loads(display, g_map.mapModelPackages[model]))
        return DATE_NOT_LOADED;
    int slot = g_map.mapModelSlots[model];
    if (slot < 0)
        return DATE_UNCHANGED;
    switch (display->slots[slot]) {
    case SCNDISPLAY_SHOWN: return DATE_SHOWN;
    case SCNDISPLAY_HIDDEN: return DATE_HIDDEN;
    case SCNDISPLAY_VARIES: return DATE_VARIES;
    default: return DATE_UNCHANGED;
    }
}

bool MAPINFODate_IsHidden(uint32_t model) {
    DateState state = MapModelDate(model);
    return state == DATE_HIDDEN || state == DATE_NOT_LOADED;
}

const char* MAPINFODate_MapModelText(uint32_t model) {
    switch (MapModelDate(model)) {
    case DATE_SHOWN: return "shown by the program";
    case DATE_HIDDEN: return "hidden by the program";
    case DATE_VARIES: return "shown or hidden, depending on the paths of the program";
    case DATE_NOT_LOADED: return "not loaded: the program loads its package at other dates";
    default: return g_map.mapModelVariants[model] ? "loaded by the program" : "not changed by the program";
    }
}

// The date, its events and weather, and what the program shows then
void MAPINFODate_Properties(Properties* properties) {
    const WEATHER_Date* date = &g_mapOptions.gameDate;
    const WEATHER_File* weather = MapDate_Weather();
    Properties_Section(properties, "Date of the game");
    if (!g_map.hasDate) {
        Properties_Note(properties, "The program of the map has no bytecode to emulate (SCN file)");
        return;
    }
    Properties_Text(properties, "Date", TextFormat("%s %d %s %d, %02d:%02d", g_weekdays[WEATHER_Weekday(date)], date->day,
        g_months[date->month - 1], date->year, date->hour, date->minute));
    if (weather == NULL)
        Properties_Note(properties, "No WEATHER.BIN in misc/coldinit, misc/cold_s or sprite: no events");
    else {
        Properties_Text(properties, "Events", EventsText(MapDate_Events(date), false));
        Properties_Text(properties, "Clouds", TextFormat("%.0f%%", WEATHER_Intensity(weather, WEATHER_CLOUDS, date) * 100.0f));
        Properties_Text(properties, "Rain", TextFormat("%.0f%%", WEATHER_Intensity(weather, WEATHER_RAIN, date) * 100.0f));
        Properties_Text(properties, "Snowfall", TextFormat("%.0f%%", WEATHER_Intensity(weather, WEATHER_SNOW, date) * 100.0f));
    }
    if (g_map.sky.source == SCNSKY_WEATHER)
        Properties_Text(properties, "Sky", MAPINFODate_SkyText());
    SCNSky_Air air;
    if (MAPINFODate_Air(&air)) {
        Properties_Text(properties, "Tables of the hours", TextFormat("clear %.0f%%, clouds %.0f%%, rain %.0f%%, snowfall %.0f%%",
            air.weights[SCNSKY_TABLE_CLEAR] * 100.0f, air.weights[SCNSKY_TABLE_CLOUDS] * 100.0f,
            air.weights[SCNSKY_TABLE_RAIN] * 100.0f, air.weights[SCNSKY_TABLE_SNOW] * 100.0f));
        if (g_map.sky.air & SCNSKY_AIR_FOG)
            Properties_Text(properties, "Fog", TextFormat("%.1f to %.0f, density %.2f, #%06X", air.fog.nearDistance,
                air.fog.farDistance, air.fog.density, air.fog.color));
        if (g_map.sky.air & SCNSKY_AIR_BACK)
            Properties_Text(properties, "Background", TextFormat("#%08X at the top, #%08X at the bottom (ARGB)", air.fog.top,
                air.fog.bottom));
        if (g_map.sky.air & SCNSKY_AIR_LIGHT)
            Properties_Text(properties, "Light", TextFormat("ambient %.2f, light 0 %.2f", air.ambient, air.intensity));
    }
    const SCNDisplay* display = g_map.hasDate ? MapDate_Display() : NULL;
    if (!MapDate_Current())
        Properties_Text(properties, "Program", MapDate_Busy() ? "emulating..." : "not emulated");
    else if (display != NULL) {
        uint32_t counts[4] = { 0 };
        for (int s = 0; s < SCNDISPLAY_SLOTS; s++)
            counts[display->slots[s]]++;
        Properties_Text(properties, "MAP models", TextFormat("%u shown, %u hidden, %u depending on the paths",
            counts[SCNDISPLAY_SHOWN], counts[SCNDISPLAY_HIDDEN], counts[SCNDISPLAY_VARIES]));
        uint32_t variants = 0, loaded = 0;
        for (uint32_t i = 0; i < g_map.mapModelCount; i++) {
            variants += g_map.mapModelVariants[i];
            loaded += g_map.mapModelVariants[i] && SCNDisplay_Loads(display, g_map.mapModelPackages[i]);
        }
        if (variants > 0)
            Properties_Text(properties, "Variants", TextFormat("%u of %u models of packages loaded", loaded, variants));
        Properties_Text(properties, "Packages loaded", TextFormat("%u", display->packageCount));
        if (display->truncated > 0 || display->skipped > 0)
            Properties_Note(properties, "The emulation stopped at its limits: some paths weren't followed");
    }
    if (!g_mapOptions.date)
        Properties_Note(properties, "Not applied: Date is off in the toolbar");
}

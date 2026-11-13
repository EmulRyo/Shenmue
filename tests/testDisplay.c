#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/map/weather.h"
#include "formats/script/scn.h"
#include "formats/script/scnDisplay.h"
#include "test.h"

// WEATHER.BIN and the display of the maps by the scene programs at a date: the files have the 4 chunks, the events of
// FCAS are those of the wiki (snow on the ground, Christmas and New Year), and the emulation of the 136 SCN files at
// the dates of the events gives the variants of the maps that the wiki describes (e.g. MAPY with snow in
// scene/01/jd00, MAP04 hidden in betd)

static void CheckWeather(const char* content, const char* name, uint32_t days, int lastMonth, int lastDay) {
    char path[CORPUS_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/%s", content, name);
    WEATHER_File file;
    if (!WEATHER_Load(path, &file)) {
        CHECK(false, "%s: not read", name);
        return;
    }
    CHECK_EQ(file.dayCount, days, name);
    WEATHER_Date first, last;
    CHECK(WEATHER_FirstDay(&file, &first) && first.year == 1986 && first.month == 11 && first.day == 29, "%s: first day", name);
    CHECK(WEATHER_LastDay(&file, &last) && last.year == 1987 && last.month == lastMonth && last.day == lastDay, "%s: last day", name);
    // The events: bit 1 on 41 days, bit 2 from 15 to 25 December, bit 3 from 1 to 5 January, bit 0 never
    uint32_t counts[8] = { 0 };
    for (WEATHER_Date date = first; WEATHER_CompareDays(&date, &last) <= 0; WEATHER_AddDays(&date, 1)) {
        uint8_t events;
        CHECK(WEATHER_Day(&file, &date, &events, NULL), "%s: no entry for %d-%d-%d", name, date.year, date.month, date.day);
        for (int b = 0; b < 8; b++)
            counts[b] += (events >> b) & 1;
        bool christmas = date.month == 12 && date.day >= 15 && date.day <= 25;
        bool newYear = date.month == 1 && date.day <= 5;
        CHECK(((events & WEATHER_EVENT_CHRISTMAS) != 0) == christmas && ((events & WEATHER_EVENT_NEW_YEAR) != 0) == newYear,
            "%s: events 0x%X on %d-%d-%d", name, events, date.year, date.month, date.day);
    }
    CHECK_EQ(counts[0], 0, "days with the bit 0");
    CHECK_EQ(counts[1], 41, "days with snow on the ground");
    // It snows on the first day (the snow of the start of the game), and the day is a Saturday
    CHECK(WEATHER_Intensity(&file, WEATHER_SNOW, &(WEATHER_Date) { 1986, 11, 29, 6, 0 }) > 0.9f, "%s: snow on 29 November", name);
    CHECK_EQ(WEATHER_Weekday(&first), 6, "day of the week of 29 November 1986");
    WEATHER_Free(&file);
}

// The dates of the checks: a day without event, with snow, Christmas without snow, and New Year with snow
static const SCNDisplay_Time g_times[] = {
    { 1986, 12, 3, 12, 0, 0 },
    { 1986, 12, 11, 12, 0, WEATHER_EVENT_SNOW },
    { 1986, 12, 20, 12, 0, WEATHER_EVENT_CHRISTMAS },
    { 1987, 1, 1, 12, 0, WEATHER_EVENT_NEW_YEAR | WEATHER_EVENT_SNOW },
    { 1986, 12, 3, 23, 0, 0 },
};
#define TIME_COUNT (sizeof(g_times) / sizeof(g_times[0]))

static const char* SlotsText(const SCNDisplay* display) {
    static char text[256];
    int length = 0;
    text[0] = '\0';
    for (int s = 0; s < SCNDISPLAY_SLOTS && length < (int)sizeof(text); s++) {
        if (display->slots[s] != SCNDISPLAY_UNSET)
            length += snprintf(text + length, sizeof(text) - length, " %d%c", s, "-+x?"[display->slots[s]]);
    }
    return text;
}

static const char* PackagesText(const SCNDisplay* display) {
    static char text[512];
    int length = 0;
    text[0] = '\0';
    for (uint32_t p = 0; p < display->packageCount && length < (int)sizeof(text); p++)
        length += snprintf(text + length, sizeof(text) - length, " %s", display->packages[p]);
    return text;
}

static void CheckDisplays(const char* name, const SCNDisplay displays[TIME_COUNT]) {
    if (strcmp(name, "scenario/act01_jd00.scn") == 0) {
        CHECK(SCNDisplay_Loads(&displays[0], "MAPN") && !SCNDisplay_Loads(&displays[0], "MAPY") && !SCNDisplay_Loads(&displays[0], "MAPS"),
            "act01_jd00: MAPN without event");
        CHECK(SCNDisplay_Loads(&displays[1], "MAPY") && !SCNDisplay_Loads(&displays[1], "MAPN"), "act01_jd00: MAPY with snow");
        CHECK(SCNDisplay_Loads(&displays[3], "MAPS"), "act01_jd00: MAPS on New Year");
    }
    if (strcmp(name, "scenario/act01_d000.scn") == 0) {
        CHECK(SCNDisplay_Loads(&displays[0], "MAP0") && !SCNDisplay_Loads(&displays[0], "MAP2"), "act01_d000: MAP0");
        CHECK(SCNDisplay_Loads(&displays[1], "MAP1") && !SCNDisplay_Loads(&displays[1], "MAP0"), "act01_d000: MAP1 with snow");
        CHECK(SCNDisplay_Loads(&displays[2], "MAP2"), "act01_d000: MAP2 at Christmas");
        CHECK(SCNDisplay_Loads(&displays[3], "MAP3") && !SCNDisplay_Loads(&displays[3], "MAP2"), "act01_d000: MAP3 on New Year");
    }
    if (strcmp(name, "scenario/act01_jhd0.scn") == 0) {
        CHECK(SCNDisplay_Loads(&displays[0], "MPSU") && !SCNDisplay_Loads(&displays[0], "MPSN"), "act01_jhd0: MPSU");
        CHECK(SCNDisplay_Loads(&displays[1], "MPSN") && !SCNDisplay_Loads(&displays[1], "MPSU"), "act01_jhd0: MPSN with snow");
    }
    if (strcmp(name, "scenario/act01_betd.scn") == 0) {
        for (int s = 4; s <= 10; s++)
            CHECK_EQ(displays[0].slots[s], SCNDISPLAY_HIDDEN, "act01_betd: slots 4 to 10 hidden");
        CHECK_EQ(displays[0].slots[0], SCNDISPLAY_UNSET, "act01_betd: slot 0 (hidden at the end only)");
    }
    if (strcmp(name, "scenario/act01_dnoz.scn") == 0) {
        CHECK(displays[0].slots[8] == SCNDISPLAY_SHOWN && displays[0].slots[9] == SCNDISPLAY_HIDDEN, "act01_dnoz: slots 8, 9");
        CHECK(displays[1].slots[8] == SCNDISPLAY_HIDDEN && displays[1].slots[9] == SCNDISPLAY_SHOWN, "act01_dnoz: slots 8, 9 with snow");
    }
}

void Test_Display(const TestContext* context) {
    CheckWeather(context->content, "misc/coldinit/WEATHER.bin", 153, 4, 30);
    CheckWeather(context->content, "misc/cold_s/WEATHER.bin", 143, 4, 20);
    CHECK(SCNDisplay_SlotOfFile("MAP.MT5") == 0 && SCNDisplay_SlotOfFile("scene/01/betd/map04.mt5") == 4 &&
        SCNDisplay_SlotOfFile("MAP22.mt5") == 22 && SCNDisplay_SlotOfFile("MAPM.mt5") == -1 &&
        SCNDisplay_SlotOfFile("map01.chrm") == -1, "slots of the MAP model files");

    CorpusList list = Corpus_ScnFiles(context->content);
    uint32_t programs = 0, withSlots = 0, withEvents = 0, varying = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program scn;
        BC_Program program;
        if (data == NULL || !SCN_Read(data, size, &scn) || !BC_AnalyzeProgram(&program, &scn)) {
            free(data);
            continue;
        }
        SCNDisplay displays[TIME_COUNT];
        bool ok = true;
        for (uint32_t t = 0; t < TIME_COUNT && ok; t++)
            ok = SCNDisplay_Run(&program, &g_times[t], &displays[t]);
        CHECK(ok, "%s: emulation", list.files[i].name);
        if (ok) {
            programs++;
            bool different = false, slots = false;
            char first[512];
            snprintf(first, sizeof(first), "%s", PackagesText(&displays[0]));
            for (uint32_t t = 0; t < TIME_COUNT; t++) {
                different |= memcmp(displays[t].slots, displays[0].slots, sizeof(displays[0].slots)) != 0 ||
                    strcmp(PackagesText(&displays[t]), first) != 0;
                for (int s = 0; s < SCNDISPLAY_SLOTS; s++) {
                    slots |= displays[t].slots[s] != SCNDISPLAY_UNSET;
                    varying += displays[t].slots[s] == SCNDISPLAY_VARIES;
                }
            }
            withSlots += slots;
            withEvents += different;
            if (different) {
                for (uint32_t t = 0; t < TIME_COUNT; t++)
                    Test_Info("%s %d-%02d-%02d %02d:00:%s |%s", list.files[i].name, g_times[t].year, g_times[t].month,
                        g_times[t].day, g_times[t].hour, SlotsText(&displays[t]), PackagesText(&displays[t]));
            }
            CheckDisplays(list.files[i].name, displays);
        }
        BC_FreeProgram(&program);
        free(data);
    }
    Corpus_Free(&list);
    CHECK_EQ(programs, 136, "programs emulated");
    Test_Info("%u programs, %u show or hide MAP models at the start, %u change with the date or the hour; %u slots vary",
        programs, withSlots, withEvents, varying);
}

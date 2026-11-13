#include <stdio.h>
#include <string.h>
#include <raylib.h>
#include "core/thread.h"
#include "views/mapDate.h"
#include "views/mapProgram.h"

#define MAPDATE_PATH_SIZE 1200

// The WEATHER.BIN files of the game, from the content folder, in this order
static const char* const g_weatherFiles[] = { "misc/coldinit/WEATHER.bin", "misc/cold_s/WEATHER.bin", "sprite/weather.bin" };

typedef struct {
    MapBytecode     bytecode;
    char            content[MAPDATE_PATH_SIZE];     // Folder of the weather loaded
    WEATHER_File    weather;
    bool            hasWeather;
    // The emulation: the thread computes worker at workerTime; display is the result at displayTime
    Thread          thread;
    AtomicInt       done;
    SCNDisplay_Time workerTime;
    SCNDisplay      worker;
    bool            workerOk;
    SCNDisplay_Time displayTime;
    SCNDisplay      display;
    bool            displayValid;
    SCNDisplay_Time requested;
    bool            hasRequest;
} MapDateState;

static MapDateState g_date;

static void LoadWeather(const char* content) {
    if (g_date.hasWeather && strcmp(g_date.content, content) == 0)
        return;
    if (g_date.hasWeather)
        WEATHER_Free(&g_date.weather);
    g_date.hasWeather = false;
    snprintf(g_date.content, sizeof(g_date.content), "%s", content);
    for (size_t i = 0; i < sizeof(g_weatherFiles) / sizeof(g_weatherFiles[0]) && !g_date.hasWeather; i++) {
        char path[MAPDATE_PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", content, g_weatherFiles[i]);
        if (FileExists(path) && WEATHER_Load(path, &g_date.weather)) {
            g_date.hasWeather = true;
            printf("[MapDate]: weather of %s\n", path);
        }
    }
}

static void Join(void) {
    if (g_date.thread != NULL) {
        Thread_Join(g_date.thread);
        g_date.thread = NULL;
    }
}

bool MapDate_Open(const char* mapinfoPath, const MAPINFO_File* file) {
    MapDate_Close();
    char content[MAPDATE_PATH_SIZE];
    if (MapProgram_ContentFolder(mapinfoPath, content, sizeof(content)))
        LoadWeather(content);
    return MapProgram_LoadBytecode(mapinfoPath, file, &g_date.bytecode);
}

void MapDate_Close(void) {
    Join();
    MapProgram_FreeBytecode(&g_date.bytecode);
    g_date.displayValid = false;
    g_date.hasRequest = false;
    Atomic_Store(&g_date.done, 0);
}

bool MapDate_HasProgram(void) {
    return g_date.bytecode.loaded;
}

const WEATHER_File* MapDate_Weather(void) {
    return g_date.hasWeather ? &g_date.weather : NULL;
}

uint8_t MapDate_Events(const WEATHER_Date* date) {
    uint8_t events = 0;
    if (g_date.hasWeather && !WEATHER_Day(&g_date.weather, date, &events, NULL))
        events = 0;
    return events;
}

static void Worker(void* arg) {
    (void)arg;
    g_date.workerOk = SCNDisplay_Run(&g_date.bytecode.program, &g_date.workerTime, &g_date.worker);
    Atomic_Store(&g_date.done, 1);
}

static bool SameTime(const SCNDisplay_Time* a, const SCNDisplay_Time* b) {
    return a->year == b->year && a->month == b->month && a->day == b->day && a->hour == b->hour && a->minute == b->minute &&
        a->events == b->events;
}

static void Collect(void) {
    g_date.display = g_date.worker;
    g_date.displayTime = g_date.workerTime;
    g_date.displayValid = g_date.workerOk;
}

static void Start(void) {
    g_date.workerTime = g_date.requested;
    Atomic_Store(&g_date.done, 0);
    g_date.thread = Thread_Create(Worker, NULL);
    if (g_date.thread == NULL) {
        // In this thread
        Worker(NULL);
        Collect();
    }
}

void MapDate_Request(const WEATHER_Date* date) {
    if (!g_date.bytecode.loaded)
        return;
    g_date.requested = (SCNDisplay_Time) { date->year, date->month, date->day, date->hour, date->minute, MapDate_Events(date) };
    g_date.hasRequest = true;
    if (g_date.thread == NULL && !(g_date.displayValid && SameTime(&g_date.displayTime, &g_date.requested)))
        Start();
}

const SCNDisplay* MapDate_Display(void) {
    if (g_date.thread != NULL && Atomic_Load(&g_date.done)) {
        Join();
        Collect();
        if (g_date.hasRequest && !SameTime(&g_date.workerTime, &g_date.requested))
            Start();
    }
    return g_date.displayValid && g_date.hasRequest ? &g_date.display : NULL;
}

bool MapDate_Current(void) {
    return g_date.displayValid && g_date.hasRequest && SameTime(&g_date.displayTime, &g_date.requested);
}

bool MapDate_Busy(void) {
    return g_date.thread != NULL;
}

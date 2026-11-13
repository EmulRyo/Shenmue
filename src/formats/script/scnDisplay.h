#ifndef __SCNDISPLAY_H__
#define __SCNDISPLAY_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/scnBytecode.h"

// What a scene program shows of its map at a date and an hour of the game, found by the emulation of its bytecode
// (scnVm.h) in an environment that knows them: the function GetDate (0x059) writes the date and the hour, and Weather
// (0x176), operation 12, gives the events of the day (snow on the ground, Christmas, New Year: the FCAS chunk of
// WEATHER.BIN, see weather.h). With them, the programs show or hide the MAP models of the map (MapModelDisplay, 0x098:
// slot 0 is MAP.MT5 and slot n MAPnn.MT5) and load the packages of the variants of the map (Package, 0x13C, e.g. MAPN or
// MAPY, the snow, of scene/01/jd00).
// The display of a slot is the one the program leaves when its threads yield for the first time (their first frame):
// the last call to 0x098 for the slot on each path before it, the threads started last (StartThread) after the first
// ones. The packages are those loaded on any path, at any time

#define SCNDISPLAY_SLOTS        32
#define SCNDISPLAY_PACKAGES     64
#define SCNDISPLAY_NAME_SIZE    32
#define SCNDISPLAY_GET_DATE     0x059   // (address): writes year - 1900, month, day, day of the week, hour, minute, second
#define SCNDISPLAY_MAP_MODEL    0x098   // (slot, 1 / 0): shows or hides a MAP model
#define SCNDISPLAY_WEATHER      0x176   // (12, n): the events for n = 0, no event for n = 1, bit n - 2 of them for n = 2 to 5
#define SCNDISPLAY_PACKAGE      0x13C   // (0, 0, folder, name): loads a package
#define SCNDISPLAY_EVENTS_OP    12

typedef enum {
    SCNDISPLAY_UNSET,       // The program doesn't show or hide the slot at the start
    SCNDISPLAY_SHOWN,
    SCNDISPLAY_HIDDEN,
    SCNDISPLAY_VARIES,      // Shown on some paths and hidden on others (e.g. after a flag of the game), or by a value
                            // computed that the emulation doesn't know
} SCNDisplay_Slot;

typedef struct {
    int         year;       // e.g. 1986
    int         month;
    int         day;
    int         hour;
    int         minute;
    uint8_t     events;     // Of the day (WEATHER_EVENT_* of weather.h)
} SCNDisplay_Time;

typedef struct {
    uint8_t     slots[SCNDISPLAY_SLOTS];    // SCNDisplay_Slot of each MAP model
    uint32_t    slotCalls;                  // Calls to 0x098 run before the first yield, with a known slot
    uint32_t    packageCount;
    char        packages[SCNDISPLAY_PACKAGES][SCNDISPLAY_NAME_SIZE]; // Names of the packages loaded (e.g. "MAPN")
    uint32_t    threads;                    // Of the emulation: threads run, not run (limit of the program) and stopped
    uint32_t    skipped;                    // by a limit
    uint32_t    truncated;
    uint32_t    eventQueries;               // Calls to 0x176 with the operation 12 answered
    uint32_t    dateQueries;                // Calls to 0x059 answered
} SCNDisplay;

// Runs the program at a time. Returns false on memory error
bool SCNDisplay_Run(const BC_Program* program, const SCNDisplay_Time* time, SCNDisplay* display);
// The package was loaded (the name compared ignoring the case)
bool SCNDisplay_Loads(const SCNDisplay* display, const char* package);
// The slot of a MAP model file name ("MAP.MT5": 0, "map04.mt5": 4), -1 if it isn't one
int SCNDisplay_SlotOfFile(const char* name);

#endif

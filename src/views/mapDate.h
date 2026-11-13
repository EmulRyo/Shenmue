#ifndef __MAPDATE_H__
#define __MAPDATE_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/map/mapinfo.h"
#include "formats/map/weather.h"
#include "formats/script/scnDisplay.h"

// The date and the hour of the game at which the view of a map shows it: the events of the day come from the weather
// of the game (WEATHER.BIN of misc/coldinit, or of misc/cold_s or sprite), and what the program of the map shows then
// (its MAP models and the packages of its variants) from the emulation of its bytecode (scnDisplay.h), run in a thread
// when the date changes. One map at a time

// Loads the bytecode of the program of a map and the weather of its content folder. The file must outlive the module
// (MapDate_Close). Returns false if the map has no bytecode: the date can't be applied
bool MapDate_Open(const char* mapinfoPath, const MAPINFO_File* file);
// Waits for the emulation running, if any
void MapDate_Close(void);
bool MapDate_HasProgram(void);
// The weather, NULL if no WEATHER.BIN was found
const WEATHER_File* MapDate_Weather(void);
// The events of a day (WEATHER_EVENT_*), 0 without weather or if the day has none
uint8_t MapDate_Events(const WEATHER_Date* date);
// Asks for the display at a date: the emulation starts if it isn't the one computed or being computed
void MapDate_Request(const WEATHER_Date* date);
// The display at the date of the last request, or at the previous one while it is computed (see MapDate_Current). NULL
// before the first one or if it failed. Call it every frame: it collects the result of the thread and starts the next
// request
const SCNDisplay* MapDate_Display(void);
// The display is the one of the last request
bool MapDate_Current(void);
bool MapDate_Busy(void);

#endif

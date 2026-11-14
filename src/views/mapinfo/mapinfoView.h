#ifndef __MAPINFOVIEW_H__
#define __MAPINFOVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// 3D view of a mapinfo.bin: the models of the map (MAP*.mt5 files of the folder of the file and of its subfolders,
// where the PKF files of the map are extracted) and of the doors of the door table of the scene program, with the
// collisions and areas, the lights, the cameras and the characters of the file over them, and a list with the cameras,
// lights, characters, doors, cutscenes and other information. A camera of the list shows the map from it, and a cutscene
// of the SCEX chunk is played by the view of the cutscenes (authView.h), shown over this one until it is closed

// Loads a mapinfo.bin and the models of its map, replacing the current one, with the same layers and fly mode. Returns
// false if the file can't be loaded
bool MAPINFOView_Open(const char* filepath);
void MAPINFOView_Close(void);
bool MAPINFOView_IsOpen(void);
// Handles the input (click on the list: select a camera, light, character or door, V: free/map camera, mouse on the scene:
// rotate, pan and zoom the free camera, R: reset it, toggles: layers) and draws the view inside area
void MAPINFOView_Draw(Rectangle area);
// Fills the texts of the status bar
void MAPINFOView_Status(ViewStatus* status);
// Adds its actions to the toolbar (Enter: play the selected cutscene, P: show the scene program, see programView.h)
void MAPINFOView_Toolbar(void);
// Path of the open mapinfo.bin, NULL if the view isn't open
const char* MAPINFOView_Path(void);
// Draws the visible models of the map, of the doors and of the characters, as the view shows them (its layers and the
// eyes of its tree), from camera, inside a 3D mode begun by another view (the cutscenes of the map). The characters for
// which hidden returns true aren't drawn (hidden can be NULL). Does nothing if the view isn't open
void MAPINFOView_DrawMap(Camera3D camera, bool (*hidden)(const char* id));

#endif

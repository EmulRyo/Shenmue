#ifndef __MAPINFOVIEW_H__
#define __MAPINFOVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// 3D view of a mapinfo.bin: the models of the map (MAP*.mt5 files of the folder of the file and of its subfolders,
// where the PKF files of the map are extracted) with the collisions and areas, the lights, the cameras and the
// characters of the file over them, and a list with the cameras, lights, characters and other information. A camera of
// the list shows the map from it

// Loads a mapinfo.bin and the models of its map, replacing the current one. Returns false if the file can't be loaded
bool MAPINFOView_Open(const char* filepath);
void MAPINFOView_Close(void);
bool MAPINFOView_IsOpen(void);
// Handles the input (click on the list: select a camera, light or character, V: free/map camera, mouse on the scene:
// rotate, pan and zoom the free camera, R: reset it, toggles: layers) and draws the view inside area
void MAPINFOView_Draw(Rectangle area);

#endif

#ifndef __AUTHVIEW_H__
#define __AUTHVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Player of AUTH cutscenes: the list of commands, the camera and the characters in 3D (seen from a free camera or from
// the camera of the cutscene), and a timeline with the motions, voices and sounds. The names of the motions are taken
// from the MOTN files in the folder of the cutscene

// Loads a cutscene, replacing the current one. Returns false if the file can't be loaded
bool AUTHView_Open(const char* filepath);
void AUTHView_Close(void);
bool AUTHView_IsOpen(void);
// Handles the input (space: play/pause, left/right: previous/next command, home: start, V: free/cutscene camera, mouse
// on the scene: rotate, pan and zoom the free camera, R: reset it, click or drag on the timeline: seek) and draws the
// player inside area
void AUTHView_Draw(Rectangle area);

#endif

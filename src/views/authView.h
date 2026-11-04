#ifndef __AUTHVIEW_H__
#define __AUTHVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// Player of AUTH cutscenes: the list of commands, the camera and the characters in 3D (seen from a free camera or from
// the camera of the cutscene), and a timeline with the motions, voices and sounds. The names of the motions are taken
// from the MOTN files in the folder of the cutscene. The characters are drawn with their models (in their rest pose:
// the motions aren't played), found in the CHRT scripts of the folder, of the scene and of the map, and the voices and
// sound effects are played (see authSounds.h)

// Loads a cutscene, replacing the current one, with the same camera (free or of the cutscene) and fly mode. Returns
// false if the file can't be loaded
bool AUTHView_Open(const char* filepath);
void AUTHView_Close(void);
bool AUTHView_IsOpen(void);
// Handles the input (space: play/pause, left/right: previous/next command, home: start, V: free/cutscene camera, mouse
// on the scene: rotate, pan and zoom the free camera, R: reset it, click or drag on the timeline: seek) and draws the
// player inside area
void AUTHView_Draw(Rectangle area);
// Fills the texts of the status bar
void AUTHView_Status(ViewStatus* status);
// Adds its actions to the toolbar
void AUTHView_Toolbar(void);

#endif

#ifndef __AUTHVIEW_H__
#define __AUTHVIEW_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/script/scnData.h"
#include "views/views.h"

// Player of AUTH cutscenes: the list of commands, the camera and the characters in 3D (seen from a free camera or from
// the camera of the cutscene), and a timeline with the motions, voices and sounds. The cutscenes are AUTH files or the
// cutscenes of a mapinfo.bin (its SCEX chunk). The motions are taken from the MOTN files in the folder of the cutscene or
// of its map and from the common motions of the game (motion/motion.bin). The characters are drawn with their models, in
// the pose of their motions, found in the CHRT scripts of the folder (or of the PKF/PKS files of the map), of the scene
// and of the map, and the voices and sound effects are played (see authSounds.h)

// Loads a cutscene, replacing the current one, with the same camera (free or of the cutscene) and fly mode. With the
// option of the map, the view of its map (the mapinfo.bin of the parent folder) is opened behind it, and its models are
// drawn around the characters. Returns false if the file can't be loaded
bool AUTHView_Open(const char* filepath);
// Loads a cutscene of a mapinfo.bin (a TRCK chunk of its SCEX chunk, by its number among them), replacing the current
// one. Its map is the folder of the file: the motions and the models are also searched in the PKF/PKS files of the
// map, extracted or not. Opened from the view of the map, which stays open behind it: the view goes back to it when the
// cutscene is closed (backspace). Returns false if the cutscene can't be loaded
bool AUTHView_OpenMapCutscene(const char* filepath, uint32_t cutscene);
// Loads a cutscene that the program of a mapinfo.bin loads in the table of cutscenes of the game (an AUTH file of a
// package of its map, see mapProgram.h), as one of its cutscenes: its number is the slot of the table. Returns false if
// the cutscene can't be loaded
bool AUTHView_OpenMapSequence(const char* filepath, const SD_Reference* sequence);
// The open cutscene is one of a mapinfo.bin
bool AUTHView_IsMapCutscene(void);
// The view of the map of the open cutscene is open behind it: the view of the map that played it, or the one opened for
// an AUTH file (the mapinfo.bin of its parent folder) to show its models. Back (backspace) closes the cutscene and shows
// the map
bool AUTHView_UsesMap(void);
void AUTHView_Close(void);
bool AUTHView_IsOpen(void);
// Handles the input (space: play/pause, left/right: previous/next command, home: start, V: free/cutscene camera, mouse
// on the scene: rotate, pan and zoom the free camera, R: reset it, click or drag on the timeline: seek) and draws the
// player inside area
void AUTHView_Draw(Rectangle area);
// Fills the texts of the status bar
void AUTHView_Status(ViewStatus* status);
// Adds its actions to the toolbar (backspace: back to the map, M: show the map)
void AUTHView_Toolbar(void);

#endif

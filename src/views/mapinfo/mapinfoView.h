#ifndef __MAPINFOVIEW_H__
#define __MAPINFOVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "render/mt5Render.h"
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
// What another view changes of the map it draws (the program of the map during a cutscene). Each function can be NULL
typedef struct {
    // Fade of a character (MT5Render_Instance.fade): 0 drawn as the map draws it, 1 not drawn
    float   (*characterFade)(const char* id);
    // The MAP model of a slot (MapModelDisplay): changes shown, whether the map shows it at its date, and sets its fade
    void    (*mapModel)(int slot, bool* shown, float* fade);
    // The lights of the characters, instead of those of the group 0 of the map (the Lights toggle); NULL: from the camera
    const MT5Render_Lighting* lighting;
} MAPINFOView_Changes;

// Whether the characters are lit by the lights of the map (the Lights toggle), else from the camera (always for the
// exteriors)
bool MAPINFOView_LightsShown(void);
// Number of lights of the open map (of all its groups), 0 if the view isn't open
uint32_t MAPINFOView_LightCount(void);
// Whether a group of lights of the open map lights an exterior, which the game also lights by the hour and the weather
// (not known, so not lit by its lights)
bool MAPINFOView_IsOutdoorGroup(uint32_t group);
// The switched-on lights of a group of lights of the open map (its LGHT chunk of that number) as the view has them (they
// can be edited), into lights, with their IDs into ids (if not NULL), up to max, and its ambient color. Returns their
// number
uint32_t MAPINFOView_GroupLights(uint32_t group, float ambient[3], MT5Render_Light* lights, uint16_t* ids, uint32_t max);
// The light that the game gives to each person of the map (MAPINFOVIEW_CHARACTER_LIGHT, a white light switched off in
// most groups), from the camera: the gray that the game gives it times its intensity into color. Returns false if the
// group hasn't it
#define MAPINFOVIEW_CHARACTER_LIGHT 31
#define MAPINFOVIEW_CHARACTER_LIGHT_COLOR 0.3f
bool MAPINFOView_CharacterLight(uint32_t group, float color[3]);

// Draws the visible models of the map, of the doors and of the characters, as the view shows them (its layers and the
// eyes of its tree), from camera, inside a 3D mode begun by another view (the cutscenes of the map), with the changes of
// that view (NULL for none). Does nothing if the view isn't open
void MAPINFOView_DrawMap(Camera3D camera, const MAPINFOView_Changes* changes);
// Draws the sky of the open map seen from camera in area (pixels of the screen, with the aspect ratio of the camera), as
// the view shows it (its Sky toggle and its date), before a 3D mode begun by another view. Does nothing if the view isn't
// open or the map has no sky
void MAPINFOView_DrawSky(Camera3D camera, Rectangle area);

#endif

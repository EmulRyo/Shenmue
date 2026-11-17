#ifndef __SKYRENDER_H__
#define __SKYRENDER_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>

// Drawing of the sky of the maps behind them: the textures of a SCROLLnn.SPR file as the game places them around the
// camera (0x0C09A31C in 1ST_READ.BIN PAL, with the quads of the table at 0x0C28F4B8, in pixels of its 640x480 screen).
// The first texture is repeated every 90 degrees around the vertical axis (960 pixels per quarter turn), upside down
// (its first row at the bottom), over 480 pixels high, that is 45 degrees, its first row 5.625 degrees below the horizon
// (60 pixels): it covers -5.625 to 39.375 degrees. The second texture of the files with two (a band of clouds or stars)
// goes on above it, over 1255.5 pixels, that is 117.7 degrees, repeated 9 times (13.1 degrees each), upside down too. Below the first texture, the color of its
// first pixel fills the screen, as the game clears it with; above the textures, the color of the last pixel of the top
// one (the game keeps it, but clears with the first one). The game draws over the first texture a quad with the 4 colors
// of the background at its corners (0 and 1 at the top, 2 and 3 at the bottom): a gradient with their alpha; here the top
// color also covers the sky above the texture, the band included, so that there is no edge between them

typedef struct {
    Shader      shader;
    int         forwardLoc;
    int         rightLoc;
    int         upLoc;
    int         tangentsLoc;
    int         rowsLoc;
    int         belowLoc;
    int         aboveLoc;
    int         backTopLoc;
    int         backBottomLoc;
    int         bandLoc;
    int         hasBandLoc;
    int         offsetLoc;
    Texture     texture;        // id 0 if none is loaded
    Texture     band;           // The second texture, id 0 if none
    float       below[3];       // Color of the first pixel of the first texture
    float       above[3];       // Color of the last pixel of the top texture
    char        path[1024];     // Of the file loaded, or of the last one that failed
    bool        failed;
} SkyRender;

void SkyRender_Init(SkyRender* sky);
void SkyRender_Free(SkyRender* sky);
// Loads the textures of an SPR file, unless it is the one loaded. Returns false if the first one can't be loaded (the sky
// is then empty)
bool SkyRender_Load(SkyRender* sky, const char* path);
void SkyRender_Unload(SkyRender* sky);
// Draws the sky seen from the camera in area (pixels of the screen, with the aspect ratio of the camera), in 2D: before
// the scene. offset is added to the angle of the camera from the horizon (degrees, the SCOF chunk of the map: negative
// values raise the sky). backTop and backBottom are the ARGB colors of the background over the first texture, at its
// top and its bottom as drawn (alpha 0: none)
void SkyRender_Draw(const SkyRender* sky, Camera3D camera, Rectangle area, float offset, uint32_t backTop,
    uint32_t backBottom);

#endif

#ifndef __SKYRENDER_H__
#define __SKYRENDER_H__

#include <stdbool.h>
#include <raylib.h>

// Drawing of the sky of the maps behind them: the first texture of a SCROLLnn.SPR file as the game places it around the
// camera (0x0C09A31C in 1ST_READ.BIN PAL). The texture is repeated every 90 degrees around the vertical axis (the game
// moves it 960 pixels of its 640x480 screen per quarter turn), and covers 45 degrees up and down, its middle row on the
// horizon (480 pixels for the 45 degrees of the height of the screen); above and below it, its first and its last rows
// fill the screen, as the colors that the game clears it with

typedef struct {
    Shader      shader;
    int         forwardLoc;
    int         rightLoc;
    int         upLoc;
    int         tangentsLoc;
    int         rowsLoc;
    int         topLoc;
    int         bottomLoc;
    Texture     texture;        // id 0 if none is loaded
    float       top[3];         // Colors of its first and its last pixels
    float       bottom[3];
    char        path[1024];     // Of the file loaded, or of the last one that failed
    bool        failed;
} SkyRender;

void SkyRender_Init(SkyRender* sky);
void SkyRender_Free(SkyRender* sky);
// Loads the first texture of an SPR file, unless it is the one loaded. Returns false if it can't be loaded (the sky is
// then empty)
bool SkyRender_Load(SkyRender* sky, const char* path);
void SkyRender_Unload(SkyRender* sky);
// Draws the sky seen from the camera in area (pixels of the screen, with the aspect ratio of the camera), in 2D: before
// the scene
void SkyRender_Draw(const SkyRender* sky, Camera3D camera, Rectangle area);

#endif

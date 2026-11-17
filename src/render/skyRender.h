#ifndef __SKYRENDER_H__
#define __SKYRENDER_H__

#include <stdbool.h>
#include <raylib.h>

// Drawing of the sky of the maps behind them: the first texture of a SCROLLnn.SPR file as the game places it around the
// camera (0x0C09A31C in 1ST_READ.BIN PAL). The texture is repeated every 90 degrees around the vertical axis (the game
// moves it 960 pixels of its 640x480 screen per quarter turn), and covers 45 degrees up and down, its middle row on the
// horizon (480 pixels for the 45 degrees of the height of the screen); above and below it, the color of its first pixel
// fills the screen, as the game clears it with. The game draws over the texture a quad with the 4 colors
// of the background at its corners (0 and 1 at the top, 2 and 3 at the bottom): a gradient with their alpha;
// here the top color also covers the sky above the texture, so that there is no edge between them

typedef struct {
    Shader      shader;
    int         forwardLoc;
    int         rightLoc;
    int         upLoc;
    int         tangentsLoc;
    int         rowsLoc;
    int         topLoc;
    int         bottomLoc;
    int         backTopLoc;
    int         backBottomLoc;
    Texture     texture;        // id 0 if none is loaded
    float       top[3];         // Color of its first pixel, above and below it
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
// the scene. backTop and backBottom are the ARGB colors of the background over the texture, at its first and last rows
// (alpha 0: none)
void SkyRender_Draw(const SkyRender* sky, Camera3D camera, Rectangle area, uint32_t backTop, uint32_t backBottom);

#endif

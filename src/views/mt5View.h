#ifndef __MT5VIEW_H__
#define __MT5VIEW_H__

#include <stdbool.h>
#include <raylib.h>

// 3D viewer of MT5/CHRM models. The textures of CHRM files are searched in the folder of the model, where the PKF of
// the scene is extracted (tex_<index>_<texture ID>.pvr files)

// Loads a model to show it, replacing the current one. Returns false if the file is not a valid model
bool MT5View_Open(const char* filepath);
void MT5View_Close(void);
bool MT5View_IsOpen(void);
// Handles the camera input (left drag: rotate, right/middle drag: pan, wheel: zoom, R: reset, W: wireframe, C: draw the
// back faces) and the animation frames (space: play/pause, left/right: previous/next frame), and draws the model inside
// area
void MT5View_Draw(Rectangle area);

#endif

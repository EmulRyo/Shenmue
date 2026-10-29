#ifndef __CHRTVIEW_H__
#define __CHRTVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// 3D view of the characters and objects of a CHRT script, each one with its model at its position and angle. The
// models are searched in the folder of the script (<name>.chrm or <name>.mt5), where the IPAC block that contains it
// is extracted, and their textures as in the MT5 viewer

// Loads a script and its models, replacing the current one. Returns false if the script can't be loaded
bool CHRTView_Open(const char* filepath);
void CHRTView_Close(void);
bool CHRTView_IsOpen(void);
// Handles the input (left drag: rotate, right/middle drag: pan, wheel: zoom, R: reset, Tab/Shift+Tab or click on a
// label: look at the next/previous character, L: labels, S: sleeping characters, W: wireframe, C: back faces) and draws
// the scene inside area
void CHRTView_Draw(Rectangle area);

#endif

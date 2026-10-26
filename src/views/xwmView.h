#ifndef __XWMVIEW_H__
#define __XWMVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Player of xWMA audio clips: shows the waveform and plays the clip

// Decodes a clip and starts playing it, replacing the current one. Returns false if the file can't be decoded
bool XWMView_Open(const char* filepath);
void XWMView_Close(void);
bool XWMView_IsOpen(void);
// Handles the input (space: play/pause, left/right: back/forward 1 second, home: start, click or drag on the waveform:
// seek) and draws the player inside area
void XWMView_Draw(Rectangle area);

#endif

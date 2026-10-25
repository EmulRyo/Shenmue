#ifndef __SNDVIEW_H__
#define __SNDVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Player of the samples of SND sound banks: lists the samples, shows the waveform of the selected one and plays it,
// repeating its loop. The banks don't store the sample rate, so it is chosen in the player (22050 Hz by default)

// Loads a bank and starts playing its first sample, replacing the current one. Returns false if the file can't be loaded
bool SNDView_Open(const char* filepath);
void SNDView_Close(void);
bool SNDView_IsOpen(void);
// Handles the input (space: play/pause, up/down: previous/next sample, left/right: back/forward 1 second, home: start,
// L: loop on/off, click or drag on the waveform: seek) and draws the player inside area
void SNDView_Draw(Rectangle area);

#endif

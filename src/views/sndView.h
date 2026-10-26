#ifndef __SNDVIEW_H__
#define __SNDVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Player of SND sound banks: plays the sequences (music and effects) as the Dreamcast does, or the samples alone with
// their waveform

// Loads a bank and starts playing its first sequence, replacing the current one. Returns false if the file can't be
// loaded
bool SNDView_Open(const char* filepath);
void SNDView_Close(void);
bool SNDView_IsOpen(void);
// Handles the input (space: play/pause, up/down: previous/next item, home: start, tab: sequences/samples; in the
// samples: left/right: back/forward 1 second, L: loop on/off, click or drag on the waveform: seek) and draws the player
// inside area
void SNDView_Draw(Rectangle area);

#endif

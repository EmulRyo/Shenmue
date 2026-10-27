#ifndef __WAVEFORM_H__
#define __WAVEFORM_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>

// Waveform of an audio clip for the players: precomputed peaks, drawing and seeking with the mouse

// Columns of the precomputed waveform: the minimum and maximum sample of each range of frames
#define WAVEFORM_PEAKS 4096

typedef struct {
    int16_t     peakMin[WAVEFORM_PEAKS];
    int16_t     peakMax[WAVEFORM_PEAKS];
    uint32_t    peakCount;
    uint32_t    frameCount;
    bool        seeking;  // Dragging on the waveform
} Waveform;

// Loop region drawn behind the waveform, highlighted when it is enabled
typedef struct {
    uint32_t    start;
    uint32_t    end;
    bool        enabled;
} WaveformLoop;

// Computes the minimum and maximum of the first channel of the interleaved frames for each column of the waveform
void Waveform_Compute(Waveform* waveform, const int16_t* pcm, uint32_t frameCount, uint32_t channels);
// Handles a click or a drag on area. Returns true while seeking, with the frame under the mouse in frame
bool Waveform_UpdateSeek(Waveform* waveform, Rectangle area, int64_t* frame);
// Draws the waveform inside area, with the part before position as played. loop can be NULL
void Waveform_Draw(const Waveform* waveform, Rectangle area, uint32_t position, const WaveformLoop* loop);

#endif

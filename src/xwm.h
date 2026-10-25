#ifndef __XWM_H__
#define __XWM_H__

#include <stdbool.h>
#include <stdint.h>

// xWMA audio: a RIFF file ("XWMA") with a WAVEFORMATEX, the decoded size at the end of each packet (dpds) and the WMA
// packets. All the clips of the game are WMA v2, mono, at 22050, 44100 or 32000 Hz

typedef struct {
	uint16_t	formatTag;      // 0x161: WMA v2
	uint16_t	channels;
	uint32_t	sampleRate;
	uint32_t	avgBytesPerSec;
	uint16_t	blockAlign;     // Size of a packet
	uint32_t	packetCount;
	uint32_t	frameCount;     // Decoded frames, from the last dpds value
} XWM_Info;

// Decodes an xWMA file (gzipped or not) to interleaved 16-bit PCM. Returns the samples (frameCount * channels, to be
// freed with free), or NULL if the file is not a valid xWMA file or can't be decoded. The decoding uses the WMA decoder
// of Windows (Media Foundation), so it isn't available on other platforms
int16_t* XWM_Decode(const char* filepath, XWM_Info* info);

#endif

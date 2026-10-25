#ifndef __SND_H__
#define __SND_H__

#include <stdbool.h>
#include <stdint.h>

// SND: a DTPK sound bank of the Dreamcast sound driver, with the instruments, the sequences, the DSP settings and the
// samples. Only the samples are decoded: the sequences are in an unknown encoding

typedef enum {
	SND_ENCODING_PCM16,
	SND_ENCODING_ADPCM,     // 4-bit Yamaha ADPCM of the AICA, 2 samples per byte
} SND_Encoding;

typedef struct {
	SND_Encoding	encoding;
	uint32_t		offset;     // Offset of the data in the file
	uint32_t		size;       // Size of the data in bytes
	bool			loop;       // Bit 25 of the offset field
	uint32_t		loopStart;  // In frames of the decoded sample
	uint32_t		loopEnd;
	int16_t*		pcm;        // Decoded sample, mono
	uint32_t		frameCount;
} SND_Sample;

typedef struct {
	uint32_t		size;
	uint32_t		programCount;
	uint32_t		sequenceCount;
	bool			hasDSP;
	uint32_t		sampleCount;
	SND_Sample*		samples;
} SND_Bank;

// Loads a SND file (gzipped or not) and decodes its samples. Returns false if the file is not a valid DTPK bank
bool SND_Load(const char* filepath, SND_Bank* bank);
void SND_Free(SND_Bank* bank);

#endif

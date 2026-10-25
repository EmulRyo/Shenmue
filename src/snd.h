#ifndef __SND_H__
#define __SND_H__

#include <stdbool.h>
#include <stdint.h>

// SND: a DTPK sound bank of the Dreamcast sound driver, with the instruments, the sequences, the DSP settings and the
// samples. The values are stored as in the registers of the AICA sound chip

#define SND_MAX_LAYERS      4
#define SND_MAX_CHANNELS    16
#define SND_UNSET           0x80 // Pan, level and effect send of a split that take the value of the part

typedef enum {
	SND_ENCODING_PCM16,
	SND_ENCODING_ADPCM,     // 4-bit Yamaha ADPCM of the AICA, 2 samples per byte
} SND_Encoding;

typedef struct {
	SND_Encoding	encoding;
	uint32_t		offset;     // Offset of the data in the file
	uint32_t		size;       // Size of the data in bytes
	bool			loop;       // Bit 25 of the offset field
	bool			halfRate;   // The data has been doubled from half the sample rate
	bool			silentLoop; // The loop repeats a constant value (silence, or the offset left by the ADPCM)
	bool			tailLoop;   // The loop is a few frames at the end: a sound played once that holds its last frames
	uint32_t		loopStart;  // In frames of the decoded sample
	uint32_t		loopEnd;
	int16_t*		pcm;        // Decoded sample, mono
	uint32_t		frameCount;
	uint32_t		sampleRate; // Rate of the first split that plays the sample at note 60, 0 if no split plays it
} SND_Sample;

// A sample played over a range of keys
typedef struct {
	uint8_t		highKey;    // Last key of the split (the index of the effect in the effect program)
	bool		fixedPitch; // The pitch doesn't follow the key: the sample plays as at note 60 (drums, effects)
	uint8_t		sample;
	int8_t		transpose;  // Semitones: the sample plays at 44100 Hz at note 60 - transpose - fine / 64
	int8_t		fine;       // 1/64 semitone
	uint8_t		pan;        // AICA DIPAN (bit 4: left), or SND_UNSET
	uint8_t		level;      // AICA DISDL (0 to 15, 3 dB steps), or SND_UNSET
	uint8_t		attackRate;     // AICA envelope rates (0 to 31) and decay level
	uint8_t		decay1Rate;
	uint8_t		decay2Rate;
	uint8_t		releaseRate;
	uint8_t		decayLevel;
	uint8_t		keyRateScaling;
	uint16_t	lfo;        // AICA LFO register (reset, frequency, pitch and amplitude waveform and depth); the low byte
	                        // is 0x80 when the depths are not set
	uint8_t		send;       // AICA IMXL (bits 4-7) and ISEL (bits 0-3) of the effect send
	bool		sendSet;    // false: the effect send of the part is used
} SND_Split;

typedef struct {
	uint8_t		lowKey;
	uint8_t		highKey;
	uint8_t		bendRange;  // Semitones
	uint32_t	splitCount;
	SND_Split*	splits;
	int16_t		keyMap[128]; // Split of each key, -1 if none; when the layer has no key map, the split covering the key
} SND_Layer;

typedef struct {
	uint32_t	layerCount;
	SND_Layer	layers[SND_MAX_LAYERS];
} SND_Program;

typedef struct {
	uint8_t		channel;
	uint8_t		program;
	uint8_t		send;       // IMXL and ISEL
	uint8_t		level;      // DISDL
	uint8_t		volume;     // 0 to 127
	uint8_t		pan;        // DIPAN
	int8_t		transpose;
} SND_Part;

typedef enum {
	SND_SEQUENCE_EMPTY,
	SND_SEQUENCE_MUSIC,     // A playlist of MIDI-like tracks, with times in milliseconds
	SND_SEQUENCE_EFFECT,    // Effects of the effect program
} SND_SequenceType;

typedef struct {
	uint32_t			group;
	uint32_t			index;
	SND_SequenceType	type;
	uint32_t			offset; // In the file
	uint32_t			duration; // Milliseconds up to the end or the loop
	bool				loop;
} SND_Sequence;

typedef struct {
	bool		present;
	uint16_t	outputs[18]; // EFSDL (bits 8-11) and EFPAN (bits 0-4) of the 16 effect outputs and the 2 external inputs
	uint16_t	coef[128];
	uint16_t	madrs[64];
	uint16_t	mpro[128][4];
} SND_DSP;

typedef struct {
	uint8_t*		data;   // The whole file, for the sequences
	uint32_t		size;
	uint32_t		partCount;
	SND_Part*		parts;
	uint32_t		programCount;
	SND_Program*	programs;
	bool			hasEffectProgram;
	SND_Program		effectProgram;
	uint8_t			velocityCurve[128];
	uint32_t		sequenceCount;
	SND_Sequence*	sequences;
	SND_DSP			dsp;
	uint32_t		sampleCount;
	SND_Sample*		samples;
} SND_Bank;

// An event of a sequence: a MIDI message (note on and off, controller, program change, pitch bend of 1 byte) in the music
// tracks, or the start of an effect (status Dx: effect, volume and an unknown byte) and a controller (Ax) in the effects
typedef struct {
	uint8_t		status;     // SND_EVENT_END at the end of the track or the effect
	uint8_t		data[3];
	uint32_t	delta;      // Milliseconds to wait after the event
} SND_Event;

#define SND_EVENT_END 0xFF

// Playlist of a music sequence: 32-bit words after the first one
#define SND_PLAYLIST_COMMAND(word)  (((word) >> 16) == 0x80A0) // Command (bits 8-15) and value (bits 0-7)
#define SND_PLAYLIST_TRACK(word)    (((word) >> 24) == 0x00)   // Offset of a track, from the start of the sequence
#define SND_PLAYLIST_JUMP(word)     (((word) >> 24) == 0x90)   // Go on at the word at this offset (the loop)
#define SND_PLAYLIST_LAST(word)     (((word) >> 24) == 0x91)   // Offset of the last track (bits 0-23), then stop

// Loads a SND file (gzipped or not) and decodes its samples. Returns false if the file is not a valid DTPK bank
bool SND_Load(const char* filepath, SND_Bank* bank);
void SND_Free(SND_Bank* bank);
// Reads the event at pos of a music track or an effect sequence. runningStatus keeps the status between the events.
// Returns the position of the next event, or 0 if the event is not valid or after the end of the file
uint32_t SND_ReadEvent(const SND_Bank* bank, uint32_t pos, bool effect, uint8_t* runningStatus, SND_Event* event);
uint32_t SND_ReadU32(const SND_Bank* bank, uint32_t pos); // 0 after the end of the file

#endif

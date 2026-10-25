#ifndef __SNDSYNTH_H__
#define __SNDSYNTH_H__

#include <stdbool.h>
#include <stdint.h>
#include "snd.h"

// Plays the sequences and samples of a SND bank as the Dreamcast does: a sequencer drives up to 64 voices of the AICA
// (sample playback with pitch, envelope, LFO, level and pan), mixed with the reverb of the DSP program of the bank.
// The output is 16-bit stereo at 44100 Hz. The functions are not thread-safe: the caller serializes them

#define SND_SYNTH_RATE      44100
#define SND_SYNTH_VOICES    64

typedef struct SND_Synth SND_Synth;

SND_Synth*	SND_SynthCreate(const SND_Bank* bank);
void		SND_SynthDestroy(SND_Synth* synth);
// Stops everything and starts a sequence (an index of bank->sequences)
void		SND_SynthPlaySequence(SND_Synth* synth, uint32_t sequence);
// Stops everything and plays a sample alone at a rate, repeating its loop if it has one and loop is true
void		SND_SynthPlaySample(SND_Synth* synth, uint32_t sample, uint32_t rate, bool loop);
void		SND_SynthStop(SND_Synth* synth);
// Frame of the sample played alone, and seek in it
uint32_t	SND_SynthSamplePosition(const SND_Synth* synth);
void		SND_SynthSeekSample(SND_Synth* synth, uint32_t frame);
void		SND_SynthSetSampleLoop(SND_Synth* synth, bool loop);
// Renders frames of interleaved stereo samples
void		SND_SynthRender(SND_Synth* synth, int16_t* output, uint32_t frames);
// True while the sequence or the sample plays, or while voices sound
bool		SND_SynthIsPlaying(const SND_Synth* synth);
uint32_t	SND_SynthTime(const SND_Synth* synth);         // Milliseconds since the start
uint32_t	SND_SynthActiveVoices(const SND_Synth* synth);
// Level of the voices of each MIDI channel (0 to 1), for meters
void		SND_SynthChannelLevels(const SND_Synth* synth, float levels[SND_MAX_CHANNELS]);

#endif

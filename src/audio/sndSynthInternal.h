#ifndef __SNDSYNTHINTERNAL_H__
#define __SNDSYNTHINTERNAL_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/sound/snd.h"
#include "audio/sndDsp.h"
#include "audio/sndSynth.h"

// State of the synthesizer shared by its parts: the voices (sndSynth.c), the sequencer and the MIDI channels
// (sndSequencer.c) and the DSP (sndDsp.c)

#define CHANNEL_COUNT       (SND_MAX_CHANNELS * 2) // Music channels, then effect channels
#define SAMPLES_PER_MS      (SND_SYNTH_RATE / 1000.0)
#define PAN_LAYERS          64      // Layers whose position in their pan pattern is kept

typedef enum {
    ENVELOPE_ATTACK,
    ENVELOPE_DECAY1,
    ENVELOPE_DECAY2,
    ENVELOPE_RELEASE,
} EnvelopeState;

typedef struct {
    bool                active;
    const SND_Sample*   sample;
    const SND_Split*    split;
    int                 channel;    // 0-15: music, 16-31: effects, -1: a sample played alone
    uint8_t             key;        // Note, or index of the effect
    bool                keyOff;
    bool                sustained;  // Released while the sustain pedal is down
    bool                fading;     // Reached a loop that would hold it forever
    bool                loop;
    double              position;   // Frame of the sample
    double              step;       // Frames per output sample
    float               pitch;      // Semitones from the rate of 44100 Hz, without bend and LFO
    float               rateFactor; // 2 for the samples doubled from half the sample rate
    float               bendUp, bendDown;
    // Envelope: the level goes from 0 (-96 dB) to 1023 (0 dB), linearly in dB
    bool                useEnvelope;
    EnvelopeState       state;
    float               level;
    float               attackStep, decay1Step, decay2Step, releaseStep, decayLevel;
    // Filter: the envelope moves the cutoff level (FLV, 0 to 8191) towards the level of the next state, every control
    // period; a resonant low-pass biquad
    bool                filter;
    EnvelopeState       filterState;
    float               filterLevel;
    float               filterSteps[4];
    float               b0, b1, b2, a1, a2;
    float               x1, x2, y1, y2;
    // Levels: the velocity through the curve (0 to 254) and DISDL, then the channel, the pan and the effect send
    int                 velocityLevel;
    float               baseGain;
    uint8_t             pan;        // AICA DIPAN of the split or the part
    float               sendGain;
    uint8_t             isel;
    float               gainLeft, gainRight, gainSend; // Updated every control period
    // LFO
    float               lfoPhase;
    float               lfoStep;
    float               pitchDepth; // Cents
    float               ampDepth;   // dB
    int                 pitchWave, ampWave;
    uint32_t            age;
    float               meter;
} Voice;

typedef struct {
    uint8_t     volume;
    uint8_t     expression;
    uint8_t     pan;        // MIDI pan, used when panSet
    bool        panSet;
    uint8_t     bend;
    uint8_t     modulation;
    bool        sustain;
    int         program;    // Program change, -1 for the programs of the parts
} Channel;

typedef struct {
    bool        active;
    bool        effect;
    uint32_t    start;      // Offset of the sequence
    uint32_t    word;       // Offset of the next word of the playlist, from the start
    uint32_t    pos;        // Position of the next event, 0 between the tracks
    uint8_t     status;
    bool        last;       // The track is the last one of the playlist
    double      wait;       // Samples until the next event
} Sequencer;

struct SND_Synth {
    const SND_Bank* bank;
    Voice           voices[SND_SYNTH_VOICES];
    Channel         channels[CHANNEL_COUNT];
    Sequencer       sequencer;
    uint64_t        time;
    uint32_t        age;
    uint32_t        controlCounter;
    float           levelTable[1024];   // Gain of each level of the envelope
    float           totalLevelTable[256]; // Gain of each AICA total level
    const SND_Layer* panLayers[PAN_LAYERS]; // Position of the layers with a pan pattern in their pattern
    uint8_t         panPositions[PAN_LAYERS];
    DSP             dsp;
    float           outputLeft[16], outputRight[16]; // EFSDL and EFPAN of the effect outputs
};

// Voices
void Synth_KeyOff(Voice* voice);
// Each layer of the program plays the split of the key, if the velocity is in the range of the split. part is NULL for
// the effects
void Synth_StartNote(SND_Synth* synth, const SND_Program* program, int channel, uint8_t key, uint8_t velocity,
    const SND_Part* part);
// A note on a channel plays the programs of all the parts of the channel
void Synth_NoteOn(SND_Synth* synth, int channel, uint8_t key, uint8_t velocity);
void Synth_NoteOff(SND_Synth* synth, int channel, uint8_t key);

// Sequencer and channels
void Channels_Reset(SND_Synth* synth);
void Sequencer_Start(SND_Synth* synth, const SND_Sequence* sequence);
void Sequencer_Stop(SND_Synth* synth);
// Plays the events due at the current sample
void Sequencer_Update(SND_Synth* synth);

#endif

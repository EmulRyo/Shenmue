#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sndSynth.h"

#define CHANNEL_COUNT       (SND_MAX_CHANNELS * 2) // Music channels, then effect channels
#define CONTROL_PERIOD      32      // Samples between the updates of the pitch, the LFO and the levels
#define MASTER_GAIN         0.6f
#define MAX_EVENTS_PER_SAMPLE 4096  // Stops a sequence that loops without advancing in time
#define SAMPLES_PER_MS      (SND_SYNTH_RATE / 1000.0)
#define DSP_RAM_MASK        0xFFFF
#define FADE_MS             5.0f    // Fade out of the voices that reach a loop that would hold them forever
#define PI                  3.14159265f

// Times of the AICA envelope, in milliseconds, for the effective rates 0 to 63 (attack, and decay and release)
static const float g_attackTimes[64] = {
	100000.0f, 100000.0f, 8100.0f, 6900.0f, 6000.0f, 4800.0f, 4000.0f, 3400.0f, 3000.0f, 2400.0f, 2000.0f, 1700.0f,
	1500.0f, 1200.0f, 1000.0f, 860.0f, 760.0f, 600.0f, 500.0f, 430.0f, 380.0f, 300.0f, 250.0f, 220.0f, 190.0f,
	150.0f, 130.0f, 110.0f, 95.0f, 76.0f, 63.0f, 55.0f, 47.0f, 38.0f, 31.0f, 27.0f, 24.0f, 19.0f, 15.0f, 13.0f,
	12.0f, 9.4f, 7.9f, 6.8f, 6.0f, 4.7f, 3.8f, 3.4f, 3.0f, 2.4f, 2.0f, 1.8f, 1.6f, 1.3f, 1.1f, 0.93f, 0.85f, 0.65f,
	0.53f, 0.44f, 0.40f, 0.35f, 0.0f, 0.0f,
};
static const float g_decayTimes[64] = {
	100000.0f, 100000.0f, 118200.0f, 101300.0f, 88600.0f, 70900.0f, 59100.0f, 50700.0f, 44300.0f, 35500.0f,
	29600.0f, 25300.0f, 22200.0f, 17700.0f, 14800.0f, 12700.0f, 11100.0f, 8900.0f, 7400.0f, 6300.0f, 5500.0f,
	4400.0f, 3700.0f, 3200.0f, 2800.0f, 2200.0f, 1800.0f, 1600.0f, 1400.0f, 1100.0f, 920.0f, 790.0f, 690.0f,
	550.0f, 460.0f, 390.0f, 340.0f, 270.0f, 230.0f, 200.0f, 170.0f, 140.0f, 110.0f, 98.0f, 85.0f, 68.0f, 57.0f,
	49.0f, 43.0f, 34.0f, 28.0f, 25.0f, 22.0f, 18.0f, 14.0f, 12.0f, 11.0f, 8.5f, 7.1f, 6.1f, 5.4f, 4.3f, 3.6f, 3.1f,
};
// Frequencies of the LFO (Hz), and depths of the pitch LFO (cents) and of the amplitude LFO (dB)
static const float g_lfoFrequencies[32] = {
	0.17f, 0.19f, 0.23f, 0.27f, 0.34f, 0.39f, 0.45f, 0.55f, 0.68f, 0.78f, 0.92f, 1.10f, 1.39f, 1.60f, 1.87f, 2.27f,
	2.87f, 3.31f, 3.92f, 4.79f, 6.07f, 7.18f, 8.60f, 10.8f, 14.4f, 17.2f, 21.5f, 28.7f, 43.1f, 57.4f, 86.1f, 172.3f,
};
static const float g_pitchLfoCents[8] = { 0.0f, 7.0f, 13.5f, 27.0f, 55.0f, 112.0f, 230.0f, 494.0f };
static const float g_ampLfoDb[8] = { 0.0f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f };
#define DEFAULT_LFO_FREQUENCY   19      // When the split doesn't set it (4.79 Hz)
#define MODULATION_CENTS        100.0f  // Depth of the vibrato of the modulation wheel at 127
// Attenuation of each bit of the AICA total level (TL), in dB
static const float g_totalLevelDb[8] = { 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f, 48.0f };

// Pan patterns of the sound driver: the successive notes of a layer take the next pan (AICA DIPAN) of the pattern, up to
// the end of the pattern (16 values or 0xFF) and again from the start
#define PAN_PATTERNS        8
#define PAN_PATTERN_SIZE    16
#define PAN_LAYERS          64      // Layers whose position in their pattern is kept
static const uint8_t g_panPatterns[PAN_PATTERNS][PAN_PATTERN_SIZE] = {
	{ 0x11, 0x01, 0xFF },
	{ 0x14, 0x04, 0xFF },
	{ 0x1F, 0x0F, 0xFF },
	{ 0x18, 0x08, 0x14, 0x04, 0xFF },
	{ 0x02, 0x04, 0x08, 0x0F, 0x08, 0x04, 0x02, 0x00, 0x12, 0x14, 0x18, 0x1F, 0x18, 0x14, 0x12, 0x00 },
	{ 0x02, 0x04, 0x08, 0x0F, 0x1F, 0x18, 0x14, 0x12, 0xFF },
	{ 0x12, 0x14, 0x18, 0x1F, 0x0F, 0x08, 0x04, 0x02, 0xFF },
	{ 0x08, 0x1F, 0x02, 0x18, 0x08, 0x14, 0x02, 0x00, 0x14, 0x08, 0x02, 0x1F, 0x04, 0x12, 0x08, 0x18 },
};

typedef enum {
	ENVELOPE_ATTACK,
	ENVELOPE_DECAY1,
	ENVELOPE_DECAY2,
	ENVELOPE_RELEASE,
} EnvelopeState;

typedef struct {
	bool				active;
	const SND_Sample*	sample;
	const SND_Split*	split;
	int					channel;    // 0-15: music, 16-31: effects, -1: a sample played alone
	uint8_t				key;        // Note, or index of the effect
	bool				keyOff;
	bool				sustained;  // Released while the sustain pedal is down
	bool				fading;     // Reached a loop that would hold it forever
	bool				loop;
	double				position;   // Frame of the sample
	double				step;       // Frames per output sample
	float				pitch;      // Semitones from the rate of 44100 Hz, without bend and LFO
	float				rateFactor; // 2 for the samples doubled from half the sample rate
	float				bendUp, bendDown;
	// Envelope: the level goes from 0 (-96 dB) to 1023 (0 dB), linearly in dB
	bool				useEnvelope;
	EnvelopeState		state;
	float				level;
	float				attackStep, decay1Step, decay2Step, releaseStep, decayLevel;
	// Filter: the envelope moves the cutoff level (FLV, 0 to 8191) towards the level of the next state, every control
	// period; a resonant low-pass biquad
	bool				filter;
	EnvelopeState		filterState;
	float				filterLevel;
	float				filterSteps[4];
	float				b0, b1, b2, a1, a2;
	float				x1, x2, y1, y2;
	// Levels: the velocity through the curve (0 to 254) and DISDL, then the channel, the pan and the effect send
	int					velocityLevel;
	float				baseGain;
	uint8_t				pan;        // AICA DIPAN of the split or the part
	float				sendGain;
	uint8_t				isel;
	float				gainLeft, gainRight, gainSend; // Updated every control period
	// LFO
	float				lfoPhase;
	float				lfoStep;
	float				pitchDepth; // Cents
	float				ampDepth;   // dB
	int					pitchWave, ampWave;
	uint32_t			age;
	float				meter;
} Voice;

typedef struct {
	uint8_t		volume;
	uint8_t		expression;
	uint8_t		pan;        // MIDI pan, used when panSet
	bool		panSet;
	uint8_t		bend;
	uint8_t		modulation;
	bool		sustain;
	int			program;    // Program change, -1 for the programs of the parts
} Channel;

typedef struct {
	bool		active;
	bool		effect;
	uint32_t	start;      // Offset of the sequence
	uint32_t	word;       // Offset of the next word of the playlist, from the start
	uint32_t	pos;        // Position of the next event, 0 between the tracks
	uint8_t		status;
	bool		last;       // The track is the last one of the playlist
	double		wait;       // Samples until the next event
} Sequencer;

typedef struct {
	int32_t		temp[128];
	int32_t		mems[32];
	int32_t		mixs[16];
	int32_t		efreg[16];
	int32_t		acc;
	int32_t		frcReg;
	int32_t		yReg;
	int32_t		adrsReg;
	int32_t		memVal;
	uint32_t	dec;
	int			stepCount;
	uint16_t	ram[DSP_RAM_MASK + 1];
} DSP;

struct SND_Synth {
	const SND_Bank*	bank;
	Voice			voices[SND_SYNTH_VOICES];
	Channel			channels[CHANNEL_COUNT];
	Sequencer		sequencer;
	uint64_t		time;
	uint32_t		age;
	uint32_t		controlCounter;
	float			levelTable[1024];   // Gain of each level of the envelope
	float			totalLevelTable[256]; // Gain of each AICA total level
	const SND_Layer* panLayers[PAN_LAYERS]; // Position of the layers with a pan pattern in their pattern
	uint8_t			panPositions[PAN_LAYERS];
	DSP				dsp;
	float			outputLeft[16], outputRight[16]; // EFSDL and EFPAN of the effect outputs
};

// AICA levels (DISDL, IMXL, EFSDL): 15 is 0 dB, each step is -3 dB, 0 is off
static float LevelGain(int level) {
	return level <= 0 ? 0.0f : powf(10.0f, -3.0f * (15 - level) / 20.0f);
}

// Level of the sound driver (0 to 254) scaled by a value from 0 to 127, rounded up
static int ScaleLevel(int level, int scale) {
	int result = (level * scale * 2) >> 8;
	return result != 0 ? result + 1 : 0;
}

// The sound driver turns the level (0 to 254: the velocity through the curve, scaled by the volume and the expression)
// into the AICA total level (TL), which attenuates in steps of 0.375 dB: TL = (255 - level) / 2
static float TotalLevelGain(const SND_Synth* synth, int level) {
	return level <= 0 ? 0.0f : synth->totalLevelTable[(255 - level) >> 1];
}

// AICA pan: bits 0-3 attenuate a side by 3 dB per step (15: off), the right one below 0x10 and the left one above
static void PanGains(uint8_t pan, float* left, float* right) {
	int amount = pan & 0x0F;
	float attenuation = amount == 0x0F ? 0.0f : powf(10.0f, -3.0f * amount / 20.0f);
	*left = 1.0f;
	*right = 1.0f;
	if (pan & 0x10)
		*right = attenuation;
	else
		*left = attenuation;
}

static float TriangleWave(float phase) {
	return phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
}

// LFO waveforms of the AICA: sawtooth, square, triangle and noise, from -1 to 1
static float LfoWave(int wave, float phase) {
	switch (wave) {
	case 0: return phase * 2.0f - 1.0f;
	case 1: return phase < 0.5f ? 1.0f : -1.0f;
	case 2: return TriangleWave(phase);
	default: return (float)rand() / RAND_MAX * 2.0f - 1.0f;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// DSP: the effect processor of the AICA, running the program of the bank once per output sample

static uint16_t DSP_Pack(int32_t value) {
	int sign = (value >> 23) & 1;
	uint32_t temp = (uint32_t)(value ^ (value << 1)) & 0xFFFFFF;
	int exponent = 0;
	for (int k = 0; k < 12; k++) {
		if (temp & 0x800000)
			break;
		temp <<= 1;
		exponent++;
	}
	if (exponent < 12)
		value = (value << exponent) & 0x3FFFFF;
	else
		value <<= 11;
	value >>= 11;
	value &= 0x7FF;
	value |= sign << 15;
	value |= exponent << 11;
	return (uint16_t)value;
}

static int32_t DSP_Unpack(uint16_t value) {
	int sign = (value >> 15) & 1;
	int exponent = (value >> 11) & 0xF;
	int32_t result = (value & 0x7FF) << 11;
	if (exponent > 11) {
		exponent = 11;
		result |= sign << 22;
	}
	else
		result |= (sign ^ 1) << 22;
	result |= sign << 23;
	result = (int32_t)((uint32_t)result << 8) >> 8;
	return result >> exponent;
}

static int32_t SignExtend24(int32_t value) {
	return (int32_t)((uint32_t)value << 8) >> 8;
}

static int32_t Clamp24(int64_t value) {
	if (value > 0x7FFFFF) return 0x7FFFFF;
	if (value < -0x800000) return -0x800000;
	return (int32_t)value;
}

static void DSP_Step(DSP* dsp, const SND_DSP* program) {
	memset(dsp->efreg, 0, sizeof(dsp->efreg));
	for (int step = 0; step < dsp->stepCount; step++) {
		const uint16_t* ins = program->mpro[step];
		uint32_t tra = (ins[0] >> 9) & 0x7F;
		uint32_t twt = (ins[0] >> 8) & 0x01;
		uint32_t twa = (ins[0] >> 1) & 0x7F;
		uint32_t xsel = (ins[1] >> 15) & 0x01;
		uint32_t ysel = (ins[1] >> 13) & 0x03;
		uint32_t ira = (ins[1] >> 7) & 0x3F;
		uint32_t iwt = (ins[1] >> 6) & 0x01;
		uint32_t iwa = (ins[1] >> 1) & 0x1F;
		uint32_t table = (ins[2] >> 15) & 0x01;
		uint32_t mwt = (ins[2] >> 14) & 0x01;
		uint32_t mrd = (ins[2] >> 13) & 0x01;
		uint32_t ewt = (ins[2] >> 12) & 0x01;
		uint32_t ewa = (ins[2] >> 8) & 0x0F;
		uint32_t adrl = (ins[2] >> 7) & 0x01;
		uint32_t frcl = (ins[2] >> 6) & 0x01;
		uint32_t shift = (ins[2] >> 4) & 0x03;
		uint32_t yrl = (ins[2] >> 3) & 0x01;
		uint32_t negb = (ins[2] >> 2) & 0x01;
		uint32_t zero = (ins[2] >> 1) & 0x01;
		uint32_t bsel = (ins[2] >> 0) & 0x01;
		uint32_t nofl = (ins[3] >> 15) & 0x01;
		uint32_t masa = (ins[3] >> 9) & 0x1F;
		uint32_t adreb = (ins[3] >> 8) & 0x01;
		uint32_t nxadr = (ins[3] >> 7) & 0x01;

		// Input: the memory registers, the mixed voices (20 bits) or the external inputs (silent)
		int32_t inputs;
		if (ira <= 0x1F)
			inputs = dsp->mems[ira];
		else if (ira <= 0x2F)
			inputs = dsp->mixs[ira - 0x20] << 4;
		else
			inputs = 0;
		inputs = SignExtend24(inputs);
		if (iwt) {
			dsp->mems[iwa] = dsp->memVal;
			if (ira == iwa)
				inputs = dsp->memVal;
		}

		int32_t b = 0;
		if (!zero) {
			b = bsel ? dsp->acc : SignExtend24(dsp->temp[(tra + dsp->dec) & 0x7F]);
			if (negb)
				b = -b;
		}
		int32_t x = xsel ? inputs : SignExtend24(dsp->temp[(tra + dsp->dec) & 0x7F]);
		int32_t y;
		if (ysel == 0)
			y = dsp->frcReg;
		else if (ysel == 1)
			y = ((int16_t)program->coef[step]) >> 3; // The coefficient of each step
		else if (ysel == 2)
			y = (dsp->yReg >> 11) & 0x1FFF;
		else
			y = (dsp->yReg >> 4) & 0x0FFF;
		if (yrl)
			dsp->yReg = inputs;

		int32_t shifted;
		if (shift == 0)
			shifted = Clamp24(dsp->acc);
		else if (shift == 1)
			shifted = Clamp24((int64_t)dsp->acc * 2);
		else if (shift == 2)
			shifted = SignExtend24(dsp->acc * 2);
		else
			shifted = SignExtend24(dsp->acc);

		y = (int32_t)((uint32_t)y << 19) >> 19; // 13 bits
		dsp->acc = (int32_t)(((int64_t)x * y) >> 12) + b;

		if (twt)
			dsp->temp[(twa + dsp->dec) & 0x7F] = shifted;
		if (frcl)
			dsp->frcReg = shift == 3 ? shifted & 0x0FFF : (shifted >> 11) & 0x1FFF;

		if (mrd || mwt) {
			uint32_t address = program->madrs[masa];
			if (!table)
				address += dsp->dec;
			if (adreb)
				address += dsp->adrsReg & 0x0FFF;
			if (nxadr)
				address++;
			address &= DSP_RAM_MASK;
			// The memory is accessed on the odd steps only
			if (mrd && (step & 1))
				dsp->memVal = nofl ? dsp->ram[address] << 8 : DSP_Unpack(dsp->ram[address]);
			if (mwt && (step & 1))
				dsp->ram[address] = nofl ? (uint16_t)(shifted >> 8) : DSP_Pack(shifted);
		}

		if (adrl)
			dsp->adrsReg = shift == 3 ? (shifted >> 12) & 0x0FFF : inputs >> 16;
		if (ewt)
			dsp->efreg[ewa] += shifted >> 8;
	}
	dsp->dec--;
	memset(dsp->mixs, 0, sizeof(dsp->mixs));
}

// ---------------------------------------------------------------------------------------------------------------------
// Voices

static float EnvelopeStep(const float* times, int rate) {
	if (rate < 0) rate = 0;
	if (rate > 63) rate = 63;
	float time = times[rate];
	if (time <= 0.0f)
		return 1024.0f;
	if (time >= 100000.0f)
		return 0.0f;
	return 1023.0f / (time * (float)SAMPLES_PER_MS);
}

// Rates of the envelope: 2 × the rate of the split, plus the key rate scaling from the octave of the pitch
static void SetEnvelope(Voice* voice) {
	const SND_Split* split = voice->split;
	int base = 0;
	if (split->keyRateScaling != 0x0F) {
		float octaves = voice->pitch / 12.0f + log2f(voice->rateFactor);
		int octave = (int)floorf(octaves);
		if (octave < -8) octave = -8;
		if (octave > 7) octave = 7;
		int fnsHigh = exp2f(octaves - floorf(octaves)) >= 1.5f; // Bit 9 of FNS
		base = octave + 2 * split->keyRateScaling + fnsHigh;
	}
	voice->attackStep = EnvelopeStep(g_attackTimes, base + 2 * split->attackRate);
	voice->decay1Step = EnvelopeStep(g_decayTimes, base + 2 * split->decay1Rate);
	voice->decay2Step = EnvelopeStep(g_decayTimes, base + 2 * split->decay2Rate);
	voice->releaseStep = EnvelopeStep(g_decayTimes, base + 2 * split->releaseRate);
	voice->decayLevel = (float)((0x1F - split->decayLevel) << 5);
	// The filter envelope: the rates of the split, over the whole range of the cutoff level, every control period
	for (int i = 0; i < 4; i++)
		voice->filterSteps[i] = EnvelopeStep(g_decayTimes, 2 * split->filterRates[i]) * (8191.0f / 1023.0f) * CONTROL_PERIOD;
}

// Cutoff of the filter from the level FLV (13 bits, 256 steps per octave, 0x1FF8 is open) and resonance from Q
// (-3 dB to 20.25 dB, 0.75 dB per step): coefficients of a low-pass biquad
static void SetFilter(Voice* voice) {
	int level = (int)voice->filterLevel;
	float coefficient = (float)(((((level & 0xFF) | 0x100) << 4) >> ((level >> 8) ^ 0x1F))) / 8192.0f;
	float cutoff = -logf(1.0f - coefficient) * SND_SYNTH_RATE / (2.0f * PI);
	cutoff = fminf(fmaxf(cutoff, 10.0f), SND_SYNTH_RATE * 0.45f);
	float q = powf(10.0f, (-3.0f + 0.75f * voice->split->resonance) / 20.0f);
	float w = 2.0f * PI * cutoff / SND_SYNTH_RATE;
	float alpha = sinf(w) / (2.0f * q);
	float cosine = cosf(w);
	float a0 = 1.0f + alpha;
	voice->b1 = (1.0f - cosine) / a0;
	voice->b0 = voice->b2 = voice->b1 * 0.5f;
	voice->a1 = -2.0f * cosine / a0;
	voice->a2 = (1.0f - alpha) / a0;
}

// The filter envelope goes to FLV1 (attack), FLV2 (decay 1) and FLV3 (decay 2), and to FLV4 at the release
static void UpdateFilterEnvelope(Voice* voice) {
	const SND_Split* split = voice->split;
	float target = split->filterLevels[voice->filterState + 1];
	float step = voice->filterSteps[voice->filterState];
	if (voice->filterLevel < target)
		voice->filterLevel = fminf(voice->filterLevel + step, target);
	else
		voice->filterLevel = fmaxf(voice->filterLevel - step, target);
	if (voice->filterLevel == target && (voice->filterState == ENVELOPE_ATTACK || voice->filterState == ENVELOPE_DECAY1))
		voice->filterState++;
	SetFilter(voice);
}

static void UpdateVoiceControl(SND_Synth* synth, Voice* voice);

static void KeyOff(Voice* voice) {
	voice->keyOff = true;
	voice->sustained = false;
	voice->filterState = ENVELOPE_RELEASE;
	if (!voice->useEnvelope)
		voice->active = false;
	else
		voice->state = ENVELOPE_RELEASE;
}

static Voice* AllocateVoice(SND_Synth* synth) {
	Voice* best = NULL;
	for (int i = 0; i < SND_SYNTH_VOICES; i++) {
		Voice* voice = &synth->voices[i];
		if (!voice->active)
			return voice;
		// Steal the oldest voice, preferring the released ones
		if (best == NULL || (voice->keyOff && !best->keyOff) || (voice->keyOff == best->keyOff && voice->age < best->age))
			best = voice;
	}
	return best;
}

// Next pan of the pattern of a layer. The pan of the split is kept for the patterns that don't exist
static uint8_t NextPan(SND_Synth* synth, const SND_Layer* layer, uint8_t pan) {
	if (layer->panPattern > PAN_PATTERNS)
		return pan;
	int slot = 0;
	while (slot < PAN_LAYERS && synth->panLayers[slot] != NULL && synth->panLayers[slot] != layer)
		slot++;
	if (slot == PAN_LAYERS)
		slot = 0;
	if (synth->panLayers[slot] != layer) {
		synth->panLayers[slot] = layer;
		synth->panPositions[slot] = 0;
	}
	const uint8_t* pattern = g_panPatterns[layer->panPattern - 1];
	uint8_t position = synth->panPositions[slot];
	pan = pattern[position++];
	if (position == PAN_PATTERN_SIZE || pattern[position] == 0xFF)
		position = 0;
	synth->panPositions[slot] = position;
	return pan;
}

static void StartVoice(SND_Synth* synth, int channel, uint8_t key, uint8_t velocity, const SND_Layer* layer,
	const SND_Split* split, const SND_Part* part) {
	const SND_Bank* bank = synth->bank;
	if (split->sample >= bank->sampleCount || bank->samples[split->sample].pcm == NULL)
		return;
	const SND_Sample* sample = &bank->samples[split->sample];
	Voice* voice = AllocateVoice(synth);
	memset(voice, 0, sizeof(*voice));
	voice->active = true;
	voice->sample = sample;
	voice->split = split;
	voice->channel = channel;
	voice->key = key;
	voice->loop = sample->loop;
	voice->age = synth->age++;
	voice->rateFactor = sample->halfRate ? 2.0f : 1.0f;
	voice->bendUp = layer->bendUp;
	voice->bendDown = layer->bendDown;
	// Pitch: the key (unless the pitch is fixed), the transpose and fine tune of the part and of the split, and a random
	// detune of the split
	voice->pitch = split->transpose + split->fine / 64.0f;
	if (part != NULL)
		voice->pitch += part->transpose + part->fine / 64.0f;
	if (!split->fixedPitch)
		voice->pitch += key - 60;
	if (split->detune != 0)
		voice->pitch += split->detune * 6 * ((rand() & 0xFF) - 128) / 128 / 256.0f;

	voice->useEnvelope = true;
	voice->state = ENVELOPE_ATTACK;
	SetEnvelope(voice);
	voice->filter = split->filter;
	voice->filterState = ENVELOPE_ATTACK;
	voice->filterLevel = split->filterLevels[0];
	if (voice->filter)
		SetFilter(voice);

	const uint8_t* curve = bank->curves[layer->curve < bank->curveCount ? layer->curve : 0];
	voice->velocityLevel = ScaleLevel(curve[velocity & 0x7F], split->velocityLevel);
	uint8_t level = split->level != SND_UNSET ? split->level : (part != NULL ? part->level : 15);
	voice->baseGain = LevelGain(level);
	if (split->pan == SND_UNSET)
		voice->pan = part != NULL ? part->pan : 0;
	else
		voice->pan = layer->panPattern != 0 ? NextPan(synth, layer, split->pan) : split->pan;
	uint8_t send = split->sendSet ? split->send : (part != NULL ? part->send : 0);
	voice->sendGain = LevelGain(send >> 4);
	voice->isel = send & 0x0F;

	// LFO: the frequency and waveforms of the split, also for the modulation wheel; the depths of the split when the
	// LFO isn't held in reset (bit 15)
	int frequency = (split->lfo >> 10) & 0x1F;
	bool reset = (split->lfo & 0x8000) != 0;
	if (reset && frequency == 0)
		frequency = DEFAULT_LFO_FREQUENCY;
	voice->lfoStep = g_lfoFrequencies[frequency] / SND_SYNTH_RATE * CONTROL_PERIOD;
	voice->pitchWave = (split->lfo >> 8) & 3;
	voice->ampWave = (split->lfo >> 3) & 3;
	if (!reset) {
		voice->pitchDepth = g_pitchLfoCents[(split->lfo >> 5) & 7];
		voice->ampDepth = g_ampLfoDb[split->lfo & 7];
	}
	UpdateVoiceControl(synth, voice);
}

// Each layer plays the split of the key, if the velocity is in the range of the split
static void StartNote(SND_Synth* synth, const SND_Program* program, int channel, uint8_t key, uint8_t velocity,
	const SND_Part* part) {
	for (uint32_t l = 0; l < program->layerCount; l++) {
		const SND_Layer* layer = &program->layers[l];
		if (layer->keyMap[key] < 0)
			continue;
		const SND_Split* split = &layer->splits[layer->keyMap[key]];
		if (velocity < split->lowVelocity || velocity > split->highVelocity)
			continue;
		StartVoice(synth, channel, key, velocity, layer, split, part);
	}
}

// A note on a channel plays the programs of all the parts of the channel
static void NoteOn(SND_Synth* synth, int channel, uint8_t key, uint8_t velocity) {
	const SND_Bank* bank = synth->bank;
	int program = synth->channels[channel].program;
	for (uint32_t i = 0; i < bank->partCount; i++) {
		const SND_Part* part = &bank->parts[i];
		if (part->channel != channel)
			continue;
		uint32_t index = program >= 0 ? (uint32_t)program : part->program;
		if (index < bank->programCount)
			StartNote(synth, &bank->programs[index], channel, key, velocity, part);
	}
}

static void NoteOff(SND_Synth* synth, int channel, uint8_t key) {
	for (int i = 0; i < SND_SYNTH_VOICES; i++) {
		Voice* voice = &synth->voices[i];
		if (!voice->active || voice->keyOff || voice->channel != channel || voice->key != key)
			continue;
		if (synth->channels[channel].sustain)
			voice->sustained = true;
		else
			KeyOff(voice);
	}
}

static void ResetChannels(SND_Synth* synth) {
	const SND_Bank* bank = synth->bank;
	for (int c = 0; c < CHANNEL_COUNT; c++) {
		Channel* channel = &synth->channels[c];
		memset(channel, 0, sizeof(*channel));
		channel->volume = c < SND_MAX_CHANNELS ? 100 : 127;
		channel->expression = 127;
		channel->pan = 64;
		channel->bend = 64;
		channel->program = -1;
	}
	// The volume of the part is the initial volume of its channel
	for (int i = (int)bank->partCount - 1; i >= 0; i--)
		synth->channels[bank->parts[i].channel].volume = bank->parts[i].volume;
}

static void Controller(SND_Synth* synth, int c, uint8_t controller, uint8_t value) {
	Channel* channel = &synth->channels[c];
	switch (controller) {
	case 1:
		channel->modulation = value;
		break;
	case 7:
		channel->volume = value;
		break;
	case 10:
		channel->pan = value;
		channel->panSet = true;
		break;
	case 11:
		channel->expression = value;
		break;
	case 64:
		channel->sustain = value >= 64;
		if (!channel->sustain) {
			for (int i = 0; i < SND_SYNTH_VOICES; i++) {
				Voice* voice = &synth->voices[i];
				if (voice->active && voice->channel == c && voice->sustained)
					KeyOff(voice);
			}
		}
		break;
	case 121: // Reset all controllers
		channel->expression = 127;
		channel->modulation = 0;
		channel->bend = 64;
		channel->sustain = false;
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Sequencer

static void StopSequence(SND_Synth* synth) {
	synth->sequencer.active = false;
}

static void MusicEvent(SND_Synth* synth, const SND_Event* event) {
	int channel = event->status & 0x0F;
	switch (event->status >> 4) {
	case 0x8:
		NoteOff(synth, channel, event->data[0]);
		break;
	case 0x9:
		if (event->data[1] == 0)
			NoteOff(synth, channel, event->data[0]);
		else
			NoteOn(synth, channel, event->data[0], event->data[1]);
		break;
	case 0xB:
		Controller(synth, channel, event->data[0], event->data[1]);
		break;
	case 0xC:
		synth->channels[channel].program = event->data[0];
		break;
	case 0xE:
		synth->channels[channel].bend = event->data[0];
		break;
	}
}

// Effects: Dx starts the effect data[0] of the effect program with the volume data[1] (0 stops it), Ax is a controller
static void EffectEvent(SND_Synth* synth, const SND_Event* event) {
	int channel = SND_MAX_CHANNELS + (event->status & 0x0F);
	const SND_Bank* bank = synth->bank;
	switch (event->status >> 4) {
	case 0xD:
		if (event->data[1] == 0) {
			for (int i = 0; i < SND_SYNTH_VOICES; i++) {
				Voice* voice = &synth->voices[i];
				if (voice->active && voice->channel == channel && voice->key == event->data[0])
					KeyOff(voice);
			}
		}
		else if (bank->hasEffectProgram)
			StartNote(synth, &bank->effectProgram, channel, event->data[0], event->data[1], NULL);
		break;
	case 0xA:
		if (event->data[0] == 7 || event->data[0] == 11)
			Controller(synth, channel, event->data[0], event->data[1]);
		break;
	}
}

// Moves to the next track of the playlist. Returns false at the end of the playlist
static bool NextTrack(SND_Synth* synth) {
	Sequencer* sequencer = &synth->sequencer;
	if (sequencer->last)
		return false;
	for (int i = 0; i < 64; i++) {
		uint32_t word = SND_ReadU32(synth->bank, sequencer->start + sequencer->word);
		if (SND_PLAYLIST_COMMAND(word))
			sequencer->word += 4;
		else if (SND_PLAYLIST_JUMP(word))
			sequencer->word = word & 0xFFFFFF;
		else if (SND_PLAYLIST_TRACK(word) || SND_PLAYLIST_LAST(word)) {
			sequencer->pos = sequencer->start + (word & 0xFFFFFF);
			sequencer->status = 0;
			sequencer->last = SND_PLAYLIST_LAST(word);
			sequencer->word += 4;
			return true;
		}
		else
			return false;
	}
	return false;
}

static void UpdateSequencer(SND_Synth* synth) {
	Sequencer* sequencer = &synth->sequencer;
	for (int count = 0; sequencer->active && sequencer->wait <= 0.0; count++) {
		if (count >= MAX_EVENTS_PER_SAMPLE) {
			StopSequence(synth);
			return;
		}
		if (sequencer->pos == 0 && (sequencer->effect || !NextTrack(synth))) {
			StopSequence(synth);
			return;
		}
		SND_Event event;
		uint32_t next = SND_ReadEvent(synth->bank, sequencer->pos, sequencer->effect, &sequencer->status, &event);
		if (next == 0) {
			StopSequence(synth);
			return;
		}
		sequencer->pos = next;
		if (event.status == SND_EVENT_END) {
			sequencer->pos = 0;
			continue;
		}
		if (sequencer->effect)
			EffectEvent(synth, &event);
		else
			MusicEvent(synth, &event);
		sequencer->wait += event.delta * SAMPLES_PER_MS;
	}
	sequencer->wait -= 1.0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mixing

// Pitch, LFO and levels of a voice, every control period
static void UpdateVoiceControl(SND_Synth* synth, Voice* voice) {
	if (voice->channel < 0) {
		voice->gainLeft = voice->gainRight = voice->baseGain;
		voice->gainSend = 0.0f;
		return;
	}
	const Channel* channel = &synth->channels[voice->channel];
	voice->lfoPhase += voice->lfoStep;
	voice->lfoPhase -= floorf(voice->lfoPhase);

	float pitchDepth = fmaxf(voice->pitchDepth, channel->modulation * MODULATION_CENTS / 127.0f);
	// The bend goes up to (63 + 1) / 64 of the range up, and down to -64 / 64 of the range down
	int bend = channel->bend - 64;
	float semitones = voice->pitch + (bend > 0 ? (bend + 1) * voice->bendUp : bend * voice->bendDown) / 64.0f;
	if (pitchDepth > 0.0f) {
		// A sawtooth vibrato sounds wrong: the modulation wheel uses a triangle
		int wave = voice->pitchDepth > 0.0f ? voice->pitchWave : 2;
		semitones += pitchDepth / 100.0f * LfoWave(wave, voice->lfoPhase);
	}
	voice->step = exp2f(semitones / 12.0f) * voice->rateFactor;

	if (voice->filter)
		UpdateFilterEnvelope(voice);

	int level = ScaleLevel(ScaleLevel(voice->velocityLevel, channel->volume), channel->expression);
	float gain = voice->baseGain * TotalLevelGain(synth, level);
	if (voice->ampDepth > 0.0f)
		gain *= powf(10.0f, -voice->ampDepth * (LfoWave(voice->ampWave, voice->lfoPhase) + 1.0f) * 0.5f / 20.0f);
	float left, right;
	PanGains(voice->pan, &left, &right);
	if (channel->panSet) {
		left *= fminf(1.0f, (127 - channel->pan) / 63.0f);
		right *= fminf(1.0f, channel->pan / 64.0f);
	}
	voice->gainLeft = gain * left;
	voice->gainRight = gain * right;
	voice->gainSend = gain * voice->sendGain;
}

static void UpdateEnvelope(Voice* voice) {
	switch (voice->state) {
	case ENVELOPE_ATTACK:
		voice->level += voice->attackStep;
		if (voice->level >= 1023.0f) {
			voice->level = 1023.0f;
			voice->state = ENVELOPE_DECAY1;
		}
		break;
	case ENVELOPE_DECAY1:
		voice->level -= voice->decay1Step;
		if (voice->level <= voice->decayLevel)
			voice->state = ENVELOPE_DECAY2;
		break;
	case ENVELOPE_DECAY2:
		voice->level -= voice->decay2Step;
		break;
	case ENVELOPE_RELEASE:
		voice->level -= voice->releaseStep;
		break;
	}
	if (voice->level <= 0.0f && voice->state != ENVELOPE_ATTACK)
		voice->active = false;
}

// Next sample of a voice, with linear interpolation, or false at the end of the sample
static bool VoiceSample(Voice* voice, float* value) {
	const SND_Sample* sample = voice->sample;
	uint32_t end = voice->loop ? sample->loopEnd : sample->frameCount;
	// A loop of a constant value, or the tail loop of an effect, would keep the voice sounding forever: it fades out
	if (voice->loop && voice->useEnvelope && !voice->fading && voice->position >= sample->loopStart &&
		(sample->silentLoop || (sample->tailLoop && voice->channel >= SND_MAX_CHANNELS))) {
		voice->fading = true;
		voice->state = ENVELOPE_RELEASE;
		voice->releaseStep = fmaxf(voice->releaseStep, 1023.0f / (FADE_MS * (float)SAMPLES_PER_MS));
	}
	while (voice->position >= end) {
		if (!voice->loop)
			return false;
		voice->position -= sample->loopEnd - sample->loopStart;
	}
	uint32_t index = (uint32_t)voice->position;
	float fraction = (float)(voice->position - index);
	int16_t a = sample->pcm[index];
	int16_t b = index + 1 < end ? sample->pcm[index + 1] : (voice->loop ? sample->pcm[sample->loopStart] : 0);
	*value = a + (b - a) * fraction;
	voice->position += voice->step;
	return true;
}

static int16_t Clip16(float value) {
	if (value > 32767.0f) return 32767;
	if (value < -32768.0f) return -32768;
	return (int16_t)value;
}

void SND_SynthRender(SND_Synth* synth, int16_t* output, uint32_t frames) {
	const SND_DSP* program = &synth->bank->dsp;
	for (uint32_t f = 0; f < frames; f++) {
		UpdateSequencer(synth);
		bool control = synth->controlCounter == 0;
		synth->controlCounter = (synth->controlCounter + 1) % CONTROL_PERIOD;

		float left = 0.0f, right = 0.0f;
		for (int i = 0; i < SND_SYNTH_VOICES; i++) {
			Voice* voice = &synth->voices[i];
			if (!voice->active)
				continue;
			if (control)
				UpdateVoiceControl(synth, voice);
			float value;
			if (!VoiceSample(voice, &value)) {
				voice->active = false;
				continue;
			}
			if (voice->filter) {
				float filtered = voice->b0 * value + voice->b1 * voice->x1 + voice->b2 * voice->x2 -
					voice->a1 * voice->y1 - voice->a2 * voice->y2;
				voice->x2 = voice->x1;
				voice->x1 = value;
				voice->y2 = voice->y1;
				voice->y1 = filtered;
				value = filtered;
			}
			float envelope = 1.0f;
			if (voice->useEnvelope) {
				UpdateEnvelope(voice);
				int level = (int)voice->level;
				envelope = synth->levelTable[level < 0 ? 0 : (level > 1023 ? 1023 : level)];
			}
			value *= envelope;
			left += value * voice->gainLeft;
			right += value * voice->gainRight;
			if (voice->gainSend > 0.0f)
				synth->dsp.mixs[voice->isel] += (int32_t)(value * voice->gainSend * 16.0f); // 20 bits
			voice->meter = envelope * (voice->gainLeft + voice->gainRight) * 0.5f;
		}

		if (program->present && synth->dsp.stepCount > 0) {
			DSP_Step(&synth->dsp, program);
			for (int i = 0; i < 16; i++) {
				left += synth->dsp.efreg[i] * synth->outputLeft[i];
				right += synth->dsp.efreg[i] * synth->outputRight[i];
			}
		}
		else
			memset(synth->dsp.mixs, 0, sizeof(synth->dsp.mixs));

		output[f * 2] = Clip16(left * MASTER_GAIN);
		output[f * 2 + 1] = Clip16(right * MASTER_GAIN);
		synth->time++;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

SND_Synth* SND_SynthCreate(const SND_Bank* bank) {
	SND_Synth* synth = calloc(1, sizeof(SND_Synth));
	if (synth == NULL)
		return NULL;
	synth->bank = bank;
	for (int i = 0; i < 1024; i++)
		synth->levelTable[i] = powf(10.0f, -(1023 - i) * (96.0f / 1024.0f) / 20.0f);
	for (int i = 0; i < 256; i++) {
		float db = 0.0f;
		for (int bit = 0; bit < 8; bit++)
			db += (i >> bit) & 1 ? g_totalLevelDb[bit] : 0.0f;
		synth->totalLevelTable[i] = powf(10.0f, -db / 20.0f);
	}
	const SND_DSP* program = &bank->dsp;
	if (program->present) {
		// The program ends after its last step that isn't 0
		for (int step = 0; step < 128; step++) {
			if (program->mpro[step][0] | program->mpro[step][1] | program->mpro[step][2] | program->mpro[step][3])
				synth->dsp.stepCount = step + 1;
		}
		for (int i = 0; i < 16; i++) {
			float gain = LevelGain((program->outputs[i] >> 8) & 0x0F);
			PanGains(program->outputs[i] & 0x1F, &synth->outputLeft[i], &synth->outputRight[i]);
			synth->outputLeft[i] *= gain;
			synth->outputRight[i] *= gain;
		}
	}
	ResetChannels(synth);
	return synth;
}

void SND_SynthDestroy(SND_Synth* synth) {
	free(synth);
}

void SND_SynthStop(SND_Synth* synth) {
	StopSequence(synth);
	memset(synth->voices, 0, sizeof(synth->voices));
	memset((void*)synth->panLayers, 0, sizeof(synth->panLayers));
	ResetChannels(synth);
	synth->time = 0;
}

void SND_SynthPlaySequence(SND_Synth* synth, uint32_t index) {
	SND_SynthStop(synth);
	const SND_Bank* bank = synth->bank;
	if (index >= bank->sequenceCount || bank->sequences[index].type == SND_SEQUENCE_EMPTY)
		return;
	const SND_Sequence* sequence = &bank->sequences[index];
	Sequencer* sequencer = &synth->sequencer;
	memset(sequencer, 0, sizeof(*sequencer));
	sequencer->active = true;
	sequencer->effect = sequence->type == SND_SEQUENCE_EFFECT;
	sequencer->start = sequence->offset;
	sequencer->word = 4; // The playlist starts after the first word
	if (sequencer->effect)
		sequencer->pos = sequence->offset + 1; // After the header byte
}

void SND_SynthPlaySample(SND_Synth* synth, uint32_t index, uint32_t rate, bool loop) {
	SND_SynthStop(synth);
	const SND_Bank* bank = synth->bank;
	if (index >= bank->sampleCount || bank->samples[index].pcm == NULL)
		return;
	Voice* voice = &synth->voices[0];
	voice->active = true;
	voice->sample = &bank->samples[index];
	voice->channel = -1;
	voice->loop = loop && voice->sample->loop;
	voice->step = (double)rate / SND_SYNTH_RATE;
	voice->baseGain = 1.0f / MASTER_GAIN; // The sample at its own level
}

uint32_t SND_SynthSamplePosition(const SND_Synth* synth) {
	const Voice* voice = &synth->voices[0];
	if (voice->sample == NULL || voice->channel >= 0)
		return 0;
	return voice->active ? (uint32_t)voice->position : voice->sample->frameCount;
}

void SND_SynthSeekSample(SND_Synth* synth, uint32_t frame) {
	Voice* voice = &synth->voices[0];
	if (voice->sample != NULL && voice->channel < 0 && frame < voice->sample->frameCount)
		voice->position = frame;
}

void SND_SynthSetSampleLoop(SND_Synth* synth, bool loop) {
	Voice* voice = &synth->voices[0];
	if (voice->sample != NULL && voice->channel < 0)
		voice->loop = loop && voice->sample->loop;
}

bool SND_SynthIsPlaying(const SND_Synth* synth) {
	if (synth->sequencer.active)
		return true;
	for (int i = 0; i < SND_SYNTH_VOICES; i++) {
		if (synth->voices[i].active)
			return true;
	}
	return false;
}

uint32_t SND_SynthTime(const SND_Synth* synth) {
	return (uint32_t)(synth->time * 1000 / SND_SYNTH_RATE);
}

uint32_t SND_SynthActiveVoices(const SND_Synth* synth) {
	uint32_t count = 0;
	for (int i = 0; i < SND_SYNTH_VOICES; i++)
		count += synth->voices[i].active;
	return count;
}

void SND_SynthChannelLevels(const SND_Synth* synth, float levels[SND_MAX_CHANNELS]) {
	for (int c = 0; c < SND_MAX_CHANNELS; c++)
		levels[c] = 0.0f;
	for (int i = 0; i < SND_SYNTH_VOICES; i++) {
		const Voice* voice = &synth->voices[i];
		if (!voice->active || voice->channel < 0)
			continue;
		int c = voice->channel % SND_MAX_CHANNELS;
		levels[c] = fmaxf(levels[c], fminf(1.0f, voice->meter));
	}
}

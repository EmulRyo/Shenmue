#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "snd.h"
#include "sndSynth.h"
#include "sndView.h"

// Columns of the precomputed waveform: the minimum and maximum sample of each range of frames
#define SNDVIEW_PEAKS 4096
#define SNDVIEW_LABEL_SIZE 64
#define SNDVIEW_LIST_WIDTH 300.0f
#define SNDVIEW_DEFAULT_RATE 22050 // For the samples that no split plays

typedef enum {
	MODE_SEQUENCES,
	MODE_SAMPLES,
} Mode;

// Commands sent to the audio thread, which owns the synthesizer while the stream plays
typedef enum {
	COMMAND_NONE,
	COMMAND_SEQUENCE,
	COMMAND_SAMPLE,
	COMMAND_SEEK,
	COMMAND_LOOP,
	COMMAND_STOP,
} Command;

static const uint32_t g_sampleRates[] = { 0, 11025, 22050, 44100 }; // 0: the rate of the split

typedef struct {
	bool			open;
	char			filename[256];
	SND_Bank		bank;
	SND_Synth*		synth;
	AudioStream		stream;
	Mode			mode;
	// Lists: the sequences that aren't empty, and the samples
	uint32_t*		sequenceIndices;
	uint32_t		listedSequences;
	char			(*labels[2])[SNDVIEW_LABEL_SIZE];
	const char**	labelPointers[2];
	int				listScroll[2];
	int				listFocus;
	int				selected[2];
	int				rateIndex;
	bool			loopEnabled;
	// Shared with the audio thread
	volatile Command	command;
	volatile uint32_t	commandValue;
	volatile uint32_t	commandRate;
	volatile uint32_t	commandPosition;
	volatile bool		paused;
	volatile bool		playing;
	volatile uint32_t	time;
	volatile uint32_t	samplePosition;
	volatile uint32_t	voices;
	volatile float		levels[SND_MAX_CHANNELS];
	// Waveform of the selected sample
	int16_t			peakMin[SNDVIEW_PEAKS];
	int16_t			peakMax[SNDVIEW_PEAKS];
	uint32_t		peakCount;
	bool			seeking;  // Dragging on the waveform
} SNDView_State;

static SNDView_State g_player;

// Applies the command of the user interface, then renders the next frames, or silence when paused
static void StreamCallback(void* buffer, unsigned int frames) {
	SND_Synth* synth = g_player.synth;
	Command command = g_player.command;
	switch (command) {
	case COMMAND_SEQUENCE:
		SND_SynthPlaySequence(synth, g_player.commandValue);
		break;
	case COMMAND_SAMPLE:
		SND_SynthPlaySample(synth, g_player.commandValue, g_player.commandRate, g_player.loopEnabled);
		SND_SynthSeekSample(synth, g_player.commandPosition);
		break;
	case COMMAND_SEEK:
		SND_SynthSeekSample(synth, g_player.commandPosition);
		break;
	case COMMAND_LOOP:
		SND_SynthSetSampleLoop(synth, g_player.loopEnabled);
		break;
	case COMMAND_STOP:
		SND_SynthStop(synth);
		break;
	default:
		break;
	}
	g_player.command = COMMAND_NONE;

	if (g_player.paused)
		memset(buffer, 0, (size_t)frames * 2 * sizeof(int16_t));
	else
		SND_SynthRender(synth, buffer, frames);
	g_player.playing = SND_SynthIsPlaying(synth);
	g_player.time = SND_SynthTime(synth);
	g_player.samplePosition = SND_SynthSamplePosition(synth);
	g_player.voices = SND_SynthActiveVoices(synth);
	float levels[SND_MAX_CHANNELS];
	SND_SynthChannelLevels(synth, levels);
	for (int i = 0; i < SND_MAX_CHANNELS; i++)
		g_player.levels[i] = levels[i];
}

static void SendCommand(Command command, uint32_t value) {
	g_player.commandValue = value;
	g_player.command = command;
}

static uint32_t SampleRate(uint32_t sample) {
	if (g_sampleRates[g_player.rateIndex] != 0)
		return g_sampleRates[g_player.rateIndex];
	uint32_t rate = g_player.bank.samples[sample].sampleRate;
	return rate != 0 ? rate : SNDVIEW_DEFAULT_RATE;
}

// Computes the minimum and maximum of the selected sample for each column of the waveform
static void ComputePeaks(void) {
	const SND_Sample* sample = &g_player.bank.samples[g_player.selected[MODE_SAMPLES]];
	uint32_t frameCount = sample->frameCount;
	g_player.peakCount = frameCount < SNDVIEW_PEAKS ? frameCount : SNDVIEW_PEAKS;
	for (uint32_t i = 0; i < g_player.peakCount; i++) {
		uint32_t start = (uint32_t)((uint64_t)i * frameCount / g_player.peakCount);
		uint32_t end = (uint32_t)((uint64_t)(i + 1) * frameCount / g_player.peakCount);
		int16_t min = 0, max = 0;
		for (uint32_t frame = start; frame < end; frame++) {
			int16_t value = sample->pcm[frame];
			if (value < min) min = value;
			if (value > max) max = value;
		}
		g_player.peakMin[i] = min;
		g_player.peakMax[i] = max;
	}
}

static int ItemCount(Mode mode) {
	return (int)(mode == MODE_SEQUENCES ? g_player.listedSequences : g_player.bank.sampleCount);
}

static void PlaySample(uint32_t position) {
	uint32_t sample = g_player.selected[MODE_SAMPLES];
	g_player.commandRate = SampleRate(sample);
	g_player.commandPosition = position;
	g_player.paused = false;
	SendCommand(COMMAND_SAMPLE, sample);
}

// Selects an item of the list of the current mode and plays it from the start
static void Select(int index) {
	if (index < 0 || index >= ItemCount(g_player.mode))
		return;
	g_player.selected[g_player.mode] = index;
	g_player.paused = false;
	if (g_player.mode == MODE_SEQUENCES)
		SendCommand(COMMAND_SEQUENCE, g_player.sequenceIndices[index]);
	else {
		ComputePeaks();
		PlaySample(0);
	}
}

static void SetMode(Mode mode) {
	if (mode == g_player.mode || ItemCount(mode) == 0)
		return;
	g_player.mode = mode;
	g_player.listFocus = -1;
	Select(g_player.selected[mode]);
}

void SNDView_Close(void) {
	if (IsAudioStreamValid(g_player.stream)) {
		StopAudioStream(g_player.stream);
		UnloadAudioStream(g_player.stream);
	}
	SND_SynthDestroy(g_player.synth);
	SND_Free(&g_player.bank);
	for (int i = 0; i < 2; i++) {
		free(g_player.labels[i]);
		free(g_player.labelPointers[i]);
	}
	free(g_player.sequenceIndices);
	memset(&g_player, 0, sizeof(g_player));
}

static const char* FormatMilliseconds(uint32_t milliseconds) {
	uint32_t centiseconds = milliseconds / 10;
	return TextFormat("%u:%02u.%02u", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

static bool CreateLabels(void) {
	const SND_Bank* bank = &g_player.bank;
	g_player.sequenceIndices = malloc((bank->sequenceCount + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < bank->sequenceCount; i++) {
		if (bank->sequences[i].type != SND_SEQUENCE_EMPTY)
			g_player.sequenceIndices[g_player.listedSequences++] = i;
	}
	uint32_t counts[2] = { g_player.listedSequences, bank->sampleCount };
	for (int m = 0; m < 2; m++) {
		g_player.labels[m] = malloc((counts[m] + 1) * sizeof(*g_player.labels[m]));
		g_player.labelPointers[m] = malloc((counts[m] + 1) * sizeof(*g_player.labelPointers[m]));
		if (g_player.sequenceIndices == NULL || g_player.labels[m] == NULL || g_player.labelPointers[m] == NULL)
			return false;
		for (uint32_t i = 0; i < counts[m]; i++)
			g_player.labelPointers[m][i] = g_player.labels[m][i];
	}
	for (uint32_t i = 0; i < g_player.listedSequences; i++) {
		const SND_Sequence* sequence = &bank->sequences[g_player.sequenceIndices[i]];
		if (sequence->type == SND_SEQUENCE_MUSIC)
			snprintf(g_player.labels[MODE_SEQUENCES][i], SNDVIEW_LABEL_SIZE, "%u-%u: music, %u:%02u%s", sequence->group,
				sequence->index, sequence->duration / 60000, sequence->duration / 1000 % 60, sequence->loop ? ", loop" : "");
		else
			snprintf(g_player.labels[MODE_SEQUENCES][i], SNDVIEW_LABEL_SIZE, "%u-%u: effect", sequence->group, sequence->index);
	}
	for (uint32_t i = 0; i < bank->sampleCount; i++) {
		const SND_Sample* sample = &bank->samples[i];
		snprintf(g_player.labels[MODE_SAMPLES][i], SNDVIEW_LABEL_SIZE, "%u: %s, %u frames%s", i,
			sample->encoding == SND_ENCODING_ADPCM ? "ADPCM" : "PCM", sample->frameCount, sample->loop ? ", loop" : "");
	}
	return true;
}

bool SNDView_Open(const char* filepath) {
	SND_Bank bank;
	if (!SND_Load(filepath, &bank))
		return false;

	SNDView_Close();
	if (!IsAudioDeviceReady())
		InitAudioDevice();
	snprintf(g_player.filename, sizeof(g_player.filename), "%s", GetFileName(filepath));
	g_player.bank = bank;
	g_player.loopEnabled = true;
	g_player.listFocus = -1;
	g_player.synth = SND_SynthCreate(&g_player.bank);
	if (g_player.synth == NULL || !CreateLabels()) {
		SNDView_Close();
		return false;
	}
	g_player.mode = g_player.listedSequences > 0 ? MODE_SEQUENCES : MODE_SAMPLES;
	if (ItemCount(g_player.mode) == 0) {
		printf("%s: the bank has nothing to play\n", g_player.filename);
		SNDView_Close();
		return false;
	}

	g_player.stream = LoadAudioStream(SND_SYNTH_RATE, 16, 2);
	if (!IsAudioStreamValid(g_player.stream)) {
		fputs("[SNDView_Open]: Failed to create the audio stream\n", stderr);
		SNDView_Close();
		return false;
	}
	SetAudioStreamCallback(g_player.stream, StreamCallback);
	Select(0);
	PlayAudioStream(g_player.stream);

	printf("%s: DTPK sound bank, %u sequences (%u not empty), %u programs, %u parts, %u samples%s\n", g_player.filename,
		bank.sequenceCount, g_player.listedSequences, bank.programCount, bank.partCount, bank.sampleCount,
		bank.dsp.present ? ", DSP program" : "");
	g_player.open = true;
	return true;
}

bool SNDView_IsOpen(void) {
	return g_player.open;
}

static const SND_Sample* SelectedSample(void) {
	return &g_player.bank.samples[g_player.selected[MODE_SAMPLES]];
}

static void TogglePlay(void) {
	if (!g_player.playing && !g_player.paused)
		Select(g_player.selected[g_player.mode]); // Play again from the start
	else
		g_player.paused = !g_player.paused;
}

static void Seek(int64_t frame) {
	const SND_Sample* sample = SelectedSample();
	if (frame < 0)
		frame = 0;
	if (frame >= sample->frameCount)
		frame = sample->frameCount > 0 ? sample->frameCount - 1 : 0;
	g_player.commandPosition = (uint32_t)frame;
	// A sample that has ended plays again from the new position
	if (!g_player.playing)
		PlaySample((uint32_t)frame);
	else
		SendCommand(COMMAND_SEEK, 0);
}

static void UpdateInput(Rectangle waveform) {
	if (IsKeyPressed(KEY_SPACE))
		TogglePlay();
	if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
		Select(g_player.selected[g_player.mode] - 1);
	if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
		Select(g_player.selected[g_player.mode] + 1);
	if (IsKeyPressed(KEY_HOME))
		Select(g_player.selected[g_player.mode]);
	if (IsKeyPressed(KEY_TAB))
		SetMode(g_player.mode == MODE_SEQUENCES ? MODE_SAMPLES : MODE_SEQUENCES);
	if (g_player.mode != MODE_SAMPLES)
		return;

	int64_t second = SampleRate(g_player.selected[MODE_SAMPLES]);
	if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
		Seek((int64_t)g_player.samplePosition - second);
	if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
		Seek((int64_t)g_player.samplePosition + second);
	if (IsKeyPressed(KEY_L)) {
		g_player.loopEnabled = !g_player.loopEnabled;
		SendCommand(COMMAND_LOOP, 0);
	}

	Vector2 mouse = GetMousePosition();
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, waveform))
		g_player.seeking = true;
	if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
		g_player.seeking = false;
	if (g_player.seeking) {
		float t = (mouse.x - waveform.x) / waveform.width;
		Seek((int64_t)(t * SelectedSample()->frameCount));
	}
}

static void DrawWaveform(Rectangle area) {
	const SND_Sample* sample = SelectedSample();
	DrawRectangleRec(area, (Color) { 30, 30, 32, 255 });
	float center = area.y + area.height * 0.5f;
	float scale = area.height * 0.5f / 32768.0f;
	DrawLineV((Vector2) { area.x, center }, (Vector2) { area.x + area.width, center }, (Color) { 70, 70, 75, 255 });
	if (sample->frameCount == 0) {
		DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
		return;
	}

	// Loop region
	if (sample->loop) {
		float loopStartX = area.x + area.width * sample->loopStart / (float)sample->frameCount;
		float loopEndX = area.x + area.width * sample->loopEnd / (float)sample->frameCount;
		Color color = g_player.loopEnabled ? (Color) { 60, 50, 30, 255 } : (Color) { 42, 42, 44, 255 };
		DrawRectangleRec((Rectangle) { loopStartX, area.y, fmaxf(loopEndX - loopStartX, 1.0f), area.height }, color);
		DrawLineV((Vector2) { loopStartX, area.y }, (Vector2) { loopStartX, area.y + area.height }, ORANGE);
		DrawLineV((Vector2) { loopEndX, area.y }, (Vector2) { loopEndX, area.y + area.height }, ORANGE);
	}

	int width = (int)area.width;
	float playedX = area.x + area.width * g_player.samplePosition / (float)sample->frameCount;
	for (int x = 0; x < width && g_player.peakCount > 0; x++) {
		uint32_t start = (uint32_t)((uint64_t)x * g_player.peakCount / width);
		uint32_t end = (uint32_t)((uint64_t)(x + 1) * g_player.peakCount / width);
		if (end <= start)
			end = start + 1;
		int16_t min = 0, max = 0;
		for (uint32_t i = start; i < end; i++) {
			if (g_player.peakMin[i] < min) min = g_player.peakMin[i];
			if (g_player.peakMax[i] > max) max = g_player.peakMax[i];
		}
		float px = area.x + x;
		Color color = px < playedX ? (Color) { 90, 170, 255, 255 } : (Color) { 140, 140, 150, 255 };
		float top = floorf(center - max * scale);
		DrawRectangleRec((Rectangle) { px, top, 1.0f, fmaxf(ceilf(center - min * scale) - top, 1.0f) }, color);
	}
	DrawLineV((Vector2) { playedX, area.y }, (Vector2) { playedX, area.y + area.height }, WHITE);
	DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

// Level of the voices of each MIDI channel
static void DrawChannelMeters(Rectangle area, Font font, float fontSize) {
	DrawRectangleRec(area, (Color) { 30, 30, 32, 255 });
	float labelHeight = fontSize * 1.2f;
	float slot = area.width / SND_MAX_CHANNELS;
	float barHeight = area.height - labelHeight - 10.0f;
	for (int c = 0; c < SND_MAX_CHANNELS; c++) {
		float level = g_player.levels[c];
		// Show the level in dB, from -48 dB to 0 dB
		float height = level > 0.0f ? fmaxf(0.0f, 1.0f + 20.0f * log10f(level) / 48.0f) * barHeight : 0.0f;
		Rectangle bar = { area.x + c * slot + slot * 0.2f, area.y + 5.0f, slot * 0.6f, barHeight };
		DrawRectangleRec(bar, (Color) { 42, 42, 46, 255 });
		DrawRectangleRec((Rectangle) { bar.x, bar.y + bar.height - height, bar.width, height }, (Color) { 90, 170, 255, 255 });
		const char* label = TextFormat("%d", c);
		Vector2 size = MeasureTextEx(font, label, fontSize, 1.0f);
		DrawTextEx(font, label, (Vector2) { bar.x + (bar.width - size.x) * 0.5f, bar.y + barHeight + 3.0f }, fontSize, 1.0f, LIGHTGRAY);
	}
	DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

void SNDView_Draw(Rectangle area) {
	if (!g_player.open || area.width <= 0.0f || area.height <= 0.0f)
		return;

	Font font = GuiGetFont();
	float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
	float lineHeight = fontSize * 1.2f;
	const SND_Bank* bank = &g_player.bank;
	uint32_t music = 0;
	for (uint32_t i = 0; i < bank->sequenceCount; i++)
		music += bank->sequences[i].type == SND_SEQUENCE_MUSIC;
	const char* lines[3] = {
		g_player.filename,
		TextFormat("DTPK sound bank, %u music and %u effect sequences, %u programs, %u samples%s", music,
			g_player.listedSequences - music, bank->programCount, bank->sampleCount, bank->dsp.present ? ", reverb" : ""),
		g_player.mode == MODE_SEQUENCES ?
			"Space: play/pause   Up/Down: previous/next sequence   Home: start   Tab: samples" :
			"Space: play/pause   Up/Down: previous/next sample   Left/Right: back/forward 1 s   Home: start   L: loop   Tab: sequences   Click on the waveform: seek",
	};

	float margin = 10.0f;
	float controlsHeight = 30.0f;
	float top = area.y + margin * 2.0f + 3 * lineHeight;
	Rectangle modes = { area.x + margin, top, SNDVIEW_LIST_WIDTH, controlsHeight };
	Rectangle list = { area.x + margin, top + controlsHeight + margin, SNDVIEW_LIST_WIDTH,
		area.height - margin * 4.0f - 3 * lineHeight - controlsHeight };
	Rectangle panel = {
		list.x + list.width + margin,
		top + lineHeight + margin,
		area.width - list.width - margin * 3.0f,
		area.height - margin * 5.0f - 4 * lineHeight - controlsHeight,
	};
	if (list.height <= 0.0f || panel.width <= 0.0f || panel.height <= 0.0f)
		return;
	UpdateInput(panel);

	DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
	for (int i = 0; i < 3; i++)
		DrawTextEx(font, lines[i], (Vector2) { area.x + margin, area.y + margin + i * lineHeight }, fontSize, 1.0f, LIGHTGRAY);

	// The style of the application draws the pressed controls like the normal ones: highlight the selected toggles
	int pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
	int mode = g_player.mode;
	float toggleWidth = (SNDVIEW_LIST_WIDTH - GuiGetStyle(TOGGLE, GROUP_PADDING)) / 2.0f;
	GuiToggleGroup((Rectangle) { modes.x, modes.y, toggleWidth, modes.height }, "Sequences;Samples", &mode);
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, pressedColor);
	SetMode((Mode)mode);

	int active = g_player.selected[g_player.mode];
	GuiListViewEx(list, g_player.labelPointers[g_player.mode], ItemCount(g_player.mode), &g_player.listScroll[g_player.mode],
		&active, &g_player.listFocus);
	if (active >= 0 && active != g_player.selected[g_player.mode])
		Select(active);

	float controlsY = panel.y + panel.height + margin;
	float x = panel.x;
	if (GuiButton((Rectangle) { x, controlsY, 100.0f, controlsHeight }, g_player.playing && !g_player.paused ? "Pause" : "Play"))
		TogglePlay();
	x += 110.0f;

	if (g_player.mode == MODE_SEQUENCES) {
		const SND_Sequence* sequence = &bank->sequences[g_player.sequenceIndices[g_player.selected[MODE_SEQUENCES]]];
		const char* info = sequence->type == SND_SEQUENCE_MUSIC ?
			TextFormat("Sequence %u-%u: music, %s%s", sequence->group, sequence->index, FormatMilliseconds(sequence->duration),
				sequence->loop ? ", then loops" : "") :
			TextFormat("Sequence %u-%u: effect", sequence->group, sequence->index);
		DrawTextEx(font, info, (Vector2) { panel.x, top }, fontSize, 1.0f, LIGHTGRAY);
		DrawChannelMeters(panel, font, fontSize);
		if (GuiButton((Rectangle) { x, controlsY, 100.0f, controlsHeight }, "Stop")) {
			g_player.paused = false;
			SendCommand(COMMAND_STOP, 0);
		}
		x += 110.0f;
		const char* status = TextFormat("%s   %u/%u voices", FormatMilliseconds(g_player.time), g_player.voices, SND_SYNTH_VOICES);
		DrawTextEx(font, status, (Vector2) { x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
		return;
	}

	const SND_Sample* sample = SelectedSample();
	uint32_t rate = SampleRate(g_player.selected[MODE_SAMPLES]);
	const char* info = TextFormat("Sample %d: %s, %u bytes at 0x%X, %u frames, %u Hz%s", g_player.selected[MODE_SAMPLES],
		sample->encoding == SND_ENCODING_ADPCM ? "4-bit ADPCM" : "16-bit PCM", sample->size, sample->offset, sample->frameCount,
		rate, sample->loop ? TextFormat(", loop %u-%u", sample->loopStart, sample->loopEnd) : ", no loop");
	DrawTextEx(font, info, (Vector2) { panel.x, top }, fontSize, 1.0f, LIGHTGRAY);
	DrawWaveform(panel);

	bool loopEnabled = g_player.loopEnabled;
	GuiCheckBox((Rectangle) { x, controlsY + (controlsHeight - 20.0f) * 0.5f, 20.0f, 20.0f }, "Loop", &loopEnabled);
	if (loopEnabled != g_player.loopEnabled) {
		g_player.loopEnabled = loopEnabled;
		SendCommand(COMMAND_LOOP, 0);
	}
	x += 90.0f;
	int rateIndex = g_player.rateIndex;
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
	GuiToggleGroup((Rectangle) { x, controlsY, 80.0f, controlsHeight }, "Auto;11025 Hz;22050 Hz;44100 Hz", &rateIndex);
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, pressedColor);
	if (rateIndex != g_player.rateIndex) {
		g_player.rateIndex = rateIndex;
		PlaySample(g_player.samplePosition);
	}
	x += 4 * 80.0f + 3 * GuiGetStyle(TOGGLE, GROUP_PADDING) + margin;
	// TextFormat uses a rotating buffer, so the two times can be formatted in the same call
	const char* time = TextFormat("%s / %s", FormatMilliseconds((uint32_t)((uint64_t)g_player.samplePosition * 1000 / rate)),
		FormatMilliseconds((uint32_t)((uint64_t)sample->frameCount * 1000 / rate)));
	DrawTextEx(font, time, (Vector2) { x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

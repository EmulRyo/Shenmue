#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "snd.h"
#include "sndView.h"

// Columns of the precomputed waveform: the minimum and maximum sample of each range of frames
#define SNDVIEW_PEAKS 4096
#define SNDVIEW_LABEL_SIZE 64
#define SNDVIEW_LIST_WIDTH 280.0f

static const uint32_t g_sampleRates[] = { 11025, 22050, 44100 };
#define SNDVIEW_DEFAULT_RATE 1

typedef struct {
	bool			open;
	char			filename[256];
	SND_Bank		bank;
	char			(*labels)[SNDVIEW_LABEL_SIZE];
	const char**	labelPointers;
	int				listScroll;
	int				listFocus;
	int				rateIndex;
	AudioStream		stream;
	// Shared with the audio thread, which advances the position while playing
	volatile int		selected;
	volatile uint32_t	position; // Frame of the selected sample
	volatile bool		playing;
	volatile bool		loopEnabled;
	int16_t			peakMin[SNDVIEW_PEAKS];
	int16_t			peakMax[SNDVIEW_PEAKS];
	uint32_t		peakCount;
	bool			seeking;  // Dragging on the waveform
} SNDView_State;

static SNDView_State g_player;

// Fills the buffer of the audio stream with the next frames of the selected sample, going back to the start of the
// loop at its end, or with silence when paused
static void StreamCallback(void* buffer, unsigned int frames) {
	int16_t* output = buffer;
	int selected = g_player.selected;
	const SND_Sample* sample = &g_player.bank.samples[selected];
	uint32_t position = g_player.position;
	// After a seek past the end of the loop, the sample plays up to its end
	bool looping = g_player.loopEnabled && sample->loop && position <= sample->loopEnd;
	uint32_t end = looping ? sample->loopEnd : sample->frameCount;
	unsigned int count = 0;
	while (g_player.playing && count < frames) {
		if (position >= end) {
			if (!looping) {
				g_player.playing = false;
				break;
			}
			position = sample->loopStart;
		}
		uint32_t chunk = end - position;
		if (chunk > frames - count)
			chunk = frames - count;
		memcpy(output + count, sample->pcm + position, (size_t)chunk * sizeof(int16_t));
		position += chunk;
		count += chunk;
	}
	// The sample may have been changed while filling the buffer: then its position has already been reset
	if (g_player.selected == selected)
		g_player.position = position;
	memset(output + count, 0, (size_t)(frames - count) * sizeof(int16_t));
}

// Computes the minimum and maximum of the selected sample for each column of the waveform
static void ComputePeaks(void) {
	const SND_Sample* sample = &g_player.bank.samples[g_player.selected];
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

static uint32_t SampleRate(void) {
	return g_sampleRates[g_player.rateIndex];
}

static void UnloadStream(void) {
	if (IsAudioStreamValid(g_player.stream)) {
		StopAudioStream(g_player.stream);
		UnloadAudioStream(g_player.stream);
	}
	g_player.stream = (AudioStream) { 0 };
}

static bool LoadStream(void) {
	UnloadStream();
	g_player.stream = LoadAudioStream(SampleRate(), 16, 1);
	if (!IsAudioStreamValid(g_player.stream)) {
		fputs("[SNDView]: Failed to create the audio stream\n", stderr);
		return false;
	}
	SetAudioStreamCallback(g_player.stream, StreamCallback);
	PlayAudioStream(g_player.stream);
	return true;
}

// Selects a sample and plays it from the start
static void Select(int index) {
	if (index < 0 || (uint32_t)index >= g_player.bank.sampleCount)
		return;
	g_player.playing = false;
	g_player.selected = index;
	g_player.position = 0;
	ComputePeaks();
	g_player.playing = g_player.bank.samples[index].frameCount > 0;
}

void SNDView_Close(void) {
	UnloadStream();
	SND_Free(&g_player.bank);
	free(g_player.labels);
	free(g_player.labelPointers);
	memset(&g_player, 0, sizeof(g_player));
}

bool SNDView_Open(const char* filepath) {
	SND_Bank bank;
	if (!SND_Load(filepath, &bank))
		return false;
	if (bank.sampleCount == 0) {
		printf("%s: the bank has no samples\n", GetFileName(filepath));
		SND_Free(&bank);
		return false;
	}

	SNDView_Close();
	if (!IsAudioDeviceReady())
		InitAudioDevice();
	snprintf(g_player.filename, sizeof(g_player.filename), "%s", GetFileName(filepath));
	g_player.bank = bank;
	g_player.rateIndex = SNDVIEW_DEFAULT_RATE;
	g_player.loopEnabled = true;
	g_player.labels = malloc(bank.sampleCount * sizeof(*g_player.labels));
	g_player.labelPointers = malloc(bank.sampleCount * sizeof(*g_player.labelPointers));
	if (g_player.labels == NULL || g_player.labelPointers == NULL) {
		SNDView_Close();
		return false;
	}
	for (uint32_t i = 0; i < bank.sampleCount; i++) {
		const SND_Sample* sample = &bank.samples[i];
		snprintf(g_player.labels[i], SNDVIEW_LABEL_SIZE, "%u: %s, %u frames%s", i,
			sample->encoding == SND_ENCODING_ADPCM ? "ADPCM" : "PCM", sample->frameCount, sample->loop ? ", loop" : "");
		g_player.labelPointers[i] = g_player.labels[i];
	}
	g_player.selected = 0;
	ComputePeaks();
	if (!LoadStream()) {
		SNDView_Close();
		return false;
	}

	printf("%s: DTPK sound bank, %u samples, %u programs, %u sequences%s\n", g_player.filename, bank.sampleCount,
		bank.programCount, bank.sequenceCount, bank.hasDSP ? ", DSP settings" : "");
	g_player.open = true;
	Select(0);
	return true;
}

bool SNDView_IsOpen(void) {
	return g_player.open;
}

static const SND_Sample* Selected(void) {
	return &g_player.bank.samples[g_player.selected];
}

static void TogglePlay(void) {
	const SND_Sample* sample = Selected();
	if (!g_player.playing && g_player.position >= sample->frameCount)
		g_player.position = 0; // Play again from the start
	g_player.playing = !g_player.playing && sample->frameCount > 0;
}

static void Seek(int64_t frame) {
	if (frame < 0)
		frame = 0;
	if (frame > Selected()->frameCount)
		frame = Selected()->frameCount;
	g_player.position = (uint32_t)frame;
}

static void SetRate(int rateIndex) {
	if (rateIndex == g_player.rateIndex)
		return;
	g_player.rateIndex = rateIndex;
	LoadStream();
}

static void UpdateInput(Rectangle waveform) {
	int64_t second = SampleRate();
	if (IsKeyPressed(KEY_SPACE))
		TogglePlay();
	if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
		Select(g_player.selected - 1);
	if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
		Select(g_player.selected + 1);
	if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
		Seek((int64_t)g_player.position - second);
	if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
		Seek((int64_t)g_player.position + second);
	if (IsKeyPressed(KEY_HOME))
		Seek(0);
	if (IsKeyPressed(KEY_L))
		g_player.loopEnabled = !g_player.loopEnabled;

	Vector2 mouse = GetMousePosition();
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, waveform))
		g_player.seeking = true;
	if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
		g_player.seeking = false;
	if (g_player.seeking) {
		float t = (mouse.x - waveform.x) / waveform.width;
		Seek((int64_t)(t * Selected()->frameCount));
	}
}

static const char* FormatTime(uint32_t frame) {
	uint32_t centiseconds = (uint32_t)((uint64_t)frame * 100 / SampleRate());
	return TextFormat("%u:%02u.%02u", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

static void DrawWaveform(Rectangle area) {
	const SND_Sample* sample = Selected();
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
	float playedX = area.x + area.width * g_player.position / (float)sample->frameCount;
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

void SNDView_Draw(Rectangle area) {
	if (!g_player.open || area.width <= 0.0f || area.height <= 0.0f)
		return;

	Font font = GuiGetFont();
	float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
	float lineHeight = fontSize * 1.2f;
	const SND_Bank* bank = &g_player.bank;
	const SND_Sample* sample = Selected();
	const char* lines[3] = {
		g_player.filename,
		TextFormat("DTPK sound bank, %u samples, %u programs, %u sequences%s", bank->sampleCount, bank->programCount,
			bank->sequenceCount, bank->hasDSP ? ", DSP settings" : ""),
		"Space: play/pause   Up/Down: previous/next sample   Left/Right: back/forward 1 s   Home: start   L: loop   Click on the waveform: seek",
	};

	float margin = 10.0f;
	float controlsHeight = 30.0f;
	float top = area.y + margin * 2.0f + 3 * lineHeight;
	Rectangle list = { area.x + margin, top, SNDVIEW_LIST_WIDTH, area.height - margin * 3.0f - 3 * lineHeight };
	Rectangle waveform = {
		list.x + list.width + margin,
		top + lineHeight + margin,
		area.width - list.width - margin * 3.0f,
		area.height - margin * 5.0f - 4 * lineHeight - controlsHeight,
	};
	if (list.height <= 0.0f || waveform.width <= 0.0f || waveform.height <= 0.0f)
		return;
	UpdateInput(waveform);

	DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
	for (int i = 0; i < 3; i++)
		DrawTextEx(font, lines[i], (Vector2) { area.x + margin, area.y + margin + i * lineHeight }, fontSize, 1.0f, LIGHTGRAY);

	int active = g_player.selected;
	GuiListViewEx(list, g_player.labelPointers, (int)bank->sampleCount, &g_player.listScroll, &active, &g_player.listFocus);
	if (active >= 0 && active != g_player.selected)
		Select(active);

	const char* info = TextFormat("Sample %d: %s, %u bytes at 0x%X, %u frames%s", g_player.selected,
		sample->encoding == SND_ENCODING_ADPCM ? "4-bit ADPCM" : "16-bit PCM", sample->size, sample->offset, sample->frameCount,
		sample->loop ? TextFormat(", loop %u-%u", sample->loopStart, sample->loopEnd) : ", no loop");
	DrawTextEx(font, info, (Vector2) { waveform.x, top }, fontSize, 1.0f, LIGHTGRAY);
	DrawWaveform(waveform);

	float controlsY = waveform.y + waveform.height + margin;
	float x = waveform.x;
	if (GuiButton((Rectangle) { x, controlsY, 100.0f, controlsHeight }, g_player.playing ? "Pause" : "Play"))
		TogglePlay();
	x += 110.0f;
	bool loopEnabled = g_player.loopEnabled;
	GuiCheckBox((Rectangle) { x, controlsY + (controlsHeight - 20.0f) * 0.5f, 20.0f, 20.0f }, "Loop", &loopEnabled);
	g_player.loopEnabled = loopEnabled;
	x += 90.0f;
	// The style of the application draws the pressed controls like the normal ones: highlight the selected rate
	int rateIndex = g_player.rateIndex;
	int pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
	GuiToggleGroup((Rectangle) { x, controlsY, 80.0f, controlsHeight }, "11025 Hz;22050 Hz;44100 Hz", &rateIndex);
	GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, pressedColor);
	SetRate(rateIndex);
	x += 3 * 80.0f + 2 * GuiGetStyle(TOGGLE, GROUP_PADDING) + margin;
	// TextFormat uses a rotating buffer, so the two times can be formatted in the same call
	const char* time = TextFormat("%s / %s", FormatTime(g_player.position), FormatTime(sample->frameCount));
	DrawTextEx(font, time, (Vector2) { x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

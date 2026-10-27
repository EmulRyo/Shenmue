#include <stdint.h>
#include <math.h>
#include <raylib.h>
#include "views/waveform.h"

void Waveform_Compute(Waveform* waveform, const int16_t* pcm, uint32_t frameCount, uint32_t channels) {
	waveform->frameCount = frameCount;
	waveform->peakCount = frameCount < WAVEFORM_PEAKS ? frameCount : WAVEFORM_PEAKS;
	for (uint32_t i = 0; i < waveform->peakCount; i++) {
		uint32_t start = (uint32_t)((uint64_t)i * frameCount / waveform->peakCount);
		uint32_t end = (uint32_t)((uint64_t)(i + 1) * frameCount / waveform->peakCount);
		int16_t min = 0, max = 0;
		for (uint32_t frame = start; frame < end; frame++) {
			int16_t sample = pcm[(size_t)frame * channels];
			if (sample < min) min = sample;
			if (sample > max) max = sample;
		}
		waveform->peakMin[i] = min;
		waveform->peakMax[i] = max;
	}
}

bool Waveform_UpdateSeek(Waveform* waveform, Rectangle area, int64_t* frame) {
	Vector2 mouse = GetMousePosition();
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, area))
		waveform->seeking = true;
	if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
		waveform->seeking = false;
	if (waveform->seeking) {
		float t = (mouse.x - area.x) / area.width;
		*frame = (int64_t)(t * waveform->frameCount);
	}
	return waveform->seeking;
}

void Waveform_Draw(const Waveform* waveform, Rectangle area, uint32_t position, const WaveformLoop* loop) {
	DrawRectangleRec(area, (Color) { 30, 30, 32, 255 });
	float center = area.y + area.height * 0.5f;
	float scale = area.height * 0.5f / 32768.0f;
	DrawLineV((Vector2) { area.x, center }, (Vector2) { area.x + area.width, center }, (Color) { 70, 70, 75, 255 });
	if (waveform->frameCount == 0) {
		DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
		return;
	}

	if (loop != NULL) {
		float loopStartX = area.x + area.width * loop->start / (float)waveform->frameCount;
		float loopEndX = area.x + area.width * loop->end / (float)waveform->frameCount;
		Color color = loop->enabled ? (Color) { 60, 50, 30, 255 } : (Color) { 42, 42, 44, 255 };
		DrawRectangleRec((Rectangle) { loopStartX, area.y, fmaxf(loopEndX - loopStartX, 1.0f), area.height }, color);
		DrawLineV((Vector2) { loopStartX, area.y }, (Vector2) { loopStartX, area.y + area.height }, ORANGE);
		DrawLineV((Vector2) { loopEndX, area.y }, (Vector2) { loopEndX, area.y + area.height }, ORANGE);
	}

	int width = (int)area.width;
	float playedX = area.x + area.width * position / (float)waveform->frameCount;
	for (int x = 0; x < width && waveform->peakCount > 0; x++) {
		uint32_t start = (uint32_t)((uint64_t)x * waveform->peakCount / width);
		uint32_t end = (uint32_t)((uint64_t)(x + 1) * waveform->peakCount / width);
		if (end <= start)
			end = start + 1;
		int16_t min = 0, max = 0;
		for (uint32_t i = start; i < end; i++) {
			if (waveform->peakMin[i] < min) min = waveform->peakMin[i];
			if (waveform->peakMax[i] > max) max = waveform->peakMax[i];
		}
		float px = area.x + x;
		Color color = px < playedX ? (Color) { 90, 170, 255, 255 } : (Color) { 140, 140, 150, 255 };
		float top = floorf(center - max * scale);
		DrawRectangleRec((Rectangle) { px, top, 1.0f, fmaxf(ceilf(center - min * scale) - top, 1.0f) }, color);
	}
	DrawLineV((Vector2) { playedX, area.y }, (Vector2) { playedX, area.y + area.height }, WHITE);
	DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

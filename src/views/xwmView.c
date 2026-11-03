#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "formats/sound/xwm.h"
#include "ui/toolbar.h"
#include "ui/waveform.h"
#include "views/xwmView.h"

typedef struct {
    bool            open;
    char            filename[256];
    XWM_Info        info;
    int16_t*        pcm;
    AudioStream     stream;
    // Shared with the audio thread, which advances the position while playing
    volatile uint32_t   position; // Frame
    volatile bool       playing;
    Waveform        waveform;
} XWMView_State;

static XWMView_State g_player;

// Fills the buffer of the audio stream with the next frames, or with silence when paused
static void StreamCallback(void* buffer, unsigned int frames) {
    int16_t* output = buffer;
    uint32_t channels = g_player.info.channels;
    uint32_t position = g_player.position;
    uint32_t count = 0;
    if (g_player.playing) {
        count = g_player.info.frameCount - position;
        if (count > frames)
            count = frames;
        memcpy(output, g_player.pcm + (size_t)position * channels, (size_t)count * channels * sizeof(int16_t));
        position += count;
        g_player.position = position;
        if (position >= g_player.info.frameCount)
            g_player.playing = false;
    }
    memset(output + (size_t)count * channels, 0, (size_t)(frames - count) * channels * sizeof(int16_t));
}

void XWMView_Close(void) {
    if (IsAudioStreamValid(g_player.stream)) {
        StopAudioStream(g_player.stream);
        UnloadAudioStream(g_player.stream);
    }
    free(g_player.pcm);
    memset(&g_player, 0, sizeof(g_player));
}

bool XWMView_Open(const char* filepath) {
    XWM_Info info;
    int16_t* pcm = XWM_Decode(filepath, &info);
    if (pcm == NULL)
        return false;

    XWMView_Close();
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    snprintf(g_player.filename, sizeof(g_player.filename), "%s", GetFileName(filepath));
    g_player.info = info;
    g_player.pcm = pcm;
    Waveform_Compute(&g_player.waveform, pcm, info.frameCount, info.channels);

    g_player.stream = LoadAudioStream(info.sampleRate, 16, info.channels);
    if (!IsAudioStreamValid(g_player.stream)) {
        fputs("[XWMView_Open]: Failed to create the audio stream\n", stderr);
        XWMView_Close();
        return false;
    }
    SetAudioStreamCallback(g_player.stream, StreamCallback);
    PlayAudioStream(g_player.stream);

    printf("%s: %u Hz, %u channel(s), %u packets of %u bytes, %.2f s\n", g_player.filename, info.sampleRate, info.channels,
        info.packetCount, info.blockAlign, (float)info.frameCount / info.sampleRate);
    g_player.playing = true;
    g_player.open = true;
    return true;
}

bool XWMView_IsOpen(void) {
    return g_player.open;
}

static void TogglePlay(void) {
    if (!g_player.playing && g_player.position >= g_player.info.frameCount)
        g_player.position = 0; // Play again from the start
    g_player.playing = !g_player.playing;
}

static void Seek(int64_t frame) {
    if (frame < 0)
        frame = 0;
    if (frame > g_player.info.frameCount)
        frame = g_player.info.frameCount;
    g_player.position = (uint32_t)frame;
}

// The same controls as the view of sound banks, with those of its lists and samples disabled
void XWMView_Toolbar(void) {
    int64_t second = g_player.info.sampleRate;
    if (Toolbar_Button(g_player.playing ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY, NULL, "Play or pause", KEY_SPACE))
        TogglePlay();
    if (Toolbar_Button(ICON_PLAYER_STOP, NULL, "Stop", 0)) {
        g_player.playing = false;
        Seek(0);
    }
    if (Toolbar_Button(ICON_PLAYER_PREVIOUS, NULL, "Go to the start", KEY_HOME))
        Seek(0);
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Back 1 second", TOOLBAR_REPEAT | KEY_LEFT))
        Seek((int64_t)g_player.position - second);
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Forward 1 second", TOOLBAR_REPEAT | KEY_RIGHT))
        Seek((int64_t)g_player.position + second);
    GuiDisable();
    Toolbar_Separator();
    Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous sample", 0);
    Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next sample", 0);
    Toolbar_Separator();
    bool loop = false;
    Toolbar_Toggle(ICON_REPEAT, "Loop", "Play the loop of the sample", 0, &loop);
    int rate = 0;
    Toolbar_Choice("Auto;11025 Hz;22050 Hz;44100 Hz", "Sample rate", 0, &rate);
    GuiEnable();
}

static void UpdateInput(Rectangle waveform) {
    int64_t frame;
    if (Waveform_UpdateSeek(&g_player.waveform, waveform, &frame))
        Seek(frame);
}

static const char* FormatTime(uint32_t frame) {
    uint32_t centiseconds = (uint32_t)((uint64_t)frame * 100 / g_player.info.sampleRate);
    return TextFormat("%u:%02u.%02u", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

void XWMView_Draw(Rectangle area) {
    if (!g_player.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float lineHeight = fontSize * 1.2f;
    const XWM_Info* info = &g_player.info;
    const char* lines[3] = {
        g_player.filename,
        TextFormat("xWMA (WMA v2), %u Hz, %s, %u bytes/s, %u packets", info->sampleRate, info->channels == 1 ? "mono" : "stereo",
            info->avgBytesPerSec, info->packetCount),
        "Click on the waveform: seek",
    };

    float margin = 10.0f;
    float controlsHeight = 30.0f;
    Rectangle waveform = {
        area.x + margin,
        area.y + margin * 2.0f + 3 * lineHeight,
        area.width - margin * 2.0f,
        area.height - margin * 4.0f - 3 * lineHeight - controlsHeight,
    };
    if (waveform.width <= 0.0f || waveform.height <= 0.0f)
        return;
    UpdateInput(waveform);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    for (int i = 0; i < 3; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + margin, area.y + margin + i * lineHeight }, fontSize, 1.0f, LIGHTGRAY);
    Waveform_Draw(&g_player.waveform, waveform, g_player.position, NULL);

    float controlsY = waveform.y + waveform.height + margin;
    // TextFormat uses a rotating buffer, so the two times can be formatted in the same call
    const char* time = TextFormat("%s / %s", FormatTime(g_player.position), FormatTime(info->frameCount));
    DrawTextEx(font, time, (Vector2) { waveform.x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

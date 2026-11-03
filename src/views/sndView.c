#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "formats/sound/snd.h"
#include "audio/sndSynth.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "ui/waveform.h"
#include "views/sndView.h"

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
    bool            open;
    char            path[1024];  // Of the open file
    SND_Bank        bank;
    SND_Synth*      synth;
    AudioStream     stream;
    Mode            mode;
    // Groups of the tree: the sequences that aren't empty, and the samples
    uint32_t*       sequenceIndices;
    uint32_t        listedSequences;
    char            (*labels[2])[SNDVIEW_LABEL_SIZE];
    bool*           invalidSamples; // Samples that couldn't be decoded
    uint32_t        invalidCount;
    Tree            tree;
    bool            expanded[2];
    int             selected[2];
    int             rateIndex;
    bool            loopEnabled;
    // Shared with the audio thread
    volatile Command    command;
    volatile uint32_t   commandValue;
    volatile uint32_t   commandRate;
    volatile uint32_t   commandPosition;
    volatile bool       paused;
    volatile bool       playing;
    volatile uint32_t   time;
    volatile uint32_t   samplePosition;
    volatile uint32_t   voices;
    volatile float      levels[SND_MAX_CHANNELS];
    Waveform        waveform; // Of the selected sample
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
        const SND_Sample* sample = &g_player.bank.samples[index];
        Waveform_Compute(&g_player.waveform, sample->pcm, sample->frameCount, 1);
        PlaySample(0);
    }
}

void SNDView_Close(void) {
    if (IsAudioStreamValid(g_player.stream)) {
        StopAudioStream(g_player.stream);
        UnloadAudioStream(g_player.stream);
    }
    SND_SynthDestroy(g_player.synth);
    SND_Free(&g_player.bank);
    for (int i = 0; i < 2; i++)
        free(g_player.labels[i]);
    free(g_player.sequenceIndices);
    free(g_player.invalidSamples);
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
    g_player.invalidSamples = malloc((bank->sampleCount + 1) * sizeof(bool));
    if (g_player.invalidSamples == NULL)
        return false;
    uint32_t counts[2] = { g_player.listedSequences, bank->sampleCount };
    for (int m = 0; m < 2; m++) {
        g_player.labels[m] = malloc((counts[m] + 1) * sizeof(*g_player.labels[m]));
        if (g_player.sequenceIndices == NULL || g_player.labels[m] == NULL)
            return false;
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
        g_player.invalidSamples[i] = sample->pcm == NULL;
        g_player.invalidCount += sample->pcm == NULL;
        if (sample->pcm == NULL)
            snprintf(g_player.labels[MODE_SAMPLES][i], SNDVIEW_LABEL_SIZE, "%u: invalid", i);
        else
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
    snprintf(g_player.path, sizeof(g_player.path), "%s", filepath);
    g_player.bank = bank;
    g_player.loopEnabled = true;
    g_player.expanded[MODE_SEQUENCES] = true;
    g_player.expanded[MODE_SAMPLES] = true;
    g_player.synth = SND_SynthCreate(&g_player.bank);
    if (g_player.synth == NULL || !CreateLabels()) {
        SNDView_Close();
        return false;
    }
    g_player.mode = g_player.listedSequences > 0 ? MODE_SEQUENCES : MODE_SAMPLES;
    if (ItemCount(g_player.mode) == 0) {
        printf("%s: the bank has nothing to play\n", GetFileName(g_player.path));
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

    printf("%s: DTPK sound bank, %u sequences (%u not empty), %u programs, %u parts, %u samples%s\n", GetFileName(g_player.path),
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

// The previous or next item of the group of the current mode, shown in the tree
static void SelectNext(int direction) {
    Select(g_player.selected[g_player.mode] + direction);
    Tree_Reveal(&g_player.tree, true);
}

// The same controls in both modes, as in the view of xWMA files: those of the samples are disabled with the sequences
void SNDView_Toolbar(void) {
    bool sequences = g_player.mode == MODE_SEQUENCES;
    if (Toolbar_Button(g_player.playing && !g_player.paused ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY, NULL, "Play or pause", KEY_SPACE))
        TogglePlay();
    if (Toolbar_Button(ICON_PLAYER_STOP, NULL, "Stop", 0)) {
        g_player.paused = false;
        SendCommand(COMMAND_STOP, 0);
    }
    if (Toolbar_Button(ICON_PLAYER_PREVIOUS, NULL, "Play from the start", KEY_HOME))
        Select(g_player.selected[g_player.mode]);
    if (sequences)
        GuiDisable();
    int64_t second = SampleRate(g_player.selected[MODE_SAMPLES]);
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Back 1 second", TOOLBAR_REPEAT | KEY_LEFT))
        Seek((int64_t)g_player.samplePosition - second);
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Forward 1 second", TOOLBAR_REPEAT | KEY_RIGHT))
        Seek((int64_t)g_player.samplePosition + second);
    GuiEnable();
    Toolbar_Separator();
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, sequences ? "Previous sequence" : "Previous sample", TOOLBAR_REPEAT | TOOLBAR_SHIFT | KEY_TAB))
        SelectNext(-1);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, sequences ? "Next sequence" : "Next sample", TOOLBAR_REPEAT | KEY_TAB))
        SelectNext(1);
    Toolbar_Separator();
    if (sequences)
        GuiDisable();
    bool loop = g_player.loopEnabled;
    if (Toolbar_Toggle(ICON_REPEAT, "Loop", "Play the loop of the sample", KEY_L, &loop)) {
        g_player.loopEnabled = loop;
        SendCommand(COMMAND_LOOP, 0);
    }
    int rateIndex = g_player.rateIndex;
    if (Toolbar_Choice("Auto;11025 Hz;22050 Hz;44100 Hz", "Sample rate: the one of the split that plays the sample, or a fixed one", 0, &rateIndex)) {
        g_player.rateIndex = rateIndex;
        PlaySample(g_player.samplePosition);
    }
    GuiEnable();
}

static void UpdateInput(Rectangle waveform) {
    int64_t frame;
    if (g_player.mode == MODE_SAMPLES && Waveform_UpdateSeek(&g_player.waveform, waveform, &frame))
        Seek(frame);
}

static void DrawWaveform(Rectangle area) {
    const SND_Sample* sample = SelectedSample();
    WaveformLoop loop = { sample->loopStart, sample->loopEnd, g_player.loopEnabled };
    Waveform_Draw(&g_player.waveform, area, g_player.samplePosition, sample->loop ? &loop : NULL);
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

// The sequences and the samples. A click on one plays it from the start, in its mode
static void DrawTree(Rectangle bounds) {
    static const char* names[2] = { "Sequences", "Samples" };
    int clickedMode = -1, clicked = -1;
    Tree_Begin(&g_player.tree, bounds);
    for (int m = 0; m < 2; m++) {
        Tree_BeginGroup(&g_player.tree, names[m], &g_player.expanded[m], NULL);
        for (int i = 0; i < ItemCount((Mode)m); i++) {
            int flags = (m == (int)g_player.mode && i == g_player.selected[m] ? TREE_SELECTED : 0) |
                (m == MODE_SAMPLES && g_player.invalidSamples[i] ? TREE_FAILED : 0);
            if (Tree_Item(&g_player.tree, g_player.labels[m][i], flags, NULL)) {
                clickedMode = m;
                clicked = i;
            }
        }
        Tree_EndGroup(&g_player.tree);
    }
    Tree_End(&g_player.tree);
    if (clicked >= 0) {
        g_player.mode = (Mode)clickedMode;
        Select(clicked);
    }
}

void SNDView_Draw(Rectangle area) {
    if (!g_player.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float lineHeight = fontSize * 1.2f;
    const SND_Bank* bank = &g_player.bank;
    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin;
    Rectangle list = { area.x + margin, top, SNDVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle panel = {
        list.x + list.width + margin,
        top + lineHeight + margin,
        area.width - list.width - margin * 3.0f,
        area.height - margin * 4.0f - lineHeight - controlsHeight,
    };
    if (list.height <= 0.0f || panel.width <= 0.0f || panel.height <= 0.0f)
        return;
    UpdateInput(panel);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    DrawTree(list);

    float controlsY = panel.y + panel.height + margin;
    float x = panel.x;

    if (g_player.mode == MODE_SEQUENCES) {
        const SND_Sequence* sequence = &bank->sequences[g_player.sequenceIndices[g_player.selected[MODE_SEQUENCES]]];
        const char* info = sequence->type == SND_SEQUENCE_MUSIC ?
            TextFormat("Sequence %u-%u: music, %s%s", sequence->group, sequence->index, FormatMilliseconds(sequence->duration),
                sequence->loop ? ", then loops" : "") :
            TextFormat("Sequence %u-%u: effect", sequence->group, sequence->index);
        DrawTextEx(font, info, (Vector2) { panel.x, top }, fontSize, 1.0f, LIGHTGRAY);
        DrawChannelMeters(panel, font, fontSize);
        const char* status = TextFormat("%s   %u/%u voices", FormatMilliseconds(g_player.time), g_player.voices, SND_SYNTH_VOICES);
        DrawTextEx(font, status, (Vector2) { x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
        return;
    }

    const SND_Sample* sample = SelectedSample();
    uint32_t rate = SampleRate(g_player.selected[MODE_SAMPLES]);
    const char* info = sample->pcm == NULL ?
        TextFormat("Sample %d: invalid, its data is out of the file", g_player.selected[MODE_SAMPLES]) :
        TextFormat("Sample %d: %s, %u bytes at 0x%X, %u frames, %u Hz%s", g_player.selected[MODE_SAMPLES],
            sample->encoding == SND_ENCODING_ADPCM ? "4-bit ADPCM" : "16-bit PCM", sample->size, sample->offset, sample->frameCount,
            rate, sample->loop ? TextFormat(", loop %u-%u", sample->loopStart, sample->loopEnd) : ", no loop");
    DrawTextEx(font, info, (Vector2) { panel.x, top }, fontSize, 1.0f, LIGHTGRAY);
    DrawWaveform(panel);

    // TextFormat uses a rotating buffer, so the two times can be formatted in the same call
    const char* time = TextFormat("%s / %s", FormatMilliseconds((uint32_t)((uint64_t)g_player.samplePosition * 1000 / rate)),
        FormatMilliseconds((uint32_t)((uint64_t)sample->frameCount * 1000 / rate)));
    DrawTextEx(font, time, (Vector2) { x, controlsY + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

void SNDView_Status(ViewStatus* status) {
    const SND_Bank* bank = &g_player.bank;
    uint32_t music = 0;
    for (uint32_t i = 0; i < bank->sequenceCount; i++)
        music += bank->sequences[i].type == SND_SEQUENCE_MUSIC;
    snprintf(status->file, sizeof(status->file), "%s", g_player.path);
    int length = snprintf(status->info, sizeof(status->info), "%u music, %u effects, %u samples",
        music, g_player.listedSequences - music, bank->sampleCount);
    if (g_player.invalidCount > 0)
        length += snprintf(status->info + length, sizeof(status->info) - length, " (%u invalid)", g_player.invalidCount);
    if (bank->dsp.present)
        snprintf(status->info + length, sizeof(status->info) - length, ", reverb");
    snprintf(status->hint, sizeof(status->hint), "%s", g_player.mode == MODE_SEQUENCES ? "" : "Click the waveform: seek");
}

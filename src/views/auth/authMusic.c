#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include "audio/sndSynth.h"
#include "core/log.h"
#include "formats/animation/auth.h"
#include "formats/sound/snd.h"
#include "views/auth/authMusic.h"

#define AUTHMUSIC_MAX_BANKS     128     // Banks named by the program of a map (77 at most)
#define AUTHMUSIC_NAME_SIZE     32
#define AUTHMUSIC_PATH_SIZE     1024
#define AUTHMUSIC_BUFFER        4096    // Frames of each buffer of the audio stream
#define AUTHMUSIC_RENDERED      2.0f    // Seconds of music rendered before the time where it starts in its middle
#define AUTHMUSIC_JUMP          0.25f   // Seconds: a bigger step of the time is a jump
#define AUTHMUSIC_FULL_VOLUME   127

// A bank of the sound folder of the scene, loaded when a music command needs it
typedef struct {
    char        name[AUTHMUSIC_NAME_SIZE];
    bool        valid;
    SND_Bank    bank;
} MusicBank;

// A bank played in a slot: its synthesizer and its stream
typedef struct {
    int         bank;           // Index of g_music.banks
    int         slot;           // -1 if not known
    SND_Synth*  synth;
    AudioStream stream;
    int         sequence;       // Played, -1 if none
    float       start;          // Time of the command that plays it
    bool        streaming;
} MusicTrack;

// A music command of the program with the track and the sequence it plays
typedef struct {
    int         track;          // -1 if the bank wasn't found
    int         sequence;
} MusicCommand;

static struct {
    const SCNCutscene*  program;
    uint32_t            bankCount;
    MusicBank*          banks;
    uint32_t            trackCount;
    MusicTrack*         tracks;
    MusicCommand*       commands;   // One per sound command of the program
    uint32_t            commandCount;
    float               lastTime;
    bool                restart;    // The time has changed while paused: the music starts again at the new time
    int16_t*            buffer;     // Of AUTHMUSIC_BUFFER stereo frames
    AUTHMusic_Info      info;
} g_music;

static bool EqualsNoCase(const char* a, const char* b) {
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool IsBankName(const char* name) {
    size_t length = strlen(name);
    return length > 4 && EqualsNoCase(name + length - 4, ".snd");
}

// Operation of a sound command: its low byte
static uint32_t Operation(uint32_t command) {
    return command & 0xFF;
}

// Loads a bank of the sound folder of the scene (or of the misc folder next to it) the first time. Returns its index,
// -1 if it can't be loaded
static int LoadBank(const char* sceneFolder, const char* name) {
    for (uint32_t i = 0; i < g_music.bankCount; i++) {
        if (EqualsNoCase(g_music.banks[i].name, name))
            return g_music.banks[i].valid ? (int)i : -1;
    }
    if (!IsBankName(name) || strlen(name) >= AUTHMUSIC_NAME_SIZE)
        return -1;
    MusicBank* banks = realloc(g_music.banks, (g_music.bankCount + 1) * sizeof(MusicBank));
    if (banks == NULL)
        return -1;
    g_music.banks = banks;
    MusicBank* bank = &g_music.banks[g_music.bankCount++];
    memset(bank, 0, sizeof(*bank));
    snprintf(bank->name, sizeof(bank->name), "%s", name);
    char lower[AUTHMUSIC_NAME_SIZE];
    snprintf(lower, sizeof(lower), "%s", TextToLower(name));
    const char* paths[4] = { TextFormat("%s/sound/%s", sceneFolder, name), TextFormat("%s/sound/%s", sceneFolder, lower),
        TextFormat("%s/../../misc/%s", sceneFolder, lower), NULL };
    char path[AUTHMUSIC_PATH_SIZE];
    for (int p = 0; paths[p] != NULL && !bank->valid; p++) {
        snprintf(path, sizeof(path), "%s", paths[p]);
        bank->valid = FileExists(path) && SND_Load(path, &bank->bank);
    }
    return bank->valid ? (int)(g_music.bankCount - 1) : -1;
}

// Index of the music sequence of a command in a bank, -1 if it doesn't have it
static int MusicSequence(int bank, uint32_t command) {
    if (bank < 0)
        return -1;
    const SND_Bank* snd = &g_music.banks[bank].bank;
    for (uint32_t i = 0; i < snd->sequenceCount; i++) {
        const SND_Sequence* sequence = &snd->sequences[i];
        if (sequence->groupID == (command & 0xFFFF) && sequence->index == command >> 16)
            return sequence->type == SND_SEQUENCE_MUSIC ? (int)i : -1;
    }
    return -1;
}

// The only bank of a list with the music sequence of a command, -1 if none or more than one
static int OnlyBank(const char* sceneFolder, const char* const* names, uint32_t count, uint32_t command, const char* source) {
    int found = -1;
    uint32_t matches = 0;
    for (uint32_t i = 0; i < count; i++) {
        int bank = LoadBank(sceneFolder, names[i]);
        if (MusicSequence(bank, command) < 0 || bank == found)
            continue;
        found = bank;
        matches++;
    }
    if (matches > 1)
        Log_Info("[AUTHMusic_Load]: music 0x%X in %u banks of %s\n", command, matches, source);
    return matches == 1 ? found : -1;
}

// The track of a bank in a slot, added the first time. -1 on memory error
static int TrackOf(int bank, int slot) {
    for (uint32_t i = 0; i < g_music.trackCount; i++) {
        if (g_music.tracks[i].bank == bank && g_music.tracks[i].slot == slot)
            return (int)i;
    }
    MusicTrack* tracks = realloc(g_music.tracks, (g_music.trackCount + 1) * sizeof(MusicTrack));
    if (tracks == NULL)
        return -1;
    g_music.tracks = tracks;
    g_music.tracks[g_music.trackCount] = (MusicTrack) { bank, slot, NULL, { 0 }, -1, 0.0f, false };
    return (int)g_music.trackCount++;
}

// The slot of the volume command that follows a music command at the same frame (the programs set the volume of the
// slot of the music right after they play it), -1 if none
static int FollowingSlot(uint32_t sound) {
    const SCNCutscene* program = g_music.program;
    const SCNCutscene_Sound* music = &program->sounds[sound];
    for (uint32_t i = sound + 1; i < program->soundCount && program->sounds[i].frame == music->frame; i++) {
        const SCNCutscene_Sound* next = &program->sounds[i];
        if (Operation(next->command) == SCNCUTSCENE_SOUND_SLOT && ((next->command >> 8) & 0xFF) == SCNCUTSCENE_SLOT_VOLUME &&
            (next->known & 1) && next->slot < SCNCUTSCENE_SOUND_SLOTS)
            return (int)next->slot;
        if (Operation(next->command) == SCNCUTSCENE_SOUND_MUSIC)
            break;
    }
    return -1;
}

// The bank of a music command: in a slot loaded by the program during the cutscene, or else the only one with its
// sequence among the banks of the program with the names of the cutscene, of the SNDD chunk, or of the whole program
static void FindMusic(const char* sceneFolder, const MAPINFO_File* map, const char* const* keys, uint32_t keyCount,
    uint32_t sound) {
    const SCNCutscene* program = g_music.program;
    const SCNCutscene_Sound* music = &program->sounds[sound];
    MusicCommand* command = &g_music.commands[sound];
    // The banks of the slots up to the command
    const char* slots[SCNCUTSCENE_SOUND_SLOTS] = { NULL };
    for (uint32_t i = 0; i < program->bankCount && program->banks[i].frame <= music->frame; i++)
        slots[program->banks[i].slot] = program->banks[i].name;
    int bank = -1, slot = -1;
    for (int s = 0; s < SCNCUTSCENE_SOUND_SLOTS && bank < 0; s++) {
        int candidate = slots[s] != NULL ? LoadBank(sceneFolder, slots[s]) : -1;
        if (MusicSequence(candidate, music->command) >= 0) {
            bank = candidate;
            slot = s;
        }
    }
    if (bank < 0 && map != NULL) {
        const char* names[AUTHMUSIC_MAX_BANKS];
        uint32_t count = MAPINFO_ProgramBanks(map, keys, keyCount, names, AUTHMUSIC_MAX_BANKS);
        bank = OnlyBank(sceneFolder, names, count, music->command, "the program with the cutscene");
        if (bank < 0) {
            count = 0;
            for (uint32_t i = 0; i < map->bankCount && count < AUTHMUSIC_MAX_BANKS; i++)
                names[count++] = map->banks[i].name;
            bank = OnlyBank(sceneFolder, names, count, music->command, "the SNDD chunk");
            for (uint32_t i = 0; bank >= 0 && i < map->bankCount; i++) {
                if (EqualsNoCase(map->banks[i].name, g_music.banks[bank].name))
                    slot = (int)map->banks[i].slot;
            }
        }
        if (bank < 0) {
            count = MAPINFO_ProgramBanks(map, NULL, 0, names, AUTHMUSIC_MAX_BANKS);
            bank = OnlyBank(sceneFolder, names, count, music->command, "the program");
        }
        if (slot < 0)
            slot = FollowingSlot(sound);
        for (uint32_t i = 0; bank >= 0 && slot < 0 && i < program->bankCount; i++) {
            if (EqualsNoCase(program->banks[i].name, g_music.banks[bank].name))
                slot = (int)program->banks[i].slot;
        }
    }
    command->track = bank >= 0 ? TrackOf(bank, slot) : -1;
    command->sequence = bank >= 0 ? MusicSequence(bank, music->command) : -1;
    g_music.info.found += command->track >= 0;
    if (bank < 0)
        Log_Warning("[AUTHMusic_Load]: The bank of the music 0x%X of the frame %u isn't known\n", music->command, music->frame);
}

void AUTHMusic_Free(void) {
    for (uint32_t i = 0; i < g_music.trackCount; i++) {
        MusicTrack* track = &g_music.tracks[i];
        if (IsAudioStreamValid(track->stream)) {
            StopAudioStream(track->stream);
            UnloadAudioStream(track->stream);
        }
        SND_SynthDestroy(track->synth);
    }
    for (uint32_t i = 0; i < g_music.bankCount; i++) {
        if (g_music.banks[i].valid)
            SND_Free(&g_music.banks[i].bank);
    }
    free(g_music.tracks);
    free(g_music.banks);
    free(g_music.commands);
    free(g_music.buffer);
    memset(&g_music, 0, sizeof(g_music));
}

// Frees the banks loaded to find the music that no track plays
static void FreeUnusedBanks(void) {
    for (uint32_t i = 0; i < g_music.bankCount; i++) {
        bool used = false;
        for (uint32_t t = 0; t < g_music.trackCount && !used; t++)
            used = g_music.tracks[t].bank == (int)i;
        if (!used && g_music.banks[i].valid) {
            SND_Free(&g_music.banks[i].bank);
            g_music.banks[i].valid = false;
        }
    }
}

void AUTHMusic_Load(const char* sceneFolder, const MAPINFO_File* map, const char* const* keys, uint32_t keyCount,
    const SCNCutscene* program) {
    AUTHMusic_Free();
    if (sceneFolder == NULL || program == NULL || program->soundCount == 0)
        return;
    g_music.program = program;
    g_music.commandCount = program->soundCount;
    g_music.commands = malloc(program->soundCount * sizeof(MusicCommand));
    g_music.buffer = malloc(AUTHMUSIC_BUFFER * 2 * sizeof(int16_t));
    if (g_music.commands == NULL || g_music.buffer == NULL) {
        AUTHMusic_Free();
        return;
    }
    double start = GetTime();
    for (uint32_t i = 0; i < program->soundCount; i++) {
        g_music.commands[i] = (MusicCommand) { -1, -1 };
        if (Operation(program->sounds[i].command) != SCNCUTSCENE_SOUND_MUSIC)
            continue;
        g_music.info.music++;
        FindMusic(sceneFolder, map, keys, keyCount, i);
    }
    FreeUnusedBanks();
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    SetAudioStreamBufferSizeDefault(AUTHMUSIC_BUFFER);
    for (uint32_t i = 0; i < g_music.trackCount; i++) {
        MusicTrack* track = &g_music.tracks[i];
        track->synth = SND_SynthCreate(&g_music.banks[track->bank].bank);
        track->stream = LoadAudioStream(SND_SYNTH_RATE, 16, 2);
        if (track->synth == NULL || !IsAudioStreamValid(track->stream))
            Log_Error("[AUTHMusic_Load]: Failed to create the synthesizer of %s\n", g_music.banks[track->bank].name);
        Log_Info("[AUTHMusic_Load]: music of %s in the slot %d\n", g_music.banks[track->bank].name, track->slot);
    }
    SetAudioStreamBufferSizeDefault(0);
    g_music.lastTime = -1.0f;
    if (g_music.info.music > 0)
        Log_Info("[AUTHMusic_Load]: %u/%u music commands of the program in %.2f s\n", g_music.info.found, g_music.info.music,
            GetTime() - start);
}

// The volume (0 to 127) of the bank of a slot at a time, after its volume and fade commands
static float SlotVolume(const SCNCutscene* program, int slot, float time) {
    float volume = AUTHMUSIC_FULL_VOLUME, fadeStart = -1.0f, fadeFrom = 0.0f, fadeTime = 0.0f;
    for (uint32_t i = 0; slot >= 0 && i < program->soundCount; i++) {
        const SCNCutscene_Sound* sound = &program->sounds[i];
        float at = sound->frame / AUTH_FRAME_RATE;
        if (at > time)
            break;
        if (Operation(sound->command) != SCNCUTSCENE_SOUND_SLOT || (sound->known & 3) != 3 || sound->slot != (uint32_t)slot)
            continue;
        uint32_t operation = (sound->command >> 8) & 0xFF;
        if (operation == SCNCUTSCENE_SLOT_VOLUME) {
            volume = (float)(sound->value & 0x7F);
            fadeStart = -1.0f;
        } else if (operation == SCNCUTSCENE_SLOT_FADE) {
            fadeFrom = fadeStart >= 0.0f ? fmaxf(fadeFrom * (1.0f - (at - fadeStart) / fadeTime), 0.0f) : volume;
            fadeStart = at;
            fadeTime = fmaxf(sound->value / AUTH_FRAME_RATE, 1.0f / AUTH_FRAME_RATE);
        }
    }
    if (fadeStart >= 0.0f)
        volume = fmaxf(fadeFrom * (1.0f - (time - fadeStart) / fadeTime), 0.0f);
    return volume;
}

// The gain of a volume, as the sound driver scales the level of the voices with the volume of their channel: a level
// (0 to 254) multiplied by the volume × 2 / 256, then attenuated by 0.375 dB per 2 steps below 255 (see the SND page of
// the wiki); the full volume is the reference
static float Gain(float volume) {
    if (volume <= 0.0f)
        return 0.0f;
    float level = 254.0f * volume * 2.0f / 256.0f;
    float full = 254.0f * AUTHMUSIC_FULL_VOLUME * 2.0f / 256.0f;
    return powf(10.0f, -0.375f * (full - level) / 2.0f / 20.0f);
}

// The sequence that a track plays at a time and the time of its command: the last music command of the track up to the
// time, unless a music command of another track of its slot, or the load of a bank in its slot, comes after it. -1 if
// none
static int PlayedSequence(const SCNCutscene* program, uint32_t t, float time, float* start) {
    const MusicTrack* track = &g_music.tracks[t];
    int sequence = -1;
    for (uint32_t i = 0; i < program->soundCount && program->sounds[i].frame / AUTH_FRAME_RATE <= time; i++) {
        const MusicCommand* command = &g_music.commands[i];
        if (command->track == (int)t) {
            sequence = command->sequence;
            *start = program->sounds[i].frame / AUTH_FRAME_RATE;
        } else if (command->track >= 0 && track->slot >= 0 && g_music.tracks[command->track].slot == track->slot)
            sequence = -1;
    }
    for (uint32_t i = 0; sequence >= 0 && track->slot >= 0 && i < program->bankCount; i++) {
        const SCNCutscene_Bank* bank = &program->banks[i];
        float at = bank->frame / AUTH_FRAME_RATE;
        if (at > *start && at <= time && bank->slot == (uint32_t)track->slot &&
            !EqualsNoCase(bank->name, g_music.banks[track->bank].name))
            sequence = -1;
    }
    return sequence;
}

// Starts a sequence of a track at a time after the start of the sequence: the sequence is skipped up to
// AUTHMUSIC_RENDERED seconds before it (without its notes), then rendered and dropped (the notes held from before are
// missing)
static void StartSequence(MusicTrack* track, int sequence, float offset) {
    StopAudioStream(track->stream);
    track->streaming = false;
    track->sequence = sequence;
    if (sequence < 0) {
        SND_SynthStop(track->synth);
        return;
    }
    SND_SynthPlaySequence(track->synth, (uint32_t)sequence);
    if (offset > AUTHMUSIC_RENDERED)
        SND_SynthSkip(track->synth, (uint64_t)((offset - AUTHMUSIC_RENDERED) * SND_SYNTH_RATE));
    uint64_t skip = (uint64_t)(fminf(fmaxf(offset, 0.0f), AUTHMUSIC_RENDERED) * SND_SYNTH_RATE);
    while (skip > 0 && SND_SynthIsPlaying(track->synth)) {
        uint32_t frames = skip < AUTHMUSIC_BUFFER ? (uint32_t)skip : AUTHMUSIC_BUFFER;
        SND_SynthRender(track->synth, g_music.buffer, frames);
        skip -= frames;
    }
}

void AUTHMusic_Update(float time, bool playing, bool jumped) {
    if (g_music.trackCount == 0)
        return;
    jumped |= time < g_music.lastTime || time - g_music.lastTime > AUTHMUSIC_JUMP;
    if (!playing) {
        // Paused: the music starts again at the new time when it plays
        g_music.restart |= jumped || time != g_music.lastTime;
        g_music.lastTime = time;
        for (uint32_t t = 0; t < g_music.trackCount; t++) {
            if (IsAudioStreamValid(g_music.tracks[t].stream) && IsAudioStreamPlaying(g_music.tracks[t].stream))
                PauseAudioStream(g_music.tracks[t].stream);
        }
        return;
    }
    jumped |= g_music.restart;
    g_music.restart = false;
    g_music.lastTime = time;
    for (uint32_t t = 0; t < g_music.trackCount; t++) {
        MusicTrack* track = &g_music.tracks[t];
        if (track->synth == NULL || !IsAudioStreamValid(track->stream))
            continue;
        float start = 0.0f;
        int sequence = PlayedSequence(g_music.program, t, time, &start);
        if (jumped || sequence != track->sequence || (sequence >= 0 && start != track->start)) {
            track->start = start;
            StartSequence(track, sequence, time - start);
        }
        if (track->sequence < 0)
            continue;
        SetAudioStreamVolume(track->stream, Gain(SlotVolume(g_music.program, track->slot, time)));
        for (int b = 0; b < 2 && IsAudioStreamProcessed(track->stream); b++) {
            SND_SynthRender(track->synth, g_music.buffer, AUTHMUSIC_BUFFER);
            UpdateAudioStream(track->stream, g_music.buffer, AUTHMUSIC_BUFFER);
        }
        if (!track->streaming) {
            PlayAudioStream(track->stream);
            track->streaming = true;
        } else if (!IsAudioStreamPlaying(track->stream))
            ResumeAudioStream(track->stream);
    }
}

void AUTHMusic_GetInfo(AUTHMusic_Info* info) {
    *info = g_music.info;
}

const char* AUTHMusic_BankOf(uint32_t sound) {
    if (sound >= g_music.commandCount || g_music.commands[sound].track < 0)
        return NULL;
    return g_music.banks[g_music.tracks[g_music.commands[sound].track].bank].name;
}

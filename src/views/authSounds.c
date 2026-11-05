#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include "formats/archive/afs.h"
#include "formats/map/mapinfo.h"
#include "formats/sound/snd.h"
#include "formats/sound/xwm.h"
#include "views/authSounds.h"

#define AUTHSOUNDS_PATH_SIZE    1024
#define AUTHSOUNDS_NAME_SIZE    64
#define AUTHSOUNDS_PROGRAM_BANKS 16     // Banks and names of the cutscene compared in the program of the map
#define AUTHSOUNDS_SEQUENCE_EVENTS 16   // Events read of an effect sequence
#define AUTHSOUNDS_JUMP         0.25f   // Seconds: a bigger step of the time is a jump

// A voice clip or an effect, played by one or more commands
typedef struct {
    char    key[AUTHSOUNDS_NAME_SIZE];  // Clip name, or "#<effect>"
    Sound   sound;
    bool    paused;
} AUTHSound;

// A sound started at a time with a volume, or stopped (volume 0): the voice of a command, or an effect of the sequence of
// a sound command
typedef struct {
    float   time;
    int     sound;
    float   volume;
} AUTHCue;

static struct {
    uint32_t        soundCount;
    AUTHSound*      sounds;
    uint32_t        commandCount;
    int*            commandSounds;  // First sound of each command, -1 if none
    uint32_t        cueCount;
    AUTHCue*        cues;           // In order of time
    float           lastTime;       // Time of the last update: the cues after it are played
    bool            wasPlaying;
    AUTHSounds_Info info;
} g_sounds;

static uint32_t Arg(const AUTH_Command* command, uint32_t arg) {
    return arg < command->argCount ? command->args[arg] : 0;
}

// Removes the last component of a path with / separators. Returns false if there is none
static bool ToParent(char* path) {
    char* slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return false;
    *slash = '\0';
    return true;
}

static const char* LastComponent(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

// Sound with a key, -1 if it isn't loaded yet
static int FindSound(const char* key) {
    for (uint32_t i = 0; i < g_sounds.soundCount; i++) {
        if (strcmp(g_sounds.sounds[i].key, key) == 0)
            return (int)i;
    }
    return -1;
}

// Uploads a mono 16-bit sound. Returns its index, -1 if it can't be loaded
static int AddSound(const char* key, const int16_t* pcm, uint32_t frameCount, uint32_t sampleRate) {
    AUTHSound* sound = &g_sounds.sounds[g_sounds.soundCount];
    snprintf(sound->key, sizeof(sound->key), "%s", key);
    Wave wave = { .frameCount = frameCount, .sampleRate = sampleRate, .sampleSize = 16, .channels = 1, .data = (void*)pcm };
    sound->sound = LoadSoundFromWave(wave);
    if (!IsSoundValid(sound->sound))
        return -1;
    return (int)g_sounds.soundCount++;
}

// Clip name and voice AFS name of a voice path: the file name, and the file name without its last 4 characters (the
// speaker and the number), e.g. "E1020B001" and "e1020" for .../01YAMA/SOUND/E1020B001.aiff
static bool VoiceNames(const char* path, char* afs, char* clip) {
    const char* name = LastComponent(path);
    const char* dot = strrchr(name, '.');
    int length = (int)(dot != NULL ? dot - name : strlen(name));
    if (length <= 4 || length >= AUTHSOUNDS_NAME_SIZE)
        return false;
    snprintf(clip, AUTHSOUNDS_NAME_SIZE, "%.*s", length, name);
    snprintf(afs, AUTHSOUNDS_NAME_SIZE, "%.*s", length - 4, name);
    for (char* c = afs; *c != '\0'; c++)
        *c = (char)tolower((unsigned char)*c);
    return true;
}

// Decodes a voice clip of the scene: the extracted clip <root>/scene/<NN>/stream/pc/<language>/<afs>/<clip>.xwm, or the
// clip in <afs>.afs, in the folders next to the folder that contains the scene folders (the audio archives are extracted
// next to the disk archive) or in that folder, in English or else in Japanese
static int16_t* DecodeVoice(const FilePathList* roots, const char* scene, const char* afs, const char* clip, XWM_Info* info) {
    const char* languages[2] = { "eng", "jap" };
    for (int l = 0; l < 2; l++) {
        for (unsigned int r = 0; r < roots->count; r++) {
            char path[AUTHSOUNDS_PATH_SIZE];
            snprintf(path, sizeof(path), "%s/scene/%s/stream/pc/%s/%s/%s.xwm", roots->paths[r], scene, languages[l], afs, clip);
            if (FileExists(path))
                return XWM_Decode(path, info);
            snprintf(path, sizeof(path), "%s/scene/%s/stream/pc/%s/%s.afs", roots->paths[r], scene, languages[l], afs);
            if (!FileExists(path))
                continue;
            size_t size = 0;
            uint8_t* data = AFS_LoadNamedEntry(path, clip, &size);
            if (data == NULL)
                continue;
            int16_t* pcm = XWM_DecodeMemory(data, size, clip, info);
            free(data);
            return pcm;
        }
    }
    return NULL;
}

// The folders where the audio archives can be: the folders next to the folder that contains the scene folders, and
// that folder
static FilePathList AudioRoots(const char* sceneFolder) {
    char root[AUTHSOUNDS_PATH_SIZE], outer[AUTHSOUNDS_PATH_SIZE];
    snprintf(root, sizeof(root), "%s", sceneFolder);
    ToParent(root);  // scene
    ToParent(root);  // The folder that contains the scene folders
    snprintf(outer, sizeof(outer), "%s", root);
    FilePathList roots = { 0 };
    if (ToParent(outer))
        roots = LoadDirectoryFilesEx(outer, "DIRS*", false);
    // The folder of the scene folders first
    FilePathList all = { 0 };
    all.paths = calloc(roots.count + 1, sizeof(char*));
    if (all.paths == NULL) {
        UnloadDirectoryFiles(roots);
        return all;
    }
    all.paths[all.count] = malloc(strlen(root) + 1);
    if (all.paths[all.count] != NULL)
        strcpy(all.paths[all.count++], root);
    for (unsigned int i = 0; i < roots.count; i++) {
        all.paths[all.count] = malloc(strlen(roots.paths[i]) + 1);
        if (all.paths[all.count] == NULL)
            break;
        strcpy(all.paths[all.count], roots.paths[i]);
        for (char* c = all.paths[all.count]; *c != '\0'; c++) {
            if (*c == '\\')
                *c = '/';
        }
        if (strcmp(all.paths[all.count], root) != 0)
            all.count++;
        else
            free(all.paths[all.count]);
    }
    UnloadDirectoryFiles(roots);
    return all;
}

static void FreeRoots(FilePathList* roots) {
    for (unsigned int i = 0; i < roots->count; i++)
        free(roots->paths[i]);
    free(roots->paths);
}

static void LoadVoices(const AUTH_Sequence* sequence, const char* sceneFolder) {
    FilePathList roots = AudioRoots(sceneFolder);
    const char* scene = LastComponent(sceneFolder);
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        if (command->opcode != AUTH_COMMAND_VOICE)
            continue;
        g_sounds.info.voiceCommands++;
        uint32_t name = Arg(command, 1);
        char afs[AUTHSOUNDS_NAME_SIZE], clip[AUTHSOUNDS_NAME_SIZE];
        if (name >= sequence->soundCount || !VoiceNames(sequence->sounds[name], afs, clip))
            continue;
        int sound = FindSound(clip);
        if (sound < 0) {
            XWM_Info info;
            int16_t* pcm = DecodeVoice(&roots, scene, afs, clip, &info);
            if (pcm == NULL) {
                printf("[AUTHSounds_Load]: Voice clip %s not found in %s.afs\n", clip, afs);
                continue;
            }
            sound = AddSound(clip, pcm, info.frameCount, info.sampleRate);
            free(pcm);
        }
        g_sounds.commandSounds[i] = sound;
        g_sounds.info.voices += sound >= 0;
    }
    FreeRoots(&roots);
}

// Group and sequence of the sound of a sound command: the low and the high 16 bits of its sound ID. Returns false if the
// command plays no sound (another command, or the sound ID 0xFFFFFFFF)
static bool CommandSequence(const AUTH_Command* command, uint16_t* group, uint32_t* index) {
    uint32_t id = Arg(command, 1);
    if (command->opcode != AUTH_COMMAND_SOUND || command->argCount < 2 || id == 0xFFFFFFFF)
        return false;
    *group = (uint16_t)(id & 0xFFFF);
    *index = id >> 16;
    return true;
}

// Effect sequence of a bank with a group ID and an index in its group, NULL if none
static const SND_Sequence* FindSequence(const SND_Bank* bank, uint16_t group, uint32_t index) {
    for (uint32_t i = 0; i < bank->sequenceCount; i++) {
        const SND_Sequence* sequence = &bank->sequences[i];
        if (sequence->groupID == group && sequence->index == index)
            return sequence->type == SND_SEQUENCE_EFFECT ? sequence : NULL;
    }
    return NULL;
}

// Effect sequence of a sound command in a bank, NULL if none
static const SND_Sequence* CommandSoundSequence(const SND_Bank* bank, const AUTH_Command* command) {
    uint16_t group;
    uint32_t index;
    return CommandSequence(command, &group, &index) ? FindSequence(bank, group, index) : NULL;
}

// Split of the effect program that plays an effect, NULL if none
static const SND_Split* EffectSplit(const SND_Bank* bank, int effect) {
    if (!bank->hasEffectProgram || bank->effectProgram.layerCount == 0 || effect < 0 || effect > 127)
        return NULL;
    const SND_Layer* layer = &bank->effectProgram.layers[0];
    int split = layer->keyMap[effect];
    if (split < 0 || (uint32_t)split >= layer->splitCount || layer->splits[split].sample >= bank->sampleCount)
        return NULL;
    return &layer->splits[split];
}

// Path of a bank of the sound folder of the scene, as named by the program (e.g. "a1_yanji.snd"). Returns false if the
// file doesn't exist
static bool BankPath(const char* sceneFolder, const char* name, char* path, size_t size) {
    snprintf(path, size, "%s/sound/%s", sceneFolder, name);
    if (FileExists(path))
        return true;
    snprintf(path, size, "%s/sound/%s", sceneFolder, TextToLower(name));
    return FileExists(path);
}

// Names the program of the map loads with the cutscene: the name of its PKS (its folder, e.g. DJHN_07) and the names of
// the AFS of its voices (e.g. 01JUCEA). Returns their number
static uint32_t CutsceneKeys(const AUTH_Sequence* sequence, const char* filepath, char keys[][AUTHSOUNDS_NAME_SIZE], uint32_t maxKeys) {
    uint32_t count = 0;
    snprintf(keys[count++], AUTHSOUNDS_NAME_SIZE, "%s", GetFileName(GetDirectoryPath(filepath)));
    for (uint32_t i = 0; i < sequence->commandCount && count < maxKeys; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        char afs[AUTHSOUNDS_NAME_SIZE], clip[AUTHSOUNDS_NAME_SIZE];
        if (command->opcode != AUTH_COMMAND_VOICE || Arg(command, 1) >= sequence->soundCount ||
            !VoiceNames(sequence->sounds[Arg(command, 1)], afs, clip))
            continue;
        bool known = false;
        for (uint32_t k = 0; k < count && !known; k++)
            known = strcmp(keys[k], afs) == 0;
        if (!known)
            snprintf(keys[count++], AUTHSOUNDS_NAME_SIZE, "%s", afs);
    }
    return count;
}

// Loads the bank of the sounds of the cutscene: among the banks loaded by the functions of the program of its map (the
// mapinfo.bin of the parent folder of its folder) that also load the name of its PKS or of the AFS of its voices, the
// one with a sequence for the most sound commands (the group with the ID of the low 16 bits of their sound ID); it has
// to have more than any other one. Returns false if there is none
static bool LoadProgramBank(const AUTH_Sequence* sequence, const char* sceneFolder, const char* filepath, SND_Bank* bank, char* name, size_t size) {
    char keys[AUTHSOUNDS_PROGRAM_BANKS][AUTHSOUNDS_NAME_SIZE];
    const char* keyPointers[AUTHSOUNDS_PROGRAM_BANKS];
    uint32_t keyCount = CutsceneKeys(sequence, filepath, keys, AUTHSOUNDS_PROGRAM_BANKS);
    for (uint32_t k = 0; k < keyCount; k++)
        keyPointers[k] = keys[k];
    char mapinfo[AUTHSOUNDS_PATH_SIZE];
    snprintf(mapinfo, sizeof(mapinfo), "%s/mapinfo.bin", GetPrevDirectoryPath(GetDirectoryPath(filepath)));
    MAPINFO_File map;
    if (!FileExists(mapinfo) || !MAPINFO_Load(mapinfo, &map)) {
        printf("[AUTHSounds_Load]: The bank of the sounds isn't known: no mapinfo.bin in the parent folder\n");
        return false;
    }
    const char* names[AUTHSOUNDS_PROGRAM_BANKS];
    uint32_t nameCount = MAPINFO_ProgramBanks(&map, keyPointers, keyCount, names, AUTHSOUNDS_PROGRAM_BANKS);
    int bestScore = 0, secondScore = 0;
    bool found = false;
    for (uint32_t n = 0; n < nameCount; n++) {
        char path[AUTHSOUNDS_PATH_SIZE];
        SND_Bank candidate;
        if (!BankPath(sceneFolder, names[n], path, sizeof(path)) || !SND_Load(path, &candidate))
            continue;
        int score = 0;
        for (uint32_t i = 0; i < sequence->commandCount; i++)
            score += CommandSoundSequence(&candidate, &sequence->commands[i]) != NULL;
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            if (found)
                SND_Free(bank);
            *bank = candidate;
            found = true;
            snprintf(name, size, "%s", names[n]);
        }
        else {
            secondScore = score > secondScore ? score : secondScore;
            SND_Free(&candidate);
        }
    }
    printf("[AUTHSounds_Load]: %u banks loaded with %s by the program of the map: %s, %d commands with a sequence (%d in the next bank)\n",
        nameCount, keys[0], found ? name : "none", bestScore, secondScore);
    MAPINFO_Free(&map);
    if (found && bestScore <= secondScore) {
        SND_Free(bank);
        found = false;
    }
    return found;
}

// Adds a cue. Returns false if there is no memory
static bool AddCue(float time, int sound, float volume) {
    AUTHCue* cues = realloc(g_sounds.cues, (g_sounds.cueCount + 1) * sizeof(AUTHCue));
    if (cues == NULL)
        return false;
    g_sounds.cues = cues;
    g_sounds.cues[g_sounds.cueCount++] = (AUTHCue) { time, sound, volume };
    return true;
}

// Sound of an effect of the effect program, uploaded the first time. -1 if the bank doesn't have it
static int EffectSound(const SND_Bank* bank, int effect, float* level) {
    const SND_Split* split = EffectSplit(bank, effect);
    if (split == NULL)
        return -1;
    // The level of the split, in steps of 3 dB from 15
    *level = split->level != SND_UNSET ? powf(10.0f, -3.0f * (15 - (split->level & 15)) / 20.0f) : 1.0f;
    char key[AUTHSOUNDS_NAME_SIZE];
    snprintf(key, sizeof(key), "#%d", effect);
    int sound = FindSound(key);
    if (sound < 0) {
        const SND_Sample* sample = &bank->samples[split->sample];
        sound = AddSound(key, sample->pcm, sample->frameCount, sample->sampleRate);
    }
    return sound;
}

// Adds the cues of the events of an effect sequence played at a time: each event Dx starts an effect of the effect
// program with a volume (0 stops it), after the delays of the events before it. Returns the first sound, -1 if none
static int AddSequenceCues(const SND_Bank* bank, const SND_Sequence* sequence, float time) {
    uint8_t runningStatus = 0;
    uint32_t pos = sequence->offset + 1; // After the header byte
    int first = -1;
    for (int e = 0; e < AUTHSOUNDS_SEQUENCE_EVENTS && pos != 0; e++) {
        SND_Event event;
        pos = SND_ReadEvent(bank, pos, true, &runningStatus, &event);
        if (pos == 0 || event.status == SND_EVENT_END)
            break;
        if ((event.status & 0xF0) == 0xD0) {
            float level;
            int sound = EffectSound(bank, event.data[0], &level);
            if (sound >= 0 && AddCue(time, sound, level * event.data[1] / 127.0f) && first < 0 && event.data[1] > 0)
                first = sound;
        }
        time += event.delta / 1000.0f;
    }
    return first;
}

static void LoadEffects(const AUTH_Sequence* sequence, const char* sceneFolder, const char* filepath) {
    bool any = false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        uint16_t group;
        uint32_t index;
        g_sounds.info.effectCommands += sequence->commands[i].opcode == AUTH_COMMAND_SOUND;
        any |= CommandSequence(&sequence->commands[i], &group, &index);
    }
    SND_Bank bank;
    if (!any || filepath == NULL || !LoadProgramBank(sequence, sceneFolder, filepath, &bank, g_sounds.info.bank, sizeof(g_sounds.info.bank)))
        return;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const SND_Sequence* effects = CommandSoundSequence(&bank, &sequence->commands[i]);
        if (effects == NULL)
            continue;
        int sound = AddSequenceCues(&bank, effects, sequence->commands[i].frame / AUTH_FRAME_RATE);
        g_sounds.commandSounds[i] = sound;
        g_sounds.info.effects += sound >= 0;
    }
    SND_Free(&bank);
}

static int CompareCues(const void* a, const void* b) {
    float ta = ((const AUTHCue*)a)->time, tb = ((const AUTHCue*)b)->time;
    return ta < tb ? -1 : ta > tb;
}

void AUTHSounds_Free(void) {
    for (uint32_t i = 0; i < g_sounds.soundCount; i++)
        UnloadSound(g_sounds.sounds[i].sound);
    free(g_sounds.sounds);
    free(g_sounds.commandSounds);
    free(g_sounds.cues);
    memset(&g_sounds, 0, sizeof(g_sounds));
}

void AUTHSounds_Load(const char* sceneFolder, const char* filepath, const AUTH_Sequence* sequence) {
    AUTHSounds_Free();
    g_sounds.lastTime = -1.0f / AUTH_FRAME_RATE;
    g_sounds.commandCount = sequence->commandCount;
    g_sounds.commandSounds = malloc((sequence->commandCount + 1) * sizeof(int));
    g_sounds.sounds = calloc(sequence->commandCount * AUTHSOUNDS_SEQUENCE_EVENTS + 1, sizeof(AUTHSound));
    if (g_sounds.commandSounds == NULL || g_sounds.sounds == NULL) {
        AUTHSounds_Free();
        return;
    }
    for (uint32_t i = 0; i < sequence->commandCount; i++)
        g_sounds.commandSounds[i] = -1;

    if (sceneFolder == NULL)
        return;
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    double start = GetTime();
    LoadVoices(sequence, sceneFolder);
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        if (g_sounds.commandSounds[i] >= 0)
            AddCue(sequence->commands[i].frame / AUTH_FRAME_RATE, g_sounds.commandSounds[i], 1.0f);
    }
    LoadEffects(sequence, sceneFolder, filepath);
    // The events of a sequence can come after the next commands
    if (g_sounds.cues != NULL)
        qsort(g_sounds.cues, g_sounds.cueCount, sizeof(AUTHCue), CompareCues);
    printf("[AUTHSounds_Load]: %u/%u voices, %u/%u sound effects in %.2f s\n", g_sounds.info.voices, g_sounds.info.voiceCommands,
        g_sounds.info.effects, g_sounds.info.effectCommands, GetTime() - start);
}

static void StopAll(void) {
    for (uint32_t i = 0; i < g_sounds.soundCount; i++) {
        StopSound(g_sounds.sounds[i].sound);
        g_sounds.sounds[i].paused = false;
    }
}

void AUTHSounds_Update(float time, bool playing, bool jumped) {
    if (g_sounds.soundCount == 0)
        return;
    jumped |= time < g_sounds.lastTime || time - g_sounds.lastTime > AUTHSOUNDS_JUMP;
    if (jumped) {
        // The cues at the new time are played when it plays
        StopAll();
        g_sounds.lastTime = time - 0.5f / AUTH_FRAME_RATE;
        g_sounds.wasPlaying = playing;
        return;
    }
    if (!playing) {
        for (uint32_t i = 0; i < g_sounds.soundCount && g_sounds.wasPlaying; i++) {
            AUTHSound* sound = &g_sounds.sounds[i];
            if (IsSoundPlaying(sound->sound)) {
                PauseSound(sound->sound);
                sound->paused = true;
            }
        }
        g_sounds.wasPlaying = false;
        return;
    }
    if (!g_sounds.wasPlaying) {
        for (uint32_t i = 0; i < g_sounds.soundCount; i++) {
            if (g_sounds.sounds[i].paused)
                ResumeSound(g_sounds.sounds[i].sound);
            g_sounds.sounds[i].paused = false;
        }
    }
    g_sounds.wasPlaying = true;
    for (uint32_t i = 0; i < g_sounds.cueCount && g_sounds.cues[i].time <= time; i++) {
        const AUTHCue* cue = &g_sounds.cues[i];
        if (cue->time <= g_sounds.lastTime)
            continue;
        Sound sound = g_sounds.sounds[cue->sound].sound;
        if (cue->volume <= 0.0f)
            StopSound(sound);
        else {
            SetSoundVolume(sound, cue->volume);
            PlaySound(sound);
        }
    }
    g_sounds.lastTime = time;
}

void AUTHSounds_GetInfo(AUTHSounds_Info* info) {
    *info = g_sounds.info;
}

bool AUTHSounds_HasSound(uint32_t command) {
    return command < g_sounds.commandCount && g_sounds.commandSounds[command] >= 0;
}

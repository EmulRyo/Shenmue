#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include "formats/archive/afs.h"
#include "formats/sound/snd.h"
#include "formats/sound/xwm.h"
#include "views/authSounds.h"

#define AUTHSOUNDS_PATH_SIZE    1024
#define AUTHSOUNDS_NAME_SIZE    64
#define AUTHSOUNDS_EFFECTS      128     // Keys of the effect program
#define AUTHSOUNDS_EFFECT_TYPE  0x04A9  // Low 16 bits of the sound ID of the effects of the cutscene banks
#define AUTHSOUNDS_EXTRA        0.498f  // Seconds the length of a sound command adds to the length of its effect
#define AUTHSOUNDS_TOLERANCE    0.02f   // Seconds of difference to that
#define AUTHSOUNDS_JUMP         0.25f   // Seconds: a bigger step of the time is a jump

// A voice clip or an effect, played by one or more commands
typedef struct {
    char    key[AUTHSOUNDS_NAME_SIZE];  // Clip name, or "#<effect>"
    Sound   sound;
    bool    paused;
} AUTHSound;

// Lengths of the effects of a SND bank, kept between the cutscenes so the banks are read once
typedef struct {
    char    path[AUTHSOUNDS_PATH_SIZE];
    float   lengths[AUTHSOUNDS_EFFECTS];    // Seconds, 0 if the bank has no such effect
} BankLengths;

static struct {
    uint32_t        soundCount;
    AUTHSound*      sounds;
    uint32_t        commandCount;
    const AUTH_Command* commands;
    int*            commandSounds;  // Sound of each command, -1 if none
    float           lastTime;       // Time of the last update: the commands after it are played
    bool            wasPlaying;
    AUTHSounds_Info info;
} g_sounds;

static uint32_t g_bankCount;
static BankLengths* g_banks;

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
            snprintf(path, sizeof(path), "%s/scene/%s/stream/pc/%s/%s/%s.xwm", roots->paths[r], scene, languages[l], afs, clip);            if (FileExists(path))
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

// Effect of a sound command of the cutscene bank, -1 if it plays another kind of sound
static int CommandEffect(const AUTH_Command* command) {
    uint32_t id = Arg(command, 1);
    if (command->opcode != AUTH_COMMAND_SOUND || (id & 0xFFFF) != AUTHSOUNDS_EFFECT_TYPE || (id >> 16) >= AUTHSOUNDS_EFFECTS)
        return -1;
    return (int)(id >> 16);
}

// Split of the effect program that plays an effect, NULL if none
static const SND_Split* EffectSplit(const SND_Bank* bank, int effect) {
    if (!bank->hasEffectProgram || bank->effectProgram.layerCount == 0)
        return NULL;
    const SND_Layer* layer = &bank->effectProgram.layers[0];
    int split = layer->keyMap[effect];
    if (split < 0 || (uint32_t)split >= layer->splitCount || layer->splits[split].sample >= bank->sampleCount)
        return NULL;
    return &layer->splits[split];
}

// Lengths of the effects of a bank, read the first time
static const BankLengths* GetBankLengths(const char* path) {
    for (uint32_t i = 0; i < g_bankCount; i++) {
        if (strcmp(g_banks[i].path, path) == 0)
            return &g_banks[i];
    }
    BankLengths* banks = realloc(g_banks, (g_bankCount + 1) * sizeof(BankLengths));
    if (banks == NULL)
        return NULL;
    g_banks = banks;
    BankLengths* lengths = &g_banks[g_bankCount++];
    memset(lengths, 0, sizeof(*lengths));
    snprintf(lengths->path, sizeof(lengths->path), "%s", path);
    SND_Bank bank;
    if (!SND_Load(path, &bank))
        return lengths;
    for (int e = 0; e < AUTHSOUNDS_EFFECTS; e++) {
        const SND_Split* split = EffectSplit(&bank, e);
        const SND_Sample* sample = split != NULL ? &bank.samples[split->sample] : NULL;
        if (sample != NULL && sample->sampleRate != 0 && sample->frameCount != 0)
            lengths->lengths[e] = (float)sample->frameCount / sample->sampleRate;
    }
    SND_Free(&bank);
    return lengths;
}

// The bank of the sound folder of the scene whose effects have the lengths of the most sound commands: the length of a
// command with 9 arguments is the length of the sample of its effect plus AUTHSOUNDS_EXTRA. The bank has to match more
// commands than any other bank. The commands with 3 arguments have no length, so the bank of a cutscene without
// commands of 9 arguments isn't known. Returns false if there is no such bank
static bool FindBank(const AUTH_Sequence* sequence, const char* sceneFolder, char* path, size_t size) {
    int lengths = 0;
    for (uint32_t i = 0; i < sequence->commandCount; i++)
        lengths += CommandEffect(&sequence->commands[i]) >= 0 && sequence->commands[i].argCount >= 9;
    char folder[AUTHSOUNDS_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s/sound", sceneFolder);
    if (lengths == 0 || !DirectoryExists(folder)) {
        printf("[AUTHSounds_Load]: The bank of the sound effects isn't known (no sound command with a length)\n");
        return false;
    }
    FilePathList files = LoadDirectoryFilesEx(folder, ".snd", false);
    int best = -1, bestScore = 0, secondScore = 0;
    for (unsigned int f = 0; f < files.count; f++) {
        const BankLengths* bank = GetBankLengths(files.paths[f]);
        if (bank == NULL)
            continue;
        int score = 0;
        for (uint32_t i = 0; i < sequence->commandCount; i++) {
            const AUTH_Command* command = &sequence->commands[i];
            int effect = CommandEffect(command);
            if (effect >= 0 && command->argCount >= 9 && bank->lengths[effect] > 0.0f)
                score += fabsf(bank->lengths[effect] + AUTHSOUNDS_EXTRA - AUTH_ArgFloat(command, 8)) <= AUTHSOUNDS_TOLERANCE;
        }
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            best = (int)f;
        }
        else if (score > secondScore)
            secondScore = score;
    }
    bool found = best >= 0 && bestScore > secondScore;
    if (best >= 0)
        printf("[AUTHSounds_Load]: %s %s for the sound effects: %d of %d lengths match (%d in the next bank)\n", GetFileName(files.paths[best]),
            found ? "chosen" : "rejected", bestScore, lengths, secondScore);
    if (found)
        snprintf(path, size, "%s", files.paths[best]);
    UnloadDirectoryFiles(files);
    return found;
}

static void LoadEffects(const AUTH_Sequence* sequence, const char* sceneFolder) {
    bool any = false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        g_sounds.info.effectCommands += sequence->commands[i].opcode == AUTH_COMMAND_SOUND;
        any |= CommandEffect(&sequence->commands[i]) >= 0;
    }
    char path[AUTHSOUNDS_PATH_SIZE];
    SND_Bank bank;
    if (!any || !FindBank(sequence, sceneFolder, path, sizeof(path)) || !SND_Load(path, &bank))
        return;
    snprintf(g_sounds.info.bank, sizeof(g_sounds.info.bank), "%s", GetFileName(path));
    // Only the effects with the length of a command are played: the others can be in another bank
    bool verified[AUTHSOUNDS_EFFECTS] = { false };
    const BankLengths* lengths = GetBankLengths(path);
    for (uint32_t i = 0; i < sequence->commandCount && lengths != NULL; i++) {
        int effect = CommandEffect(&sequence->commands[i]);
        if (effect >= 0 && sequence->commands[i].argCount >= 9 && lengths->lengths[effect] > 0.0f &&
            fabsf(lengths->lengths[effect] + AUTHSOUNDS_EXTRA - AUTH_ArgFloat(&sequence->commands[i], 8)) <= AUTHSOUNDS_TOLERANCE)
            verified[effect] = true;
    }
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        int effect = CommandEffect(&sequence->commands[i]);
        if (effect < 0 || !verified[effect])
            continue;
        char key[AUTHSOUNDS_NAME_SIZE];
        snprintf(key, sizeof(key), "#%d", effect);
        int sound = FindSound(key);
        const SND_Split* split = EffectSplit(&bank, effect);
        if (sound < 0 && split != NULL) {
            const SND_Sample* sample = &bank.samples[split->sample];
            sound = AddSound(key, sample->pcm, sample->frameCount, sample->sampleRate);
            // The level of the split, in steps of 3 dB from 15
            if (sound >= 0 && split->level != SND_UNSET)
                SetSoundVolume(g_sounds.sounds[sound].sound, powf(10.0f, -3.0f * (15 - (split->level & 15)) / 20.0f));
        }
        g_sounds.commandSounds[i] = sound;
        g_sounds.info.effects += sound >= 0;
    }
    SND_Free(&bank);
}

void AUTHSounds_Free(void) {
    for (uint32_t i = 0; i < g_sounds.soundCount; i++)
        UnloadSound(g_sounds.sounds[i].sound);
    free(g_sounds.sounds);
    free(g_sounds.commandSounds);
    memset(&g_sounds, 0, sizeof(g_sounds));
}

void AUTHSounds_Load(const char* sceneFolder, const AUTH_Sequence* sequence) {
    AUTHSounds_Free();
    g_sounds.lastTime = -1.0f / AUTH_FRAME_RATE;
    g_sounds.commandCount = sequence->commandCount;
    g_sounds.commands = sequence->commands;
    g_sounds.commandSounds = malloc((sequence->commandCount + 1) * sizeof(int));
    g_sounds.sounds = calloc(sequence->commandCount + 1, sizeof(AUTHSound));
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
    LoadEffects(sequence, sceneFolder);
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
        // The commands at the new time are played when it plays
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
    for (uint32_t i = 0; i < g_sounds.commandCount; i++) {
        float start = g_sounds.commands[i].frame / AUTH_FRAME_RATE;
        if (start > time)
            break;
        if (start > g_sounds.lastTime && g_sounds.commandSounds[i] >= 0)
            PlaySound(g_sounds.sounds[g_sounds.commandSounds[i]].sound);
    }
    g_sounds.lastTime = time;
}

void AUTHSounds_GetInfo(AUTHSounds_Info* info) {
    *info = g_sounds.info;
}

bool AUTHSounds_HasSound(uint32_t command) {
    return command < g_sounds.commandCount && g_sounds.commandSounds[command] >= 0;
}

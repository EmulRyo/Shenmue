#ifndef __AUTHSOUNDS_H__
#define __AUTHSOUNDS_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/animation/auth.h"

// Sounds of an AUTH cutscene, played with the timeline. The voice clips are the xWMA clips of the voice AFS files of the
// scene (extracted or not), in English or else in Japanese. The sound effects are the effects of the SND bank of the
// scene folder whose effects have the lengths given by the sound commands

typedef struct {
    uint32_t    voices;         // Voice commands with a clip
    uint32_t    voiceCommands;
    uint32_t    effects;        // Sound commands with an effect
    uint32_t    effectCommands;
    char        bank[64];       // File name of the SND bank of the effects, empty if none was found
} AUTHSounds_Info;

// Finds and decodes the sounds of the commands of a cutscene of a scene folder (e.g. .../scene/01, with / separators),
// replacing the previous ones. Without a scene folder (NULL) the cutscene has no sound
void AUTHSounds_Load(const char* sceneFolder, const AUTH_Sequence* sequence);
void AUTHSounds_Free(void);
// Plays the sounds of the commands between the previous time and time (seconds) while playing; pauses them when not
// playing, and stops them when the time jumps (seek)
void AUTHSounds_Update(float time, bool playing, bool jumped);
void AUTHSounds_GetInfo(AUTHSounds_Info* info);
// Whether the sound of a command was found
bool AUTHSounds_HasSound(uint32_t command);

#endif

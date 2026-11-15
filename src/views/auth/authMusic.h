#ifndef __AUTHMUSIC_H__
#define __AUTHMUSIC_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/map/mapinfo.h"
#include "scenario/scnCutscene.h"

// The music and the ambient sounds that the program of the map plays with a cutscene (SoundCommand, 0x06C, see
// scnCutscene.h), played with the timeline. A command 0xA8 plays the music sequence of its group in the bank that has
// it: one of the banks that the program loads during the cutscene (SoundBank 0x16D and SoundBanks 0x0F3), or else the
// only one with that group among the banks loaded by the functions of the program with the names of the cutscene, the
// banks of the SNDD chunk of the map, or all the banks of the program (e.g. 0x3FA8, the group of almost all the bgm*.snd
// banks, plays bgm127.snd in the cutscene of scene/01/betd). Each bank plays in its slot: the commands 0xA0 of the slot
// set its volume (0x04) and fade it out (0x0A, in frames), a new music of the slot or a new bank in it stops it

typedef struct {
    uint32_t    music;          // Commands that play a music sequence
    uint32_t    found;          // With the bank of their sequence
} AUTHMusic_Info;

// Finds the banks of the music commands of the program of the map with a cutscene, replacing the previous ones:
// sceneFolder, the scene folder (e.g. .../scene/01, with / separators); map, the mapinfo.bin of the map (NULL if none);
// keys, the names the program loads with the cutscene (the name of its PKS and of the AFS of its voices)
void AUTHMusic_Load(const char* sceneFolder, const MAPINFO_File* map, const char* const* keys, uint32_t keyCount,
    const SCNCutscene* program);
void AUTHMusic_Free(void);
// Plays the music at a time (seconds) while playing; pauses it when not playing, and starts it again at the new time
// when the time jumps (seek)
void AUTHMusic_Update(float time, bool playing, bool jumped);
void AUTHMusic_GetInfo(AUTHMusic_Info* info);
// The bank of a music command of the program (an index of its sounds), NULL if it isn't one or the bank wasn't found
const char* AUTHMusic_BankOf(uint32_t sound);

#endif

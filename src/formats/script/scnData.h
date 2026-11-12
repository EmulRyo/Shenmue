#ifndef __SCNDATA_H__
#define __SCNDATA_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/scnBytecode.h"
#include "formats/script/scnPair.h"
#include "formats/script/sh4Program.h"

// What a scene program does with the map, read from its calls to the functions of the game whose arguments are
// constants or strings: the packages (PKF/PKS files) it loads, the cutscenes it plays and those it loads in the table of
// cutscenes, the characters it wakes or puts to sleep and those it uses, the sound banks and other files it names. Each
// call gives a reference, in the order of the calls; the same name can be referenced by many calls

#define SD_NAME_SIZE        32
#define SD_GAME_FUNCTIONS   512     // Size of the table of the character functions (IDs 0 to 0x1CC)
#define SD_CUTSCENE_SLOTS   70      // Of the table of cutscenes of the game (0x0C224178 in 1ST_READ.BIN PAL)

// The functions of the game read (see resources/Shenmue1GameFunctions.tsv)
#define SD_GAME_CUTSCENE    0x050   // (slot): plays a cutscene of the table
#define SD_GAME_ACTIVE      0x0A8   // (character, 1 / 0): wakes or puts to sleep a character
#define SD_GAME_SOUND_BANKS 0x0F3   // (8 names): the sound banks of the 8 slots
#define SD_GAME_PACKAGE     0x13C   // (0, 0, folder, name): loads a package; (0, 8, AFS, entry, entry): one of an AFS
#define SD_GAME_SEQUENCE    0x13E   // (0, slot, file, package): loads an AUTH file in a slot of the table of cutscenes
#define SD_GAME_SOUND_BANK  0x16D   // (0, slot, name, 0): loads a sound bank in a slot

typedef enum {
    SD_PACKAGE,         // name, folder (as given, e.g. "scene/01/JOMO" or "/scene/01/DCBN/", empty if not constant)
    SD_AFS_PACKAGE,     // name: the AFS file (e.g. "OP99.afs"), number: its entry of the PKF (the PKS is the next one)
    SD_CUTSCENE,        // number: the slot played
    SD_SEQUENCE,        // number: the slot, name: the file (e.g. "seqdata1.bin"), folder: its package (e.g. "D0W0")
    SD_WAKE,            // name: the character ID
    SD_SLEEP,           // name: the character ID
    SD_CHARACTER,       // name: a character ID passed to a character function
    SD_SOUND_BANK,      // number: the slot, name: the bank ("FREE" empties the slot)
    SD_FILE,            // name: another string with an extension (e.g. "M_MDOR.BIN", "SCROLL00.SPR")
    SD_KIND_COUNT
} SD_Kind;

typedef struct {
    uint8_t     kind;               // SD_Kind
    int16_t     game;               // ID of the function of the game called
    uint32_t    function;           // Index of the calling function (of the SH-4 program if there is one)
    uint32_t    address;            // Of the call
    int32_t     number;             // -1 if none
    char        name[SD_NAME_SIZE];
    char        folder[SD_NAME_SIZE];
} SD_Reference;

typedef struct {
    uint32_t        count;
    SD_Reference*   references;
} SD_Data;

// Reads the references of the calls of an SH-4 program or of a bytecode one. With both and their pair, the calls are
// those of the SH-4 program, with the arguments it doesn't give taken from the paired calls of the bytecode.
// characterFunctions: per game ID, the functions whose arguments can be character IDs (NULL: all of them). Returns false
// on memory error
bool SD_ReadProgram(SD_Data* data, const SH4_Program* sh4, const BC_Program* bytecode, const SCN_Pair* pair,
    const bool* characterFunctions);
void SD_FreeData(SD_Data* data);
const char* SD_KindName(SD_Kind kind);
// The value is a character ID: 4 characters (letters, digits or '_'), such as 'AKIR' (0x52494B41)
bool SD_IsCharacterId(uint32_t value);

#endif

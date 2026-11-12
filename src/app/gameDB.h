#ifndef __GAMEDB_H__
#define __GAMEDB_H__

#include <stdbool.h>
#include "ui/properties.h"

// The databases of the characters, of the maps and of the functions of the game called by the scene programs of Shenmue I
// (resources/Shenmue1Characters.tsv, resources/Shenmue1Maps.tsv and resources/Shenmue1GameFunctions.tsv, embedded in the
// executable), loaded on the first use. The returned strings live until the
// end of the application. The IDs are compared ignoring the case

typedef struct {
    const char* id;
    const char* model;  // One or two, separated by a comma
    const char* name;   // Empty for the characters without a name
    const char* area;
    const char* place;
} GameDB_Character;

typedef struct {
    const char* id;
    const char* ipac;   // For the maps stored in an IPAC block of another map, empty otherwise
    const char* name;
    const char* area;
} GameDB_Map;

// NULL if the ID isn't in the database
const GameDB_Character* GameDB_FindCharacter(const char* id);
// Name of the character, NULL if it isn't in the database or has no name
const char* GameDB_CharacterName(const char* id);
// The name of the character, or its ID if it has none
const char* GameDB_CharacterDisplay(const char* id);
// Its ID followed by its name, or the ID alone if it has no name: for the lists, which are filtered by both
const char* GameDB_CharacterLabel(const char* id);

// Where the character is found: its area and its place, separated by a comma. NULL if it isn't in the database or has
// neither
const char* GameDB_CharacterPlace(const char* id);
// The name of the character and where it is found, if they are known
void GameDB_CharacterProperties(Properties* properties, const char* id);

// A function of the game called by the scene programs by its ID (resources/Shenmue1GameFunctions.tsv)
typedef struct {
    int         id;
    const char* handler;    // Address of its code in 1ST_READ.BIN (PAL), empty for the IDs without one
    const char* arguments;  // Numbers of arguments of the calls of the programs, e.g. "3" or "1-4, 6"
    const char* name;       // Empty if unknown
    const char* description;
    bool        character;  // Its code looks up a character by an ID of its arguments (e.g. 'AKIR')
} GameDB_GameFunction;

// NULL if the ID isn't in the database
const GameDB_GameFunction* GameDB_FindGameFunction(int id);
// Name of the game function, NULL if it has none
const char* GameDB_GameFunctionName(int id);
// Per ID (0 to GAMEDB_GAME_FUNCTIONS - 1): the function looks up characters by an ID of its arguments
#define GAMEDB_GAME_FUNCTIONS 512
const bool* GameDB_CharacterFunctions(void);

// The map of an ID and an IPAC block (NULL or empty for the map itself). NULL if it isn't in the database
const GameDB_Map* GameDB_FindMap(const char* id, const char* ipac);
// The map of a file of its folder (e.g. scene/01/d000/mapinfo.bin is D000), or of the folder of an IPAC block of its
// folder (scene/02/mfsy/alpha/mapinfo.bin is the block ALPHA of MFSY). NULL if it isn't in the database
const GameDB_Map* GameDB_MapOfFile(const char* path);

#endif

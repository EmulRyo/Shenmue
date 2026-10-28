#ifndef __MOTN_H__
#define __MOTN_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// MOTN: a bank of motions of characters (no magic). The motion i has the ID firstID + 1 + i, the number the cutscenes
// and the character scripts play it by. Only the names of the motions are read

typedef struct {
    uint32_t        firstID;
    uint32_t        count;
    const char**    names;  // Point into data
    uint8_t*        data;
    size_t          size;
} MOTN_Bank;

// Loads the names of the motions of a MOTN file (gzipped or not). Returns false if it is not a valid MOTN file
bool MOTN_LoadNames(const char* filepath, MOTN_Bank* bank);
void MOTN_Free(MOTN_Bank* bank);
// Name of the motion with this ID, NULL if it is not in the bank
const char* MOTN_FindName(const MOTN_Bank* bank, uint32_t id);

#endif

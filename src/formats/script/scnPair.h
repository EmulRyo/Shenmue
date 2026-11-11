#ifndef __SCNPAIR_H__
#define __SCNPAIR_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/scnBytecode.h"
#include "formats/script/sh4Program.h"

// The two versions of the program of a map: the SH-4 code of the SCN3 chunk of its mapinfo.bin and the bytecode of its
// SCN file (scenario/actNN_<map>.scn). They have the same functions, in the same order: the function i of the bytecode is
// the function i + 1 of the SH-4 code, whose first function is the resume and yield code at codeStart. Most functions
// make the same calls to the game, in the same order; within them, each call of the SH-4 code is paired with the call of
// the bytecode at the same place, whose arguments are often better known (the SH-4 code computes some of them in
// registers that are not followed)

typedef struct {
    uint32_t    functionCount;      // Of the SH-4 program
    int32_t*    functions;          // Per SH-4 function: its bytecode function, -1 if none
    bool*       sameCalls;          // Per SH-4 function: the same calls (local, game and services) in the same order
    uint32_t    pairedFunctions;
    uint32_t    sameFunctions;      // With the same calls
    uint32_t    callCount;          // Of the SH-4 program
    int32_t*    calls;              // Per SH-4 call: its bytecode call, -1 if none
} SCN_Pair;

// Pairs the functions and the calls. Returns false on memory error, or if the programs don't have the same functions
bool SCN_PairPrograms(SCN_Pair* pair, const SH4_Program* sh4, const BC_Program* bytecode);
void SCN_FreePair(SCN_Pair* pair);
// The bytecode call paired with a call of the SH-4 program (its index in sh4->calls), NULL if none
const SH4_Call* SCN_PairedCall(const SCN_Pair* pair, const BC_Program* bytecode, uint32_t sh4Call);

#endif

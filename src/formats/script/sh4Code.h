#ifndef __SH4CODE_H__
#define __SH4CODE_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/sh4.h"

// Exploration of a block of SH-4 code: follows the flow from the roots (the entry points), through the branches, the
// delay slots and the relative branches (braf, bsrf) whose offset is a literal, and marks every 16-bit word as code, a
// literal read by a PC-relative load, or unknown. The functions of the scene programs start with mov.l r14,@-r13: the
// unknown runs that start with it are explored too, as functions called through pointers

// Kind of a word (bits)
#define SH4_WORD_CODE       0x01    // Instruction reached by the flow
#define SH4_WORD_LITERAL    0x02    // Read by a PC-relative load (mov.w, mov.l)
#define SH4_WORD_SLOT       0x04    // Instruction in the delay slot of a branch
#define SH4_WORD_TARGET     0x08    // Target of a branch
#define SH4_WORD_FUNCTION   0x10    // Start of a function: a root, a call target or a function found by its prologue

typedef struct {
    uint32_t    instructions;       // Words of code
    uint32_t    literals;           // Words of literals
    uint32_t    unknown;            // Words not reached
    uint32_t    functions;
    uint32_t    invalid;            // Reached words that aren't valid instructions
    uint32_t    illegalSlots;       // Branches in a delay slot
    uint32_t    overlaps;           // Words that are both code and literal
    uint32_t    outside;            // Branches and literals out of the block
    uint32_t    unresolved;         // braf and bsrf whose offset isn't a literal
    uint32_t    firstError;         // Address of the first invalid, illegal, overlapping or outside word, 0 if none
} SH4_CodeStats;

typedef struct {
    const uint8_t*  data;   // The addresses are offsets in data
    uint32_t        start;  // First word of the code
    uint32_t        end;    // After the last word
    uint8_t*        words;  // Kind of each word (SH4_WORD_*), indexed by (address - start) / 2
    SH4_CodeStats   stats;
} SH4_Code;

// Explores the code from start to end (offsets in data, start and end even) from the roots. Returns false on memory error
bool SH4_ExploreCode(SH4_Code* code, const uint8_t* data, uint32_t start, uint32_t end, const uint32_t* roots,
    uint32_t rootCount);
void SH4_FreeCode(SH4_Code* code);
// Kind of the word at an address, 0 if outside
uint8_t SH4_WordKind(const SH4_Code* code, uint32_t address);
// Target of a braf or bsrf: PC + 4 + the register, loaded from a literal by a mov.l @(disp,pc) of the same block up to
// 8 instructions before. Returns false if the register isn't loaded that way
bool SH4_RelativeTarget(const SH4_Code* code, const SH4_Instruction* branch, uint32_t* target);

#endif

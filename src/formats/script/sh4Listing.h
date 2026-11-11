#ifndef __SH4LISTING_H__
#define __SH4LISTING_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "formats/script/sh4Program.h"

// Listing of an analyzed SH-4 scene program (sh4Program.h): a line per instruction, literal or word not reached, with
// a comment that explains it: the values of the literals (strings of the data, IDs, offsets), the targets of the relative
// branches and the calls with their ID and arguments, e.g.
//   00059A26  400B  jsr @r0             game 0x13E("D0W0", "seqdata1.bin", 4, 0)

#define SH4_LINE_TEXT       48
#define SH4_LINE_COMMENT    160

typedef enum {
    SH4_LINE_FUNCTION,      // Header of a function, before its first instruction
    SH4_LINE_INSTRUCTION,
    SH4_LINE_LITERAL,       // .word or .long read by a PC-relative load
    SH4_LINE_UNKNOWN,       // .word not reached by the flow
} SH4_LineKind;

typedef struct {
    uint32_t    address;
    uint8_t     kind;       // SH4_LineKind
    uint8_t     size;       // Bytes of the line
    bool        label;      // A branch goes to it
    bool        slot;       // Instruction in a delay slot
    uint32_t    jump;       // Address the line leads to (branch, call, relative branch, mova), 0 if none
    char        text[SH4_LINE_TEXT];
    char        comment[SH4_LINE_COMMENT];
} SH4_Line;

// Maximum number of lines of a function (one per word, and its header)
uint32_t SH4_MaxFunctionLines(const SH4_Program* program, uint32_t function);
// Fills the lines of a function, from its start to the next function. Returns their number
uint32_t SH4_FunctionLines(const SH4_Program* program, uint32_t function, SH4_Line* lines);
// Line of a function that contains an address, -1 if none
int SH4_LineAt(const SH4_Line* lines, uint32_t count, uint32_t address);
// Call at the address of a jsr, bsr or bsrf, NULL if none
const SH4_Call* SH4_CallAt(const SH4_Program* program, uint32_t address);
// Name of a function of the game by its ID, NULL if unknown: given by the application, which has the database of them
typedef const char* (*SH4_GameNamer)(int id);
void SH4_SetGameNamer(SH4_GameNamer namer);
// Text of a call, e.g. "game 0x13E(0, 4)" ("game 0x13E SequenceData(0, 4)" with a namer), "call 0x74(?, 1)", "yield"
void SH4_DescribeCall(const SH4_Program* program, const SH4_Call* call, char* text, size_t size);
// Text of a value: a number, a string of the data, a field of the context...
void SH4_DescribeValue(const SH4_Program* program, const SH4_Value* value, char* text, size_t size);
// Whole null-terminated string that starts at an offset of the data, NULL if there is none (the previous byte isn't null
// or the text isn't printable)
const char* SH4_DataString(const SH4_Program* program, uint32_t offset);
// Writes the listing of all the functions
void SH4_WriteListing(const SH4_Program* program, FILE* file);

#endif

#ifndef __SH4PROGRAM_H__
#define __SH4PROGRAM_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/sh4Code.h"

// Analysis of the SH-4 code of a scene program (SCN3 chunk of version 0x20000): its functions, basic blocks, calls and
// literals. The program runs as a coroutine of the game. The code at codeStart receives a context in r5 and keeps it in
// r8 for all the functions, which call the game through the function pointers of the context:
//   jsr to ctx[0x28]: function of the game, ID in r5, arguments pushed on the stack r13 (r6 = r13) and popped after
//   jsr to ctx[0x2C], ctx[0x30]: other services with an ID in r5
//   jsr to ctx[0x3C]: yield, returns to the game until the program is resumed
// The functions of the program call each other with bsrf (PC-relative, offset in a literal), and the data is addressed
// as ctx[0x04] + offset. Within a basic block, the registers are followed as constants, fields of the context or
// pointers into the data, so each call gets its ID and the values of its arguments when they are constant

typedef enum {
    SH4_CALL_LOCAL,     // bsr/bsrf to a function of the program
    SH4_CALL_GAME,      // ctx[0x28]
    SH4_CALL_SERVICE2C, // ctx[0x2C]
    SH4_CALL_SERVICE30, // ctx[0x30]
    SH4_CALL_YIELD,     // ctx[0x3C]
    SH4_CALL_CONTEXT,   // Another pointer of the context (0x14, 0x18, 0x1C)
    SH4_CALL_UNKNOWN,   // Target not known
} SH4_CallKind;

typedef enum {
    SH4_VALUE_UNKNOWN,
    SH4_VALUE_CONSTANT, // value
    SH4_VALUE_DATA,     // Pointer into the data: ctx[0x04] + value
    SH4_VALUE_FIELD,    // Field of the context: ctx[value]
    SH4_VALUE_CONTEXT,  // The context itself (r8)
    SH4_VALUE_STACK,    // The stack pointer r13 + value
    SH4_VALUE_VARIABLE, // Address of a variable of the program: ctx[0x0C] + value (r9), an area of the game of the size
                        // given by the field 0x18 of the header
    SH4_VALUE_PARAMETER,// The parameter number value of the calling function (bytecode only: push.l [fp + 8 + 4 * value])
} SH4_ValueKind;

typedef struct {
    uint8_t     kind;       // SH4_ValueKind
    uint32_t    value;
    uint32_t    literal;    // Address of the literal it comes from, 0 if none
} SH4_Value;

#define SH4_MAX_ARGUMENTS 12

typedef struct {
    uint32_t    address;    // Of the jsr, bsr or bsrf
    uint32_t    function;   // Index of the calling function
    uint8_t     kind;       // SH4_CallKind
    uint8_t     context;    // Offset of the pointer in the context (SH4_CALL_GAME... SH4_CALL_CONTEXT)
    int32_t     id;         // ID in r5, -1 if not constant
    uint32_t    target;     // SH4_CALL_LOCAL: called function (address)
    int32_t     argumentBytes;  // Popped from r13 after the call (add #n,r13), -1 if not found
    uint32_t    argumentCount;  // Up to SH4_MAX_ARGUMENTS, in the order of the parameters: the first one is the last
                                // push (the top of the stack). Those pushed before the block of the call are unknown
    SH4_Value   arguments[SH4_MAX_ARGUMENTS];
} SH4_Call;

typedef enum {
    SH4_LITERAL_VALUE,          // Used as a number
    SH4_LITERAL_CODE_OFFSET,    // Offset of a braf/bsrf
    SH4_LITERAL_DATA_OFFSET,    // Added to the base of the data (ctx[0x04])
    SH4_LITERAL_CALL_ID,        // ID of a call to the game or to a service
} SH4_LiteralKind;

typedef struct {
    uint32_t    address;
    uint32_t    value;      // Zero-extended
    uint8_t     size;       // 2 or 4
    uint8_t     kind;       // SH4_LiteralKind
    uint32_t    reads;      // Number of loads
} SH4_Literal;

typedef struct {
    uint32_t    start;
    uint32_t    end;        // After the last instruction (the delay slot included)
    uint32_t    function;
    uint32_t    target;     // Branch target of the last instruction, 0 if none or unknown
    bool        fallsThrough;   // The flow can continue at end
} SH4_Block;

typedef struct {
    uint32_t    start;
    uint32_t    end;        // Start of the next function, or end of the code: the literals of the function included
    uint32_t    firstBlock;
    uint32_t    blockCount;
    uint32_t    callers;    // Local calls to it
} SH4_Function;

typedef struct {
    SH4_Code        code;
    uint32_t        dataStart;  // Offsets of the data, to check the data references
    uint32_t        dataEnd;
    uint32_t        functionCount;
    SH4_Function*   functions;
    uint32_t        blockCount;
    SH4_Block*      blocks;
    uint32_t        callCount;
    SH4_Call*       calls;
    uint32_t        literalCount;
    SH4_Literal*    literals;   // Sorted by address
    uint32_t        contextWrites;  // Instructions that write r8, r9 or r10 outside the code at codeStart (0 in the game
                                    // files: they hold the context, ctx[0x0C] and ctx[0x10] in all the functions)
} SH4_Program;

// Analyzes the SH-4 program of a SCN3 chunk: data from the "SCN3" ID, with its header fields. Returns false on error
bool SH4_AnalyzeProgram(SH4_Program* program, const uint8_t* data, uint32_t codeStart, uint32_t dataStart,
    uint32_t dataEnd, uint32_t entry);
void SH4_FreeProgram(SH4_Program* program);
// Index of the function that contains an address, -1 if none
int SH4_FunctionAt(const SH4_Program* program, uint32_t address);
// Literal at an address, NULL if none
const SH4_Literal* SH4_LiteralAt(const SH4_Program* program, uint32_t address);
const char* SH4_CallKindName(SH4_CallKind kind);

#endif

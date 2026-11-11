#ifndef __SCNBYTECODE_H__
#define __SCNBYTECODE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/script/scn.h"
#include "formats/script/sh4Listing.h"
#include "formats/script/sh4Program.h"

// Bytecode of the scene programs of version 0x100 (SCN files, and the SCN3 chunk of some mapinfo.bin): the instructions
// of a stack machine, of 1 to 5 bytes. The two highest bits of the first byte give its group:
//   00: control, 01: push, 10: operator (no operand), 11: not an instruction (skipped by the machine)
// For the control and push groups, bits 4-5 give the size of the operand that follows (0, 1, 2 or 4 bytes, little-endian
// and signed) and bits 0-3 the operation. The machine has a stack of 32-bit values, a frame pointer (fp, its local
// variables at negative offsets and its arguments from fp+8), and a result register, set by the calls and by the 0x80
// operator, that the conditional branch tests. See the SCN page of the wiki

typedef enum {
    BC_RET,             // Return (ends the thread at its first function)
    BC_CALL,            // Call the function at the offset n of the program
    BC_CALL_POP,        // Call the function at the address popped
    BC_STACK,           // sp += n: drops n / 4 values, or makes room for the local variables (n < 0)
    BC_JUMP,            // Jump to the offset n of the program
    BC_BRANCH,          // Jump n bytes from the end of the instruction
    BC_BRANCH_ZERO,     // The same if the result is 0
    BC_ARGUMENT_COUNT,  // Number of arguments of the next call to the service of ctx[0x2C]
    BC_DATA_SERVICE,    // Calls the service of ctx[0x2C] with ID 1 and the address at the offset n of the program (in its
                        // data), and yields
    BC_NATIVE,          // Function n of the table of the machine, with the stack (not used by the game files)
    BC_NATIVE4,         // Function n + 1 of that table, with 4 arguments (not used by the game files)
    BC_GAME,            // Function of the game with the ID n (as ctx[0x28] of the SH-4 programs)
    BC_SERVICE2C,       // Service of ctx[0x2C] with the ID n, and yields
    BC_SERVICE30,       // Service of ctx[0x30] with the ID n
    BC_PUSH,            // Push a value (see BC_Area)
    BC_OPERATOR,        // Operator on the values of the stack
    BC_INVALID,
} BC_Kind;

// What a push pushes
typedef enum {
    BC_AREA_CONSTANT,   // n
    BC_AREA_PROGRAM,    // Address of the offset n of the program: a function, or a variable or string of its data
    BC_AREA_GLOBAL,     // Variable at n of an area of the game (not used by the game files)
    BC_AREA_DATA,       // Variable at n of the data of the program (dataStart + n)
    BC_AREA_LOCAL,      // Variable at fp + n
} BC_Area;

typedef struct {
    uint32_t    address;
    uint8_t     size;       // Bytes, operand included
    uint8_t     byte;       // First byte
    uint8_t     kind;       // BC_Kind
    uint8_t     area;       // BC_Area of the pushes
    uint8_t     width;      // Pushes of a variable: 1, 2 or 4 bytes (signed), 0 for its address
    int32_t     operand;
    uint32_t    target;     // Offset in the program of the calls, jumps, branches, code pushes and data services
} BC_Instruction;

// Decodes the instruction at pc, in data of size bytes (offsets from the start of the program). Returns false if it isn't
// an instruction or doesn't fit
bool BC_Decode(const uint8_t* data, uint32_t size, uint32_t pc, BC_Instruction* ins);
// e.g. "push 1", "push.l [fp+8]", "push &data+0x78", "game 0x1F", "sp += 12", "jz 0x120", "f+"
int BC_Format(const BC_Instruction* ins, char* text, size_t size);
// Name of an operator (0x80 to 0xA6), NULL if none
const char* BC_OperatorName(uint8_t byte);

// The program: its functions (starts at the code, the entry point, the targets of the calls, the code pushed as a value
// and the code values of the relocations) and its calls, with their arguments when they are pushed as constants or
// addresses of the data just before the call
typedef struct {
    const SCN_Program*  scn;
    uint32_t            codeEnd;        // End of the instructions: dataStart, or the start of the 0xFF padding
    uint32_t            instructionCount;
    uint32_t            invalidCount;   // Bytes that aren't instructions before codeEnd
    uint32_t            functionCount;
    uint32_t*           functions;      // Start of each function, sorted
    uint32_t            callCount;
    SH4_Call*           calls;          // As for the SH-4 programs: kind, ID, arguments (values of kind DATA are offsets
                                        // from dataStart), sorted by address
    uint8_t*            starts;         // A byte per byte of the code: 1 at the start of an instruction, 2 for a target
} BC_Program;

bool BC_AnalyzeProgram(BC_Program* program, const SCN_Program* scn);
void BC_FreeProgram(BC_Program* program);
// Index of the function that contains an address, -1 if none
int BC_FunctionAt(const BC_Program* program, uint32_t address);
uint32_t BC_FunctionEnd(const BC_Program* program, uint32_t function);
// Lines of the listing of a function, as those of the SH-4 programs (sh4Listing.h). Returns their number
uint32_t BC_MaxFunctionLines(const BC_Program* program, uint32_t function);
uint32_t BC_FunctionLines(const BC_Program* program, uint32_t function, SH4_Line* lines);
// Whole string at an offset of the data, NULL if none
const char* BC_DataString(const BC_Program* program, uint32_t offset);
// Call at an address, NULL if none
const SH4_Call* BC_CallAt(const BC_Program* program, uint32_t address);
// Text of a call, as SH4_DescribeCall
void BC_DescribeCall(const BC_Program* program, const SH4_Call* call, char* text, size_t size);
// Writes the listing of all the functions
void BC_WriteListing(const BC_Program* program, FILE* file);

#endif

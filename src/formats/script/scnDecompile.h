#ifndef __SCNDECOMPILE_H__
#define __SCNDECOMPILE_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "formats/script/scnBytecode.h"

// Pseudo-C of the functions of a bytecode program (scnBytecode.h): the values of the stack machine become expressions,
// the stores and the calls statements, and the branches if, else, while and for blocks (or gotos when they don't fit).
// The names:
//   arg0, arg1...      the arguments of the function (fp+8, fp+12...)
//   local_18           a local variable (fp-0x18)
//   data_78            a variable of the data of the program (offset 0x78), "text" for the address of a string
//   func_932           a function of the program
//   Name or game_13E   a function of the game (with its name if the namer of sh4Listing.h knows it)
//   service2C_0, service30_1   the functions of the services of the context
//   x.b, x.w, x[i]     the byte, halfword or word of a variable, an element of an array
//   b[a], w[a], l[a]   the byte, halfword or word at a computed address
// The values have no types: the floats are written as such when they look like floats ("1.5f")

typedef struct {
    uint32_t    address;    // Of the first instruction of the line, the start of the function for its header
    uint16_t    indent;
    char*       text;
} DC_Line;

typedef struct {
    uint32_t    lineCount;
    DC_Line*    lines;
    uint32_t    gotoCount;      // Branches written as goto
    uint32_t    stackErrors;    // Values left on the stack at a label, or popped from an empty stack
} DC_Function;

// Decompiles a function of the program. Returns false on memory error
bool DC_DecompileFunction(const BC_Program* program, uint32_t function, DC_Function* out);
void DC_FreeFunction(DC_Function* function);
// Writes the pseudo-C of all the functions
void DC_WriteProgram(const BC_Program* program, FILE* file);

#endif

#ifndef __SCNVM_H__
#define __SCNVM_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/scnBytecode.h"

// Emulation of the bytecode of a scene program (scnBytecode.h), as the interpreter of 1ST_READ.BIN (PAL) runs it, to find
// the values that the program computes at run time: the arguments of its calls to the game. The game isn't emulated: its
// functions return unknown values, and the variables of the program start unknown. Every value carries whether it is
// known; an operation with an unknown operand gives an unknown value, and a conditional branch on an unknown result
// follows both ways (the states already seen at a branch are not run again). The threads that the program starts with
// the function 0x002 of the game (StartThread) are run too, from their function and arguments. The exploration is
// bounded: VM_MAX_STEPS instructions and VM_MAX_STATES branches per thread, VM_MAX_TOTAL_STEPS instructions in all

#define VM_PROGRAM_BASE     0x10000000  // Address of the offset 0 of the program (the push operations 1 to 3)
#define VM_VARIABLES_BASE   0x20000000  // Of the variables of the program (8 to 11)
#define VM_STACK_BASE       0x30000000  // Of the stack of a thread
#define VM_STACK_SIZE       0x4000
#define VM_MAX_STEPS        200000      // Per thread
#define VM_MAX_TOTAL_STEPS  4000000     // Per program: the threads after it aren't run
#define VM_MAX_STATES       4000
#define VM_MAX_THREADS      512
#define VM_MAX_ARGUMENTS    12
#define VM_START_THREAD     0x002       // ID of StartThread: (function, count, arguments...)

// A call to the game run with some known arguments
typedef struct {
    uint32_t    address;                        // Of the instruction
    int32_t     id;
    uint32_t    count;                          // Arguments given (the operation sp += 4 * count after the call)
    uint32_t    values[VM_MAX_ARGUMENTS];
    uint16_t    known;                          // Bit i: argument i is known
} VM_Call;

typedef struct {
    uint32_t    callCount;
    VM_Call*    calls;          // Each address, arguments and known bits once, sorted by address
    uint32_t    threads;        // Run, the first one from the entry point
    uint32_t    skipped;        // Threads not run: the limit of steps of the program was reached
    uint64_t    steps;
    uint32_t    states;         // Branches followed both ways
    uint32_t    truncated;      // Threads stopped by a limit
    uint32_t    errors;         // Paths stopped by an invalid instruction or stack
} VM_Result;

// An environment gives what the emulation can know of the game: the results of some of its functions and the memory
// they write (e.g. the date and the hour of the game), so that the branches on them follow only one way. It can also
// follow each path: it keeps a state in VM_PATH_SIZE bytes of the path (0 at the start of a thread, copied when the path
// forks at a branch) and it is told when the path settles: when its thread yields for the first time (a service of
// ctx[0x2C], which waits for the next frames) or ends without yielding

#define VM_PATH_SIZE        64
#define VM_ANSWER_BYTES     16

// What the game does at a call, as the environment knows it
typedef struct {
    bool        known;                          // The result is known
    uint32_t    value;
    uint32_t    address;                        // Where the game writes bytes (a known address of the program, of its
                                                // variables or of the stack), if size > 0
    uint32_t    size;                           // Up to VM_ANSWER_BYTES
    uint8_t     bytes[VM_ANSWER_BYTES];
    uint16_t    knownBytes;                     // Bit i: bytes[i] is known (the others are written unknown)
} VM_Answer;

typedef struct {
    void*       context;
    // At each call to the game run, in the order of its path, with its arguments (known ones and their values) and
    // whether its thread has yielded before on the path. answer starts unknown. Can be NULL
    void        (*call)(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Answer* answer);
    // When a path yields for the first time or ends its thread before. thread: the index of the thread, in the order in
    // which they are started (0 is the entry point). Can be NULL
    void        (*settled)(void* context, uint32_t thread, const uint8_t* path);
} VM_Environment;

// Runs the program from its entry point. Returns false on memory error
bool VM_Run(const BC_Program* program, VM_Result* result);
// The same in an environment (NULL: the functions of the game return unknown values)
bool VM_RunEnvironment(const BC_Program* program, const VM_Environment* environment, VM_Result* result);
void VM_Free(VM_Result* result);
// The offset in the data of a known argument that is an address of the data of the program, -1 if it isn't one
int64_t VM_DataOffset(const BC_Program* program, uint32_t value);

#endif

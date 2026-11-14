#ifndef __SCNVM_H__
#define __SCNVM_H__

#include <stdbool.h>
#include <stdint.h>
#include "scenario/scnBytecode.h"

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
#define VM_GAME_BASE        0x40000000  // Of the memory of the game kept by the environment (VM_GAME_SIZE bytes)
#define VM_GAME_SIZE        0x800
#define VM_DISTANCE_NONE    0xFFFFFFFFu // VM_Environment.distances
#define VM_DISTANCE_RETURN  0xFFFFFFFEu
#define VM_MAX_STEPS        200000      // Per thread
#define VM_MAX_TOTAL_STEPS  4000000     // Per program: the threads after it aren't run
#define VM_MAX_STATES       4000
#define VM_MAX_THREADS      512
#define VM_MAX_ARGUMENTS    12
#define VM_WAIT_READS       16          // Reads of the variables kept per wait (VM_Environment.releaseWaits)
#define VM_START_THREAD     0x002       // ID of StartThread: (function, count, arguments...)
#define VM_KILL_THREAD      0x003       // ID of KillThread: (thread returned by StartThread, 0 for the current one)

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
// ctx[0x2C], which waits for the next frames) or ends without yielding. It can keep what the game knows (e.g. the
// positions of the characters) in the memory of the game, VM_GAME_SIZE bytes at VM_GAME_BASE that the program doesn't
// address: unknown at the start, copied when the path forks, kept in the snapshots and shared by the threads of a run
// with time

#define VM_PATH_SIZE        64
#define VM_ANSWER_BYTES     16

// Reads the memory of the thread that makes a call
typedef struct VM_Memory VM_Memory;

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
    // At each call to the game run, in the order of its path, with its arguments (known ones and their values), whether
    // its thread has yielded before on the path, and the memory of the path (VM_ReadMemory, VM_WriteMemory). answer
    // starts unknown. Can be NULL
    void        (*call)(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Memory* memory,
                    VM_Answer* answer);
    // When a path yields for the first time or ends its thread before. thread: the index of the thread, in the order in
    // which they are started (0 is the entry point). Can be NULL
    void        (*settled)(void* context, uint32_t thread, const uint8_t* path);
    // Before each call to the game run, with its arguments: returns true to keep the state of the path at the call and
    // stop the emulation (VM_Capture). Can be NULL
    bool        (*capture)(void* context, const VM_Call* call, const uint8_t* path);
    // A path that waits without changing anything (a yield whose state is the one of its previous yield: a loop that
    // waits for a variable set by another thread, e.g. while (var_408 < 1) wait(1)) goes on with the variables that it
    // has read since its previous yield unknown, as when the other threads have run (all the variables if it has read
    // more than VM_WAIT_READS times); the others keep their values. Else it waits until the limit of steps
    bool        releaseWaits;
    // For each offset of the code (up to the end of the code), how far it is from what the environment looks for (e.g.
    // the call that it captures), VM_DISTANCE_NONE if it can't get there, VM_DISTANCE_RETURN if that depends on where
    // its function returns: at a branch on an unknown value, the path goes on first the way with the smaller distance
    // (the other one is run after it), and a way that can't get there, nor after any of the returns of the path (the
    // return addresses of its stack), isn't followed. NULL: the way of a value other than 0 first
    const uint32_t* distances;
    // Waits (services of ctx[0x2C]) after which a path is dropped, 0 for no limit: the paths that loop frame after frame
    // on unknown values (e.g. a menu that waits for the choice of the player) give way to the other ones
    uint32_t    maxWaits;
    // Before the run, with the memory of the first thread: writes the variables of the program known at the start (the
    // others are unknown). Can be NULL
    void        (*initialize)(void* context, VM_Memory* memory);
    // Instructions run in all, and per thread, 0 for VM_MAX_TOTAL_STEPS and VM_MAX_STEPS
    uint64_t    maxSteps;
} VM_Environment;

// Runs the program from its entry point. Returns false on memory error
bool VM_Run(const BC_Program* program, VM_Result* result);
// The same in an environment (NULL: the functions of the game return unknown values)
bool VM_RunEnvironment(const BC_Program* program, const VM_Environment* environment, VM_Result* result);
// The same from a function of the program, run as a thread with unknown arguments
bool VM_RunFrom(const BC_Program* program, uint32_t function, const VM_Environment* environment, VM_Result* result);
void VM_Free(VM_Result* result);

// The state of a path at a call to the game: its memory, its stack and the functions that called it
typedef struct VM_Snapshot VM_Snapshot;

// Runs the program in an environment as VM_RunEnvironment, until the environment captures a call. Returns the state of
// the path at that call, NULL if none was captured or on memory error
VM_Snapshot* VM_Capture(const BC_Program* program, const VM_Environment* environment);
// The same from a function of the program, run as a thread with unknown arguments (the variables are unknown too)
VM_Snapshot* VM_CaptureFrom(const BC_Program* program, uint32_t function, const VM_Environment* environment);
void VM_FreeSnapshot(VM_Snapshot* snapshot);
// Address of the call of the snapshot
uint32_t VM_SnapshotAddress(const VM_Snapshot* snapshot);
// size bytes of the memory of the game of the snapshot at an address of it (VM_GAME_BASE...), as VM_ReadMemory
bool VM_ReadSnapshotGame(const VM_Snapshot* snapshot, uint32_t address, uint32_t size, uint8_t* bytes, uint32_t* known);

// Run with time: from a snapshot, the threads of the program run frame after frame, as the game runs them while a
// cutscene plays. The thread of the snapshot runs from the start of the function of its call (rewind), or from the call
// itself; the frame 0 is the frame of that call, and the code of the function before it runs without time (its waits
// don't wait). A wait (a service of ctx[0x2C]) yields: the service 0 waits the number of frames of its argument, the
// others 1 frame. The threads started with StartThread run from the frame they are started, after those started before
// them, and KillThread ends them. The values aren't forked: a conditional branch on an unknown result goes the way of 0
// (the loops that wait for something of the game end, the blocks of an unknown condition are skipped). A thread that
// doesn't wait within VM_TIMED_STEPS instructions is stopped

#define VM_TIMED_STEPS      200000
#define VM_TIMED_THREADS    64
#define VM_TIMED_PRE_WAITS  1000        // Waits of the thread of the snapshot before its call, which don't wait

typedef struct {
    void*       context;
    // At each call to the game, at a frame, by a thread (0: the thread of the snapshot, then those started, in order).
    // answer starts unknown (StartThread and KillThread are answered by the machine). Can be NULL
    void        (*call)(void* context, uint32_t frame, uint32_t thread, const VM_Call* call, VM_Memory* memory,
                    VM_Answer* answer);
} VM_TimedEnvironment;

typedef struct {
    bool        started;        // The call of the snapshot was run
    uint32_t    frames;         // Run, up to the number asked (fewer if every thread ended)
    uint32_t    threads;        // Run, the thread of the snapshot included
    uint32_t    stopped;        // Stopped by the limit of instructions, an invalid instruction or stack
    uint64_t    steps;
} VM_TimedResult;

// Runs frames frames from a snapshot. Returns false on memory error
bool VM_RunTimed(const BC_Program* program, const VM_Snapshot* snapshot, bool rewind, uint32_t frames,
    const VM_TimedEnvironment* environment, VM_TimedResult* result);
// size bytes of the memory at an address (of the program, of its variables or of the stack of the thread). Bit i of
// known: bytes[i] is known. Returns false if the address isn't valid
bool VM_ReadMemory(const VM_Memory* memory, uint32_t address, uint32_t size, uint8_t* bytes, uint32_t* known);
// Writes size bytes (up to 32) to the memory, as the game does at a call. Returns false if the address isn't valid
bool VM_WriteMemory(VM_Memory* memory, uint32_t address, uint32_t size, const uint8_t* bytes, uint32_t known);
// The offset in the data of a known argument that is an address of the data of the program, -1 if it isn't one
int64_t VM_DataOffset(const BC_Program* program, uint32_t value);

#endif

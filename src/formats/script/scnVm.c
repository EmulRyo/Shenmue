#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "formats/script/scnVm.h"

typedef struct {
    uint32_t    value;
    bool        known;
} Value;

static const Value g_unknown = { 0, false };

// A path of a thread: the registers and the memory that the program can write. The writes to the program itself (its
// data) are kept in an overlay of bytes
typedef struct {
    uint32_t    pc;         // Offset in the program
    uint32_t    sp;         // Addresses
    uint32_t    fp;
    Value       result;
    uint8_t*    variables;  // variableSize bytes, then their known flags
    uint8_t*    stack;      // The bytes of the stack from the offset stackBase to VM_STACK_SIZE: value, known
    uint32_t    stackBase;  // Grows down when the stack is written below it
    uint32_t    stackLow;   // Offset (>= stackBase) below which the bytes of the stack are 0 and unknown
    uint8_t*    game;       // VM_GAME_SIZE bytes of the memory of the game, then their known flags
    bool        gameUsed;   // Else the memory of the game is 0 and unknown, whatever is in game
    uint32_t    overlayCount;
    uint32_t    overlayCapacity;
    uint32_t*   overlayAddresses;
    uint8_t*    overlayBytes;   // Value, known
    bool        yielded;        // The thread has yielded on this path
    uint8_t     path[VM_PATH_SIZE]; // Of the environment
    uint8_t     status;         // Of a thread run with time (ThreadStatus)
    uint32_t    wake;           // Frame at which a thread run with time goes on after a wait
    uint64_t    waitHash;       // Of the state at its last yield (VM_Environment.releaseWaits)
    uint32_t    readCount;      // Reads of the variables since its last yield (VM_WAIT_READS at most are kept)
    uint32_t    reads[VM_WAIT_READS]; // Address of the first byte in the low 24 bits, size in the high 8 bits
    uint32_t    waits;          // Yields on the path (VM_Environment.maxWaits)
} State;

typedef enum {
    THREAD_RUNNING,
    THREAD_WAITING,
    THREAD_ENDED,
} ThreadStatus;

struct VM_Snapshot {
    State*      state;
    uint32_t    address;
    uint32_t    variableSize;
};

// The threads of a run with time
typedef struct {
    const VM_TimedEnvironment* environment;
    State**     threads;        // VM_TIMED_THREADS
    uint32_t    count;
    uint32_t    current;
    uint32_t    frame;
    uint32_t    start;          // Address of the call of the snapshot
    bool        started;
    uint8_t*    variables;      // Shared by the threads
    uint8_t*    game;           // Memory of the game, shared by the threads
} Timeline;

// A thread to run: its function and arguments, and the variables when it was started
typedef struct {
    uint32_t    function;
    uint32_t    count;
    Value       arguments[VM_MAX_ARGUMENTS];
    uint8_t*    variables;
} Root;

#define VM_SPARE_STATES 64
#define VM_STACK_CHUNK  0x100   // Bytes of the stack allocated at first

typedef struct {
    const BC_Program*   program;
    uint32_t            programSize;    // Bytes of the program addressed: up to the end of the data
    uint32_t            variableSize;
    VM_Result*          result;
    uint32_t            callCapacity;
    uint32_t*           callTable;      // Open addressing: index + 1 of the calls, 0 if empty
    uint32_t            callTableSize;
    uint32_t            rootCount;
    Root*               roots;
    uint64_t*           rootHashes;
    uint64_t*           seen;           // Hashes of the states at the branches of the current thread
    uint32_t            seenCount;
    uint32_t            seenSize;
    State**             work;
    uint32_t            workCount;
    uint64_t            steps;          // Of the current thread
    uint32_t            thread;         // Index of the current thread
    const VM_Environment* environment;  // NULL if none
    bool                ok;
    bool                stop;           // A call was captured
    State*              captured;
    Timeline*           timeline;       // Of a run with time, NULL otherwise
    uint64_t            maxSteps;       // Per thread
    State*              spares[VM_SPARE_STATES];    // Paths ended, whose memory is reused by the next ones
    uint32_t            spareCount;
} Machine;

struct VM_Memory {
    const Machine*      machine;
    State*              state;
};

// Memory of the states

static void FreeState(State* state) {
    if (state == NULL)
        return;
    free(state->variables);
    free(state->stack);
    free(state->game);
    free(state->overlayAddresses);
    free(state->overlayBytes);
    free(state);
}

// A path ended: its memory is kept for the next ones (only those that own their memory, out of a run with time)
static void RecycleState(Machine* machine, State* state) {
    if (state == NULL || machine->timeline != NULL || machine->spareCount == VM_SPARE_STATES) {
        FreeState(state);
        return;
    }
    free(state->overlayAddresses);
    free(state->overlayBytes);
    machine->spares[machine->spareCount++] = state;
}

static void FreeSpares(Machine* machine) {
    while (machine->spareCount > 0)
        FreeState(machine->spares[--machine->spareCount]);
}

// The stack of a state from an offset: its bytes from stackLow are kept, those below are 0 and unknown. Returns false on
// memory error
static bool LowerStack(State* state, uint32_t offset) {
    if (offset >= state->stackLow)
        return true;
    if (offset < state->stackBase) {
        // Room down to the offset, at least twice as much as before
        uint32_t size = VM_STACK_SIZE - state->stackBase;
        uint32_t base = (offset / VM_STACK_CHUNK) * VM_STACK_CHUNK;
        if (size * 2 < VM_STACK_SIZE && VM_STACK_SIZE - size * 2 < base)
            base = VM_STACK_SIZE - size * 2;
        uint8_t* stack = malloc((VM_STACK_SIZE - base) * 2);
        if (stack == NULL)
            return false;
        memcpy(stack + (state->stackLow - base) * 2, state->stack + (state->stackLow - state->stackBase) * 2,
            (VM_STACK_SIZE - state->stackLow) * 2);
        free(state->stack);
        state->stack = stack;
        state->stackBase = base;
    }
    memset(state->stack + (offset - state->stackBase) * 2, 0, (state->stackLow - offset) * 2);
    state->stackLow = offset;
    return true;
}

// A state with its variables zeroed (unknown) if zero, else to be written by the caller. Its stack and the memory of the
// game start unknown
static State* AllocState(Machine* machine, bool zero) {
    if (machine->spareCount > 0) {
        State* state = machine->spares[--machine->spareCount];
        State reused = { 0 };
        reused.variables = state->variables;
        reused.stack = state->stack;
        reused.stackBase = state->stackBase;
        reused.stackLow = VM_STACK_SIZE;
        reused.game = state->game;
        *state = reused;
        if (zero)
            memset(state->variables, 0, machine->variableSize * 2 + 1);
        return state;
    }
    State* state = calloc(1, sizeof(State));
    if (state == NULL)
        return NULL;
    size_t variables = machine->variableSize * 2 + 1;
    state->variables = zero ? calloc(variables, 1) : malloc(variables);
    state->stack = malloc(VM_STACK_CHUNK * 2);
    state->stackBase = VM_STACK_SIZE - VM_STACK_CHUNK;
    state->stackLow = VM_STACK_SIZE;
    state->game = malloc(VM_GAME_SIZE * 2);    // Not used yet
    if (state->variables == NULL || state->stack == NULL || state->game == NULL) {
        FreeState(state);
        return NULL;
    }
    return state;
}

static State* NewState(Machine* machine) {
    return AllocState(machine, true);
}

static State* CopyState(Machine* machine, const State* source) {
    State* state = AllocState(machine, false);
    if (state == NULL)
        return NULL;
    state->pc = source->pc;
    state->sp = source->sp;
    state->fp = source->fp;
    state->result = source->result;
    state->yielded = source->yielded;
    memcpy(state->path, source->path, sizeof(state->path));
    state->waitHash = source->waitHash;
    state->readCount = source->readCount;
    memcpy(state->reads, source->reads, sizeof(state->reads));
    state->waits = source->waits;
    memcpy(state->variables, source->variables, machine->variableSize * 2);
    state->variables[machine->variableSize * 2] = 0;
    state->gameUsed = source->gameUsed;
    if (source->gameUsed)
        memcpy(state->game, source->game, VM_GAME_SIZE * 2);
    // The stack above sp: below it, the values are not used before they are written (they are left unknown)
    uint32_t used = source->sp >= VM_STACK_BASE && source->sp <= VM_STACK_BASE + VM_STACK_SIZE ? source->sp - VM_STACK_BASE : 0;
    uint32_t low = used > source->stackLow ? used : source->stackLow;
    if (!LowerStack(state, low)) {
        FreeState(state);
        return NULL;
    }
    state->stackLow = low;
    memcpy(state->stack + (low - state->stackBase) * 2, source->stack + (low - source->stackBase) * 2,
        (VM_STACK_SIZE - low) * 2);
    if (source->overlayCount > 0) {
        state->overlayAddresses = malloc(source->overlayCount * sizeof(uint32_t));
        state->overlayBytes = malloc(source->overlayCount * 2);
        if (state->overlayAddresses == NULL || state->overlayBytes == NULL) {
            FreeState(state);
            return NULL;
        }
        memcpy(state->overlayAddresses, source->overlayAddresses, source->overlayCount * sizeof(uint32_t));
        memcpy(state->overlayBytes, source->overlayBytes, source->overlayCount * 2);
        state->overlayCount = state->overlayCapacity = source->overlayCount;
    }
    return state;
}

// A byte of the memory, false if the address isn't one of the program, of its variables or of the stack
static bool ReadByte(const Machine* machine, const State* state, uint32_t address, uint8_t* byte, bool* known) {
    if (address >= VM_STACK_BASE && address < VM_STACK_BASE + VM_STACK_SIZE) {
        uint32_t offset = address - VM_STACK_BASE;
        const uint8_t* bytes = state->stack + (offset - state->stackBase) * 2;
        *byte = offset >= state->stackLow ? bytes[0] : 0;
        *known = offset >= state->stackLow && bytes[1] != 0;
        return true;
    }
    if (address >= VM_VARIABLES_BASE && address < VM_VARIABLES_BASE + machine->variableSize) {
        *byte = state->variables[address - VM_VARIABLES_BASE];
        *known = state->variables[machine->variableSize + address - VM_VARIABLES_BASE] != 0;
        return true;
    }
    if (address >= VM_GAME_BASE && address < VM_GAME_BASE + VM_GAME_SIZE) {
        *byte = state->gameUsed ? state->game[address - VM_GAME_BASE] : 0;
        *known = state->gameUsed && state->game[VM_GAME_SIZE + address - VM_GAME_BASE] != 0;
        return true;
    }
    if (address >= VM_PROGRAM_BASE && address < VM_PROGRAM_BASE + machine->programSize) {
        for (uint32_t i = state->overlayCount; i-- > 0;) {
            if (state->overlayAddresses[i] == address) {
                *byte = state->overlayBytes[i * 2];
                *known = state->overlayBytes[i * 2 + 1] != 0;
                return true;
            }
        }
        *byte = machine->program->scn->data[address - VM_PROGRAM_BASE];
        *known = true;
        return true;
    }
    return false;
}

// The memory of the game of a state, written: 0 and unknown before its first write
static void UseGame(State* state) {
    if (!state->gameUsed)
        memset(state->game, 0, VM_GAME_SIZE * 2);
    state->gameUsed = true;
}

static bool WriteByte(const Machine* machine, State* state, uint32_t address, uint8_t byte, bool known) {
    if (address >= VM_STACK_BASE && address < VM_STACK_BASE + VM_STACK_SIZE) {
        uint32_t offset = address - VM_STACK_BASE;
        if (offset < state->stackLow && !LowerStack(state, offset))
            return false;
        state->stack[(offset - state->stackBase) * 2] = byte;
        state->stack[(offset - state->stackBase) * 2 + 1] = known;
        return true;
    }
    if (address >= VM_VARIABLES_BASE && address < VM_VARIABLES_BASE + machine->variableSize) {
        state->variables[address - VM_VARIABLES_BASE] = byte;
        state->variables[machine->variableSize + address - VM_VARIABLES_BASE] = known;
        return true;
    }
    if (address >= VM_GAME_BASE && address < VM_GAME_BASE + VM_GAME_SIZE) {
        UseGame(state);
        state->game[address - VM_GAME_BASE] = byte;
        state->game[VM_GAME_SIZE + address - VM_GAME_BASE] = known;
        return true;
    }
    if (address >= VM_PROGRAM_BASE && address < VM_PROGRAM_BASE + machine->programSize) {
        for (uint32_t i = 0; i < state->overlayCount; i++) {
            if (state->overlayAddresses[i] == address) {
                state->overlayBytes[i * 2] = byte;
                state->overlayBytes[i * 2 + 1] = known;
                return true;
            }
        }
        if (state->overlayCount == state->overlayCapacity) {
            uint32_t capacity = state->overlayCapacity == 0 ? 64 : state->overlayCapacity * 2;
            uint32_t* addresses = realloc(state->overlayAddresses, capacity * sizeof(uint32_t));
            if (addresses != NULL)
                state->overlayAddresses = addresses;
            uint8_t* bytes = realloc(state->overlayBytes, capacity * 2);
            if (bytes != NULL)
                state->overlayBytes = bytes;
            if (addresses == NULL || bytes == NULL)
                return false;
            state->overlayCapacity = capacity;
        }
        state->overlayAddresses[state->overlayCount] = address;
        state->overlayBytes[state->overlayCount * 2] = byte;
        state->overlayBytes[state->overlayCount * 2 + 1] = known;
        state->overlayCount++;
        return true;
    }
    return false;
}

// A value of 1, 2 or 4 bytes (little-endian; the bytes and halfwords are signed). Unknown if the address isn't valid
static Value Load(const Machine* machine, const State* state, Value address, uint32_t size) {
    if (!address.known)
        return g_unknown;
    uint32_t value = 0;
    bool known = true;
    if (address.value >= VM_STACK_BASE && address.value <= VM_STACK_BASE + VM_STACK_SIZE - size &&
        address.value - VM_STACK_BASE >= state->stackLow) {
        // The same as ReadByte, from the stack
        const uint8_t* bytes = state->stack + (address.value - VM_STACK_BASE - state->stackBase) * 2;
        for (uint32_t i = 0; i < size; i++) {
            value |= (uint32_t)bytes[i * 2] << (i * 8);
            known &= bytes[i * 2 + 1] != 0;
        }
    } else if (address.value >= VM_VARIABLES_BASE && address.value - VM_VARIABLES_BASE <= machine->variableSize &&
        machine->variableSize - (address.value - VM_VARIABLES_BASE) >= size) {
        // And from the variables
        const uint8_t* bytes = state->variables + (address.value - VM_VARIABLES_BASE);
        for (uint32_t i = 0; i < size; i++) {
            value |= (uint32_t)bytes[i] << (i * 8);
            known &= bytes[machine->variableSize + i] != 0;
        }
    } else for (uint32_t i = 0; i < size; i++) {
        uint8_t byte;
        bool byteKnown;
        if (!ReadByte(machine, state, address.value + i, &byte, &byteKnown))
            return g_unknown;
        value |= (uint32_t)byte << (i * 8);
        known &= byteKnown;
    }
    if (size == 1)
        value = (uint32_t)(int32_t)(int8_t)value;
    else if (size == 2)
        value = (uint32_t)(int32_t)(int16_t)value;
    return (Value) { known ? value : 0, known };
}

// Notes a read of the variables since the last yield (VM_Environment.releaseWaits)
static void NoteRead(const Machine* machine, State* state, Value address, uint32_t size) {
    if (machine->timeline != NULL || machine->environment == NULL || !machine->environment->releaseWaits ||
        !address.known || address.value < VM_VARIABLES_BASE || address.value >= VM_VARIABLES_BASE + machine->variableSize)
        return;
    if (state->readCount < VM_WAIT_READS)
        state->reads[state->readCount] = (address.value - VM_VARIABLES_BASE) | size << 24;
    state->readCount++;
}

// Returns false if the address is known but not valid. A write to an unknown address is lost
static bool Store(const Machine* machine, State* state, Value address, uint32_t size, Value value) {
    if (!address.known)
        return true;
    if (address.value >= VM_STACK_BASE && address.value <= VM_STACK_BASE + VM_STACK_SIZE - size) {
        // The same as WriteByte, to the stack
        uint32_t offset = address.value - VM_STACK_BASE;
        if (offset < state->stackLow && !LowerStack(state, offset))
            return false;
        uint8_t* bytes = state->stack + (offset - state->stackBase) * 2;
        for (uint32_t i = 0; i < size; i++) {
            bytes[i * 2] = (uint8_t)(value.value >> (i * 8));
            bytes[i * 2 + 1] = value.known;
        }
        return true;
    }
    if (address.value >= VM_VARIABLES_BASE && address.value - VM_VARIABLES_BASE <= machine->variableSize &&
        machine->variableSize - (address.value - VM_VARIABLES_BASE) >= size) {
        // And to the variables
        uint8_t* bytes = state->variables + (address.value - VM_VARIABLES_BASE);
        for (uint32_t i = 0; i < size; i++) {
            bytes[i] = (uint8_t)(value.value >> (i * 8));
            bytes[machine->variableSize + i] = value.known;
        }
        return true;
    }
    for (uint32_t i = 0; i < size; i++) {
        if (!WriteByte(machine, state, address.value + i, (uint8_t)(value.value >> (i * 8)), value.known))
            return false;
    }
    return true;
}

static bool Push(const Machine* machine, State* state, Value value) {
    state->sp -= 4;
    return state->sp >= VM_STACK_BASE && Store(machine, state, (Value) { state->sp, true }, 4, value);
}

static Value Pop(const Machine* machine, State* state) {
    Value value = Load(machine, state, (Value) { state->sp, true }, 4);
    state->sp += 4;
    return value;
}

// Hashes: FNV-1a over the bytes of the small blocks; the big ones (the memory of a state, hashed at every branch) by
// words of 8 bytes in 4 independent lanes, each word mixed as the finalizer of MurmurHash3

#define HASH_PRIME 0x100000001B3ULL

static uint64_t HashWord(uint64_t hash, uint64_t word) {
    hash ^= word;
    hash ^= hash >> 33;
    hash *= 0xFF51AFD7ED558CCDULL;
    hash ^= hash >> 33;
    hash *= 0xC4CEB9FE1A85EC53ULL;
    return hash ^ (hash >> 33);
}

static uint64_t Hash(uint64_t hash, const void* data, size_t size) {
    const uint8_t* bytes = data;
    size_t i = 0;
    if (size >= 32) {
        uint64_t lanes[4] = { hash, hash ^ 0x9E3779B97F4A7C15ULL, hash ^ 0xC2B2AE3D27D4EB4FULL, hash ^ 0x165667B19E3779F9ULL };
        for (; i + 32 <= size; i += 32) {
            uint64_t words[4];
            memcpy(words, bytes + i, sizeof(words));
            for (int l = 0; l < 4; l++)
                lanes[l] = HashWord(lanes[l], words[l]);
        }
        for (int l = 0; l < 4; l++)
            hash = HashWord(hash, lanes[l]);
    }
    for (; i < size; i++)
        hash = (hash ^ bytes[i]) * HASH_PRIME;
    return hash;
}

// A block of memory mostly 0, by its words other than 0 and their offsets: the words of 0 don't change the hash
static uint64_t HashSparse(uint64_t hash, const uint8_t* bytes, size_t size) {
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        uint64_t words[4];
        memcpy(words, bytes + i, sizeof(words));
        if ((words[0] | words[1] | words[2] | words[3]) == 0)
            continue;
        for (int w = 0; w < 4; w++) {
            if (words[w] != 0)
                hash = HashWord(HashWord(hash, i + w * 8), words[w]);
        }
    }
    for (; i < size; i += 8) {
        uint64_t word = 0;
        memcpy(&word, bytes + i, size - i < 8 ? size - i : 8);
        if (word != 0)
            hash = HashWord(HashWord(hash, i), word);
    }
    return hash;
}

// A value by its fields: the padding of the structure is not set
static uint64_t HashValue(uint64_t hash, Value value) {
    hash = Hash(hash, &value.value, sizeof(value.value));
    return Hash(hash, &value.known, sizeof(value.known));
}

static uint64_t StateHash(Machine* machine, State* state) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    hash = Hash(hash, &state->pc, sizeof(state->pc));
    hash = Hash(hash, &state->sp, sizeof(state->sp));
    hash = Hash(hash, &state->fp, sizeof(state->fp));
    hash = HashValue(hash, state->result);
    if (machine->environment != NULL) {
        hash = Hash(hash, &state->yielded, sizeof(state->yielded));
        hash = Hash(hash, state->path, sizeof(state->path));
    }
    hash = HashSparse(hash, state->variables, machine->variableSize * 2);
    if (state->gameUsed)
        hash = HashSparse(hash, state->game, VM_GAME_SIZE * 2);
    if (state->sp >= VM_STACK_BASE && state->sp < VM_STACK_BASE + VM_STACK_SIZE) {
        uint32_t used = state->sp - VM_STACK_BASE;
        if (!LowerStack(state, used))   // The same hash whatever stackLow
            machine->ok = false;
        else
            hash = Hash(hash, state->stack + (used - state->stackBase) * 2, (VM_STACK_SIZE - used) * 2);
    }
    hash = Hash(hash, state->overlayAddresses, state->overlayCount * sizeof(uint32_t));
    return Hash(hash, state->overlayBytes, state->overlayCount * 2);
}

// Adds a hash to a set (open addressing, 0 is empty). Returns false if it was already there, or on memory error (ok)
static bool AddHash(Machine* machine, uint64_t** table, uint32_t* count, uint32_t* size, uint64_t hash) {
    hash |= 1;
    if ((*count + 1) * 2 > *size) {
        uint32_t newSize = *size == 0 ? 1024 : *size * 2;
        uint64_t* grown = calloc(newSize, sizeof(uint64_t));
        if (grown == NULL) {
            machine->ok = false;
            return false;
        }
        for (uint32_t i = 0; i < *size; i++) {
            if ((*table)[i] == 0)
                continue;
            uint32_t j = (uint32_t)((*table)[i] % newSize);
            while (grown[j] != 0)
                j = (j + 1) % newSize;
            grown[j] = (*table)[i];
        }
        free(*table);
        *table = grown;
        *size = newSize;
    }
    uint32_t i = (uint32_t)(hash % *size);
    while ((*table)[i] != 0) {
        if ((*table)[i] == hash)
            return false;
        i = (i + 1) % *size;
    }
    (*table)[i] = hash;
    (*count)++;
    return true;
}

// The calls

static uint64_t CallHash(const VM_Call* call) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    hash = Hash(hash, &call->address, sizeof(call->address));
    hash = Hash(hash, &call->count, sizeof(call->count));
    hash = Hash(hash, &call->known, sizeof(call->known));
    return Hash(hash, call->values, sizeof(call->values));
}

static bool SameCall(const VM_Call* a, const VM_Call* b) {
    return a->address == b->address && a->id == b->id && a->count == b->count && a->known == b->known &&
        memcmp(a->values, b->values, sizeof(a->values)) == 0;
}

static void AddCall(Machine* machine, const VM_Call* call) {
    VM_Result* result = machine->result;
    if ((result->callCount + 1) * 2 > machine->callTableSize) {
        uint32_t size = machine->callTableSize == 0 ? 1024 : machine->callTableSize * 2;
        uint32_t* table = calloc(size, sizeof(uint32_t));
        if (table == NULL) {
            machine->ok = false;
            return;
        }
        for (uint32_t c = 0; c < result->callCount; c++) {
            uint32_t i = (uint32_t)(CallHash(&result->calls[c]) % size);
            while (table[i] != 0)
                i = (i + 1) % size;
            table[i] = c + 1;
        }
        free(machine->callTable);
        machine->callTable = table;
        machine->callTableSize = size;
    }
    uint32_t i = (uint32_t)(CallHash(call) % machine->callTableSize);
    while (machine->callTable[i] != 0) {
        if (SameCall(&result->calls[machine->callTable[i] - 1], call))
            return;
        i = (i + 1) % machine->callTableSize;
    }
    if (result->callCount == machine->callCapacity) {
        uint32_t capacity = machine->callCapacity == 0 ? 256 : machine->callCapacity * 2;
        VM_Call* calls = realloc(result->calls, capacity * sizeof(VM_Call));
        if (calls == NULL) {
            machine->ok = false;
            return;
        }
        result->calls = calls;
        machine->callCapacity = capacity;
    }
    result->calls[result->callCount] = *call;
    machine->callTable[i] = ++result->callCount;
}

// A thread started by the program, once per function and arguments (with the variables of the first start)
static void AddRoot(Machine* machine, uint32_t function, uint32_t count, const Value* arguments, const uint8_t* variables) {
    if (machine->rootCount == VM_MAX_THREADS)
        return;
    Root root = { function, count < VM_MAX_ARGUMENTS ? count : VM_MAX_ARGUMENTS, { { 0 } }, NULL };
    for (uint32_t i = 0; i < root.count; i++)
        root.arguments[i] = arguments[i].known ? arguments[i] : g_unknown;
    uint64_t hash = Hash(0xCBF29CE484222325ULL, &root.function, sizeof(root.function));
    hash = Hash(hash, &root.count, sizeof(root.count));
    for (uint32_t i = 0; i < VM_MAX_ARGUMENTS; i++)
        hash = HashValue(hash, root.arguments[i]);
    for (uint32_t r = 0; r < machine->rootCount; r++) {
        if (machine->rootHashes[r] == hash)
            return;
    }
    root.variables = malloc(machine->variableSize * 2 + 1);
    if (root.variables == NULL) {
        machine->ok = false;
        return;
    }
    memcpy(root.variables, variables, machine->variableSize * 2);
    machine->rootHashes[machine->rootCount] = hash;
    machine->roots[machine->rootCount++] = root;
}

// Execution

static Value Known(uint32_t value) {
    return (Value) { value, true };
}

static float AsFloat(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The operators 0x88 to 0xA6 on known values. Returns false if the result isn't defined (a division by 0)
static bool Operate(uint8_t byte, uint32_t a, uint32_t b, uint32_t* result) {
    int32_t x = (int32_t)a, y = (int32_t)b;
    float f = AsFloat(a), g = AsFloat(b);
    switch (byte) {
    case 0x88: *result = x == y ? 0xFFFFFFFF : 0; return true;
    case 0x89: *result = x != y ? 0xFFFFFFFF : 0; return true;
    case 0x8A: *result = x < y ? 0xFFFFFFFF : 0; return true;
    case 0x8B: *result = x <= y ? 0xFFFFFFFF : 0; return true;
    case 0x8C: *result = x > y ? 0xFFFFFFFF : 0; return true;
    case 0x8D: *result = x >= y ? 0xFFFFFFFF : 0; return true;
    case 0x8F: *result = a & b; return true;
    case 0x90: *result = a | b; return true;
    case 0x91: *result = a ^ b; return true;
    case 0x92: *result = a + b; return true;
    case 0x93: *result = a - b; return true;
    case 0x94: *result = (uint32_t)((int64_t)x * y); return true;
    case 0x95: if (y == 0 || (x == INT32_MIN && y == -1)) return false; *result = (uint32_t)(x / y); return true;
    case 0x96: if (y == 0 || (x == INT32_MIN && y == -1)) return false; *result = (uint32_t)(x % y); return true;
    case 0x97: *result = (b & 31) == b ? a << b : 0; return true;
    case 0x98: *result = (uint32_t)(x >> (b < 31 ? b : 31)); return true;
    case 0x9F: *result = f < g ? 0xFFFFFFFF : 0; return true;
    case 0xA0: *result = f <= g ? 0xFFFFFFFF : 0; return true;
    case 0xA1: *result = f > g ? 0xFFFFFFFF : 0; return true;
    case 0xA2: *result = f >= g ? 0xFFFFFFFF : 0; return true;
    case 0xA3: *result = FloatBits(f + g); return true;
    case 0xA4: *result = FloatBits(f - g); return true;
    case 0xA5: *result = FloatBits(f * g); return true;
    case 0xA6: *result = FloatBits(f / g); return true;
    default: return false;
    }
}

static bool IsBinary(uint8_t byte) {
    return (byte >= 0x88 && byte <= 0x8D) || (byte >= 0x8F && byte <= 0x98) || (byte >= 0x9F && byte <= 0xA6);
}

// What the environment knows of a call: its result, and the memory written by the game
static Value Answer(Machine* machine, State* state, const VM_Call* call) {
    const VM_Environment* environment = machine->environment;
    if (environment == NULL || environment->call == NULL)
        return g_unknown;
    VM_Answer answer;
    memset(&answer, 0, sizeof(answer));
    VM_Memory memory = { machine, state };
    environment->call(environment->context, call, state->yielded, state->path, &memory, &answer);
    uint32_t size = answer.size < VM_ANSWER_BYTES ? answer.size : VM_ANSWER_BYTES;
    for (uint32_t i = 0; i < size; i++)
        WriteByte(machine, state, answer.address + i, answer.bytes[i], (answer.knownBytes >> i) & 1);
    return answer.known ? Known(answer.value) : g_unknown;
}

// The environment is told when a path settles: it yields for the first time, or ends its thread
static void Settle(Machine* machine, State* state) {
    const VM_Environment* environment = machine->environment;
    if (state->yielded)
        return;
    state->yielded = true;
    if (environment != NULL && environment->settled != NULL)
        environment->settled(environment->context, machine->thread, state->path);
}

static State* NewThread(Machine* machine, uint32_t function, uint32_t count, const Value* arguments, const uint8_t* variables);

// A call to the game in a run with time: the threads are started and ended, the environment answers the other calls
static Value TimedGameCall(Machine* machine, State* state, const VM_Call* call, const Value* arguments) {
    const BC_Program* program = machine->program;
    Timeline* timeline = machine->timeline;
    if (!timeline->started && timeline->current == 0 && call->address == timeline->start)
        timeline->started = true;
    if (call->id == VM_START_THREAD) {
        if (call->count < 2 || !arguments[0].known || !arguments[1].known || timeline->count == VM_TIMED_THREADS ||
            arguments[0].value < VM_PROGRAM_BASE + program->scn->header.codeStart ||
            arguments[0].value >= VM_PROGRAM_BASE + program->codeEnd)
            return g_unknown;
        uint32_t count = arguments[1].value < call->count - 2 ? arguments[1].value : call->count - 2;
        State* thread = NewThread(machine, arguments[0].value - VM_PROGRAM_BASE, count, arguments + 2, NULL);
        if (thread == NULL)
            return g_unknown;
        thread->wake = timeline->frame;
        timeline->threads[timeline->count++] = thread;
        return Known(timeline->count);  // The thread: its index + 1
    }
    if (call->id == VM_KILL_THREAD) {
        if (call->count < 1 || !arguments[0].known)
            return g_unknown;
        uint32_t index = arguments[0].value == 0 ? timeline->current : arguments[0].value - 1;
        if (index >= timeline->count)
            return Known(0);
        bool running = timeline->threads[index]->status != THREAD_ENDED;
        timeline->threads[index]->status = THREAD_ENDED;
        return Known(running);
    }
    const VM_TimedEnvironment* environment = timeline->environment;
    if (environment == NULL || environment->call == NULL)
        return g_unknown;
    VM_Answer answer;
    memset(&answer, 0, sizeof(answer));
    VM_Memory memory = { machine, state };
    environment->call(environment->context, timeline->frame, timeline->current, call, &memory, &answer);
    uint32_t size = answer.size < VM_ANSWER_BYTES ? answer.size : VM_ANSWER_BYTES;
    for (uint32_t i = 0; i < size; i++)
        WriteByte(machine, state, answer.address + i, answer.bytes[i], (answer.knownBytes >> i) & 1);
    return answer.known ? Known(answer.value) : g_unknown;
}

// A call to the game: its arguments are the values on the stack, dropped by the next instruction (sp += 4 * count).
// Returns its result
static Value GameCall(Machine* machine, State* state, const BC_Instruction* ins) {
    const BC_Program* program = machine->program;
    BC_Instruction next;
    uint32_t count = 0;
    if (BC_Decode(program->scn->data, program->codeEnd, state->pc + ins->size, &next) && next.kind == BC_STACK &&
        next.operand > 0 && next.operand % 4 == 0)
        count = (uint32_t)next.operand / 4;
    VM_Call call;
    memset(&call, 0, sizeof(call));
    call.address = state->pc;
    call.id = ins->operand;
    call.count = count < VM_MAX_ARGUMENTS ? count : VM_MAX_ARGUMENTS;
    Value arguments[VM_MAX_ARGUMENTS];
    for (uint32_t i = 0; i < call.count; i++) {
        arguments[i] = Load(machine, state, Known(state->sp + 4 * i), 4);
        if (arguments[i].known) {
            call.values[i] = arguments[i].value;
            call.known |= (uint16_t)(1u << i);
        }
    }
    if (machine->timeline != NULL)
        return TimedGameCall(machine, state, &call, arguments);
    const VM_Environment* environment = machine->environment;
    if (environment != NULL && environment->capture != NULL && environment->capture(environment->context, &call, state->path)) {
        machine->captured = CopyState(machine, state);
        machine->ok = machine->captured != NULL;
        machine->stop = true;
        return g_unknown;
    }
    if (call.known != 0)
        AddCall(machine, &call);
    // A thread: the function (an address of the code) and its arguments
    if (call.id == VM_START_THREAD && call.count >= 2 && arguments[0].known && arguments[1].known &&
        arguments[0].value >= VM_PROGRAM_BASE + program->scn->header.codeStart &&
        arguments[0].value < VM_PROGRAM_BASE + program->codeEnd) {
        uint32_t threadArguments = arguments[1].value < call.count - 2 ? arguments[1].value : call.count - 2;
        AddRoot(machine, arguments[0].value - VM_PROGRAM_BASE, threadArguments, arguments + 2, state->variables);
    }
    return Answer(machine, state, &call);
}

// The path can't get to what the environment looks for from an offset of the code: nor after the returns of its stack
static bool Hopeless(const Machine* machine, const State* state, const uint32_t* distances, uint32_t pc) {
    uint32_t codeEnd = machine->program->codeEnd;
    if (pc >= codeEnd || distances[pc] < VM_DISTANCE_RETURN)
        return false;
    uint32_t fp = state->fp;
    for (uint32_t depth = 0; fp != 0 && depth < 256; depth++) {
        Value saved = Load(machine, state, Known(fp), 4);
        Value back = Load(machine, state, Known(fp + 4), 4);
        if (!saved.known || !back.known)
            return false;
        if (back.value == 0 && saved.value == 0)
            return true;    // The end of the thread
        if (back.value >= codeEnd || distances[back.value] < VM_DISTANCE_RETURN)
            return false;
        fp = saved.value;
    }
    return fp == 0;
}

// Runs a path until its thread ends, it stops or the limits are reached
static void RunPath(Machine* machine, State* state) {
    const BC_Program* program = machine->program;
    VM_Result* result = machine->result;
    uint64_t maxSteps = machine->timeline != NULL ? VM_TIMED_STEPS : machine->maxSteps;
    for (;;) {
        if (machine->steps >= maxSteps || !machine->ok || machine->stop)
            return;
        machine->steps++;
        result->steps++;
        BC_Instruction ins;
        if (state->pc < program->scn->header.codeStart ||
            !BC_Decode(program->scn->data, program->codeEnd, state->pc, &ins)) {
            result->errors++;
            return;
        }
        uint32_t next = state->pc + ins.size;
        bool ok = true;
        switch (ins.kind) {
        case BC_PUSH: {
            Value value = g_unknown;
            if (ins.area == BC_AREA_CONSTANT)
                value = Known((uint32_t)ins.operand);
            else if (ins.area == BC_AREA_PROGRAM)
                value = Known(VM_PROGRAM_BASE + (uint32_t)ins.operand);
            else if (ins.area == BC_AREA_VARIABLE || ins.area == BC_AREA_LOCAL) {
                Value address = Known((ins.area == BC_AREA_VARIABLE ? VM_VARIABLES_BASE : state->fp) + (uint32_t)ins.operand);
                if (ins.width != 0)
                    NoteRead(machine, state, address, ins.width);
                value = ins.width == 0 ? address : Load(machine, state, address, ins.width);
            }
            ok = Push(machine, state, value);
            break;
        }
        case BC_OPERATOR: {
            uint8_t byte = ins.byte;
            if (byte == 0x80) {
                state->result = Pop(machine, state);
            } else if (byte == 0x81) {
                ok = Push(machine, state, state->result);
            } else if (byte >= 0x82 && byte <= 0x84) {
                Value address = Pop(machine, state);
                NoteRead(machine, state, address, byte == 0x82 ? 1 : byte == 0x83 ? 2 : 4);
                ok = Push(machine, state, Load(machine, state, address, byte == 0x82 ? 1 : byte == 0x83 ? 2 : 4));
            } else if (byte >= 0x85 && byte <= 0x87) {
                Value value = Pop(machine, state);
                Value address = Pop(machine, state);
                uint32_t size = byte == 0x85 ? 1 : byte == 0x86 ? 2 : 4;
                ok = Store(machine, state, address, size, value);
                if (ok && size < 4 && value.known)
                    value.value = size == 1 ? (uint32_t)(int32_t)(int8_t)value.value : (uint32_t)(int32_t)(int16_t)value.value;
                ok = ok && Push(machine, state, value);
            } else if (byte == 0x8E || byte == 0x9D || byte == 0x9E) {
                Value value = Pop(machine, state);
                float f = AsFloat(value.value);
                if (byte == 0x9E && value.known && (!isfinite(f) || f >= 2147483648.0f || f < -2147483648.0f))
                    value = g_unknown;
                else if (value.known)
                    value.value = byte == 0x8E ? ~value.value : byte == 0x9D ? FloatBits((float)(int32_t)value.value) :
                        (uint32_t)(int32_t)f;
                ok = Push(machine, state, value);
            } else if (IsBinary(byte)) {
                Value b = Pop(machine, state);
                Value a = Pop(machine, state);
                Value value = g_unknown;
                if (a.known && b.known && Operate(byte, a.value, b.value, &value.value))
                    value.known = true;
                ok = Push(machine, state, value);
            }
            break;
        }
        case BC_RET: {
            state->sp = state->fp;
            Value fp = Pop(machine, state);
            Value pc = Pop(machine, state);
            if (fp.known && fp.value == 0) {
                Settle(machine, state);
                state->status = THREAD_ENDED;
                return;     // The end of the thread
            }
            if (!fp.known || !pc.known) {
                result->errors++;
                return;
            }
            state->fp = fp.value;
            next = pc.value;
            break;
        }
        case BC_CALL:
        case BC_CALL_POP: {
            uint32_t target = ins.target;
            if (ins.kind == BC_CALL_POP) {
                Value address = Pop(machine, state);
                if (!address.known || address.value < VM_PROGRAM_BASE) {
                    result->errors++;
                    return;
                }
                target = address.value - VM_PROGRAM_BASE;
            }
            ok = Push(machine, state, Known(next)) && Push(machine, state, Known(state->fp));
            state->fp = state->sp;
            next = target;
            break;
        }
        case BC_STACK:
            state->sp += (uint32_t)ins.operand;
            // The room for the local variables: unknown values
            for (int32_t i = 0; i < -ins.operand && ok; i++)
                ok = WriteByte(machine, state, state->sp + (uint32_t)i, 0, false);
            break;
        case BC_JUMP:
            next = ins.target;
            break;
        case BC_BRANCH:
            next = ins.target;
            break;
        case BC_BRANCH_ZERO:
            if (state->result.known) {
                if (state->result.value == 0)
                    next = ins.target;
            } else if (machine->timeline != NULL) {
                next = ins.target;  // As 0
            } else {
                // Both ways, if this state wasn't seen at a branch
                if (!AddHash(machine, &machine->seen, &machine->seenCount, &machine->seenSize, StateHash(machine, state)))
                    return;
                // The way followed first: the one closer to what the environment looks for
                const uint32_t* distances = machine->environment != NULL ? machine->environment->distances : NULL;
                bool zeroFirst = distances != NULL && ins.target < program->codeEnd && next < program->codeEnd &&
                    distances[ins.target] < distances[next];
                bool zeroHopeless = distances != NULL && Hopeless(machine, state, distances, ins.target);
                bool otherHopeless = distances != NULL && Hopeless(machine, state, distances, next);
                if (zeroHopeless && otherHopeless)
                    return;
                if (zeroHopeless || otherHopeless) {
                    // Only one way
                    state->result = Known(zeroHopeless ? 1 : 0);
                    if (!zeroHopeless)
                        next = ins.target;
                    break;
                }
                if (machine->workCount < VM_MAX_STATES) {
                    State* other = CopyState(machine, state);
                    if (other == NULL) {
                        machine->ok = false;
                        return;
                    }
                    other->pc = zeroFirst ? next : ins.target;
                    other->result = Known(zeroFirst ? 1 : 0);
                    machine->work[machine->workCount++] = other;
                    result->states++;
                    if (zeroFirst) {
                        state->result = Known(0);
                        next = ins.target;
                        break;
                    }
                }
                state->result = Known(1);
            }
            break;
        case BC_GAME:
            state->result = GameCall(machine, state, &ins);
            if (state->status == THREAD_ENDED)
                return;     // KillThread ended it
            break;
        case BC_DATA_SERVICE:
        case BC_SERVICE2C:
            // They yield: the next frames
            Settle(machine, state);
            state->result = g_unknown;
            if (machine->timeline == NULL && machine->environment != NULL && machine->environment->maxWaits != 0 &&
                ++state->waits > machine->environment->maxWaits)
                return;
            if (machine->timeline == NULL && machine->environment != NULL && machine->environment->releaseWaits) {
                // A wait that changes nothing: the other threads run meanwhile and change what it waits for, the
                // variables that it has read since its last wait (all of them if it has read too many)
                uint64_t hash = StateHash(machine, state);
                if (hash == state->waitHash && state->readCount > VM_WAIT_READS)
                    memset(state->variables + machine->variableSize, 0, machine->variableSize);
                else if (hash == state->waitHash) {
                    for (uint32_t i = 0; i < state->readCount; i++) {
                        uint32_t offset = state->reads[i] & 0xFFFFFF, size = state->reads[i] >> 24;
                        for (uint32_t b = 0; b < size && offset + b < machine->variableSize; b++)
                            state->variables[machine->variableSize + offset + b] = 0;
                    }
                }
                state->waitHash = hash;
                state->readCount = 0;
            }
            if (machine->timeline != NULL) {
                // The service 0 waits the frames of its argument, the first value of the stack
                uint32_t frames = 1;
                if (ins.kind == BC_SERVICE2C && ins.operand == 0) {
                    Value wait = Load(machine, state, Known(state->sp), 4);
                    if (wait.known && wait.value >= 1 && wait.value <= 100000)
                        frames = wait.value;
                }
                state->wake = machine->timeline->frame + frames;
                state->status = THREAD_WAITING;
                state->pc = next;
                return;
            }
            break;
        case BC_NATIVE:
        case BC_NATIVE4:
        case BC_SERVICE30:
            state->result = g_unknown;
            break;
        default:
            break;
        }
        if (!ok || state->sp < VM_STACK_BASE || state->sp > VM_STACK_BASE + VM_STACK_SIZE) {
            result->errors++;
            return;
        }
        state->pc = next;
    }
}

// A thread: its function called with its arguments. The variables are copied, or shared with the timeline (NULL).
// Returns NULL on memory error (ok false) or if the arguments don't fit
static State* NewThread(Machine* machine, uint32_t function, uint32_t count, const Value* arguments, const uint8_t* variables) {
    State* state = NewState(machine);
    if (state == NULL) {
        machine->ok = false;
        return NULL;
    }
    if (variables != NULL)
        memcpy(state->variables, variables, machine->variableSize * 2);
    else {
        free(state->variables);
        state->variables = machine->timeline->variables;
        free(state->game);
        state->game = machine->timeline->game;
        state->gameUsed = true;
    }
    state->sp = VM_STACK_BASE + VM_STACK_SIZE;
    state->fp = 0;
    bool ok = true;
    for (uint32_t i = count; i-- > 0 && ok;)
        ok = Push(machine, state, arguments[i]);
    // As a call: the return address and fp (0, which ends the thread at the return)
    ok = ok && Push(machine, state, Known(0)) && Push(machine, state, Known(0));
    state->fp = state->sp;
    state->pc = function;
    if (!ok) {
        if (variables == NULL) {
            state->variables = NULL;
            state->game = NULL;
        }
        FreeState(state);
        return NULL;
    }
    return state;
}

// Runs a thread: its function called with its arguments, and every path of its branches
static void RunThread(Machine* machine, const Root* root) {
    State* state = NewThread(machine, root->function, root->count, root->arguments, root->variables);
    if (state == NULL)
        return;
    machine->steps = 0;
    machine->seenCount = 0;
    if (machine->seen != NULL)
        memset(machine->seen, 0, machine->seenSize * sizeof(uint64_t));
    uint32_t states = 0;
    machine->work[machine->workCount++] = state;
    while (machine->workCount > 0 && machine->ok) {
        State* path = machine->work[--machine->workCount];
        if (states++ < VM_MAX_STATES && machine->steps < machine->maxSteps)
            RunPath(machine, path);
        RecycleState(machine, path);
    }
    if (states > VM_MAX_STATES || machine->steps >= machine->maxSteps)
        machine->result->truncated++;
}

static int CompareCalls(const void* a, const void* b) {
    const VM_Call* x = a;
    const VM_Call* y = b;
    return x->address < y->address ? -1 : x->address > y->address;
}

bool VM_Run(const BC_Program* program, VM_Result* result) {
    return VM_RunEnvironment(program, NULL, result);
}

// Runs the program from a function (its entry point, or another one called without arguments). With captured, stops at
// the first call captured by the environment and returns the state of its path (NULL if none)
static bool Run(const BC_Program* program, uint32_t function, const VM_Environment* environment, VM_Result* result,
    State** captured) {
    memset(result, 0, sizeof(*result));
    const SCN_Header* header = &program->scn->header;
    Machine machine;
    memset(&machine, 0, sizeof(machine));
    machine.program = program;
    machine.environment = environment;
    machine.programSize = header->end < header->size ? header->end : header->size;
    machine.variableSize = header->variableSize;
    machine.result = result;
    machine.ok = true;
    uint64_t maxTotal = environment != NULL && environment->maxSteps != 0 ? environment->maxSteps : VM_MAX_TOTAL_STEPS;
    machine.maxSteps = environment != NULL && environment->maxSteps != 0 && environment->maxSteps < VM_MAX_STEPS ?
        environment->maxSteps : VM_MAX_STEPS;
    machine.roots = calloc(VM_MAX_THREADS, sizeof(Root));
    machine.rootHashes = calloc(VM_MAX_THREADS, sizeof(uint64_t));
    machine.work = calloc(VM_MAX_STATES + 1, sizeof(State*));
    uint8_t* variables = calloc(machine.variableSize * 2 + 1, 1);   // Unknown at the start
    if (machine.roots == NULL || machine.rootHashes == NULL || machine.work == NULL || variables == NULL)
        machine.ok = false;
    else {
        if (environment != NULL && environment->initialize != NULL) {
            State* first = NewState(&machine);
            if (first == NULL)
                machine.ok = false;
            else {
                free(first->variables);
                first->variables = variables;
                VM_Memory memory = { &machine, first };
                environment->initialize(environment->context, &memory);
                first->variables = NULL;
                FreeState(first);
            }
        }
        AddRoot(&machine, function, 0, NULL, variables);
    }
    for (uint32_t r = 0; r < machine.rootCount && machine.ok && !machine.stop && result->steps < maxTotal; r++) {
        machine.thread = r;
        RunThread(&machine, &machine.roots[r]);
        result->threads++;
    }
    result->skipped = machine.rootCount - result->threads;
    for (uint32_t r = 0; r < machine.rootCount; r++)
        free(machine.roots[r].variables);
    if (result->callCount > 0)
        qsort(result->calls, result->callCount, sizeof(VM_Call), CompareCalls);
    while (machine.workCount > 0)
        FreeState(machine.work[--machine.workCount]);
    FreeSpares(&machine);
    free(machine.roots);
    free(machine.rootHashes);
    free(machine.work);
    free(machine.seen);
    free(machine.callTable);
    free(variables);
    if (captured != NULL)
        *captured = machine.captured;
    else
        FreeState(machine.captured);
    if (!machine.ok) {
        VM_Free(result);
        return false;
    }
    return true;
}

bool VM_RunEnvironment(const BC_Program* program, const VM_Environment* environment, VM_Result* result) {
    return Run(program, program->scn->header.entry, environment, result, NULL);
}

bool VM_RunFrom(const BC_Program* program, uint32_t function, const VM_Environment* environment, VM_Result* result) {
    return Run(program, function, environment, result, NULL);
}

VM_Snapshot* VM_Capture(const BC_Program* program, const VM_Environment* environment) {
    return VM_CaptureFrom(program, program->scn->header.entry, environment);
}

VM_Snapshot* VM_CaptureFrom(const BC_Program* program, uint32_t function, const VM_Environment* environment) {
    VM_Result result;
    State* captured = NULL;
    bool ok = Run(program, function, environment, &result, &captured);
    VM_Free(&result);
    if (!ok || captured == NULL) {
        FreeState(captured);
        return NULL;
    }
    VM_Snapshot* snapshot = malloc(sizeof(VM_Snapshot));
    if (snapshot == NULL) {
        FreeState(captured);
        return NULL;
    }
    snapshot->state = captured;
    snapshot->address = captured->pc;
    snapshot->variableSize = program->scn->header.variableSize;
    return snapshot;
}

void VM_FreeSnapshot(VM_Snapshot* snapshot) {
    if (snapshot == NULL)
        return;
    FreeState(snapshot->state);
    free(snapshot);
}

uint32_t VM_SnapshotAddress(const VM_Snapshot* snapshot) {
    return snapshot->address;
}

bool VM_ReadSnapshotGame(const VM_Snapshot* snapshot, uint32_t address, uint32_t size, uint8_t* bytes, uint32_t* known) {
    *known = 0;
    if (address < VM_GAME_BASE || size > VM_GAME_SIZE || address - VM_GAME_BASE > VM_GAME_SIZE - size)
        return false;
    const uint8_t* game = snapshot->state->game;
    if (!snapshot->state->gameUsed) {
        memset(bytes, 0, size);
        return true;
    }
    for (uint32_t i = 0; i < size; i++) {
        bytes[i] = game[address - VM_GAME_BASE + i];
        if (game[VM_GAME_SIZE + address - VM_GAME_BASE + i] != 0 && i < 32)
            *known |= 1u << i;
    }
    return true;
}

// Runs a thread with time until it waits or ends. Before the call of the snapshot, the waits of its thread don't wait
static void RunTimedThread(Machine* machine, uint32_t index, VM_TimedResult* result) {
    Timeline* timeline = machine->timeline;
    State* state = timeline->threads[index];
    uint32_t preWaits = 0;
    timeline->current = index;
    for (;;) {
        state->status = THREAD_RUNNING;
        machine->steps = 0;
        RunPath(machine, state);
        if (state->status == THREAD_RUNNING) {
            state->status = THREAD_ENDED;   // By a limit or an error
            result->stopped++;
        }
        if (index != 0 || timeline->started || state->status != THREAD_WAITING || preWaits++ >= VM_TIMED_PRE_WAITS)
            return;
    }
}

bool VM_RunTimed(const BC_Program* program, const VM_Snapshot* snapshot, bool rewind, uint32_t frames,
    const VM_TimedEnvironment* environment, VM_TimedResult* result) {
    memset(result, 0, sizeof(*result));
    const SCN_Header* header = &program->scn->header;
    VM_Result calls;
    memset(&calls, 0, sizeof(calls));
    Timeline timeline;
    memset(&timeline, 0, sizeof(timeline));
    timeline.environment = environment;
    timeline.start = snapshot->address;
    Machine machine;
    memset(&machine, 0, sizeof(machine));
    machine.program = program;
    machine.programSize = header->end < header->size ? header->end : header->size;
    machine.variableSize = header->variableSize;
    machine.result = &calls;
    machine.ok = true;
    machine.timeline = &timeline;
    timeline.threads = calloc(VM_TIMED_THREADS, sizeof(State*));
    timeline.variables = malloc(machine.variableSize * 2 + 1);
    timeline.game = malloc(VM_GAME_SIZE * 2);
    State* first = timeline.threads != NULL && timeline.variables != NULL && timeline.game != NULL ?
        CopyState(&machine, snapshot->state) : NULL;
    if (first == NULL) {
        free(timeline.threads);
        free(timeline.variables);
        free(timeline.game);
        return false;
    }
    memcpy(timeline.variables, first->variables, machine.variableSize * 2);
    free(first->variables);
    first->variables = timeline.variables;
    UseGame(first);
    memcpy(timeline.game, first->game, VM_GAME_SIZE * 2);
    free(first->game);
    first->game = timeline.game;
    first->status = THREAD_RUNNING;
    first->wake = 0;
    // From the start of the function of the call, called from the same place: sp and fp as after the call
    int function = rewind ? BC_FunctionAt(program, snapshot->address) : -1;
    if (function >= 0) {
        first->pc = program->functions[function];
        first->sp = first->fp;
    }
    timeline.threads[timeline.count++] = first;
    for (uint32_t frame = 0; frame < frames && machine.ok; frame++) {
        timeline.frame = frame;
        bool running = false;
        for (uint32_t t = 0; t < timeline.count && machine.ok; t++) {
            State* thread = timeline.threads[t];
            if (thread->status != THREAD_ENDED && thread->wake <= frame)
                RunTimedThread(&machine, t, result);
            if (t == 0 && !timeline.started)
                break;      // The thread of the snapshot ended before its call
            running |= thread->status != THREAD_ENDED;
        }
        if (!timeline.started)
            break;
        result->frames = frame + 1;
        if (!running)
            break;
    }
    result->started = timeline.started;
    result->threads = timeline.count;
    result->steps = calls.steps;
    for (uint32_t t = 0; t < timeline.count; t++) {
        timeline.threads[t]->variables = NULL;  // Shared
        timeline.threads[t]->game = NULL;
        FreeState(timeline.threads[t]);
    }
    free(timeline.threads);
    free(timeline.variables);
    free(timeline.game);
    return machine.ok;
}

bool VM_ReadMemory(const VM_Memory* memory, uint32_t address, uint32_t size, uint8_t* bytes, uint32_t* known) {
    *known = 0;
    for (uint32_t i = 0; i < size; i++) {
        bool byteKnown;
        if (!ReadByte(memory->machine, memory->state, address + i, &bytes[i], &byteKnown))
            return false;
        if (byteKnown && i < 32)
            *known |= 1u << i;
    }
    return true;
}

void VM_Free(VM_Result* result) {
    free(result->calls);
    memset(result, 0, sizeof(*result));
}

int64_t VM_DataOffset(const BC_Program* program, uint32_t value) {
    const SCN_Header* header = &program->scn->header;
    if (value < VM_PROGRAM_BASE + header->dataStart || value >= VM_PROGRAM_BASE + header->end)
        return -1;
    return (int64_t)(value - VM_PROGRAM_BASE - header->dataStart);
}

bool VM_WriteMemory(VM_Memory* memory, uint32_t address, uint32_t size, const uint8_t* bytes, uint32_t known) {
    for (uint32_t i = 0; i < size && i < 32; i++) {
        if (!WriteByte(memory->machine, memory->state, address + i, bytes[i], (known >> i) & 1))
            return false;
    }
    return true;
}

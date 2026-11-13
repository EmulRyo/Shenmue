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
    uint8_t*    stack;      // VM_STACK_SIZE bytes, then their known flags
    uint32_t    overlayCount;
    uint32_t    overlayCapacity;
    uint32_t*   overlayAddresses;
    uint8_t*    overlayBytes;   // Value, known
    bool        yielded;        // The thread has yielded on this path
    uint8_t     path[VM_PATH_SIZE]; // Of the environment
} State;

// A thread to run: its function and arguments, and the variables when it was started
typedef struct {
    uint32_t    function;
    uint32_t    count;
    Value       arguments[VM_MAX_ARGUMENTS];
    uint8_t*    variables;
} Root;

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
} Machine;

// Memory of the states

static void FreeState(State* state) {
    if (state == NULL)
        return;
    free(state->variables);
    free(state->stack);
    free(state->overlayAddresses);
    free(state->overlayBytes);
    free(state);
}

static State* NewState(const Machine* machine) {
    State* state = calloc(1, sizeof(State));
    if (state == NULL)
        return NULL;
    state->variables = calloc(machine->variableSize * 2 + 1, 1);
    state->stack = calloc(VM_STACK_SIZE * 2, 1);
    if (state->variables == NULL || state->stack == NULL) {
        FreeState(state);
        return NULL;
    }
    return state;
}

static State* CopyState(const Machine* machine, const State* source) {
    State* state = NewState(machine);
    if (state == NULL)
        return NULL;
    state->pc = source->pc;
    state->sp = source->sp;
    state->fp = source->fp;
    state->result = source->result;
    state->yielded = source->yielded;
    memcpy(state->path, source->path, sizeof(state->path));
    memcpy(state->variables, source->variables, machine->variableSize * 2);
    // The stack above sp: below it, the values are not used before they are written
    uint32_t used = source->sp >= VM_STACK_BASE && source->sp <= VM_STACK_BASE + VM_STACK_SIZE ? source->sp - VM_STACK_BASE : 0;
    memcpy(state->stack + used, source->stack + used, VM_STACK_SIZE - used);
    memcpy(state->stack + VM_STACK_SIZE + used, source->stack + VM_STACK_SIZE + used, VM_STACK_SIZE - used);
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
        *byte = state->stack[address - VM_STACK_BASE];
        *known = state->stack[VM_STACK_SIZE + address - VM_STACK_BASE] != 0;
        return true;
    }
    if (address >= VM_VARIABLES_BASE && address < VM_VARIABLES_BASE + machine->variableSize) {
        *byte = state->variables[address - VM_VARIABLES_BASE];
        *known = state->variables[machine->variableSize + address - VM_VARIABLES_BASE] != 0;
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

static bool WriteByte(const Machine* machine, State* state, uint32_t address, uint8_t byte, bool known) {
    if (address >= VM_STACK_BASE && address < VM_STACK_BASE + VM_STACK_SIZE) {
        state->stack[address - VM_STACK_BASE] = byte;
        state->stack[VM_STACK_SIZE + address - VM_STACK_BASE] = known;
        return true;
    }
    if (address >= VM_VARIABLES_BASE && address < VM_VARIABLES_BASE + machine->variableSize) {
        state->variables[address - VM_VARIABLES_BASE] = byte;
        state->variables[machine->variableSize + address - VM_VARIABLES_BASE] = known;
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
    for (uint32_t i = 0; i < size; i++) {
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

// Returns false if the address is known but not valid. A write to an unknown address is lost
static bool Store(const Machine* machine, State* state, Value address, uint32_t size, Value value) {
    if (!address.known)
        return true;
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

// Hashes (FNV-1a)

static uint64_t Hash(uint64_t hash, const void* data, size_t size) {
    const uint8_t* bytes = data;
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 0x100000001B3ULL;
    return hash;
}

static uint64_t StateHash(const Machine* machine, const State* state) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    hash = Hash(hash, &state->pc, sizeof(state->pc));
    hash = Hash(hash, &state->sp, sizeof(state->sp));
    hash = Hash(hash, &state->fp, sizeof(state->fp));
    hash = Hash(hash, &state->result, sizeof(state->result));
    if (machine->environment != NULL) {
        hash = Hash(hash, &state->yielded, sizeof(state->yielded));
        hash = Hash(hash, state->path, sizeof(state->path));
    }
    hash = Hash(hash, state->variables, machine->variableSize * 2);
    if (state->sp >= VM_STACK_BASE && state->sp < VM_STACK_BASE + VM_STACK_SIZE) {
        uint32_t used = state->sp - VM_STACK_BASE;
        hash = Hash(hash, state->stack + used, VM_STACK_SIZE - used);
        hash = Hash(hash, state->stack + VM_STACK_SIZE + used, VM_STACK_SIZE - used);
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
    hash = Hash(hash, root.arguments, sizeof(root.arguments));
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
    environment->call(environment->context, call, state->yielded, state->path, &answer);
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

// Runs a path until its thread ends, it stops or the limits are reached
static void RunPath(Machine* machine, State* state) {
    const BC_Program* program = machine->program;
    VM_Result* result = machine->result;
    for (;;) {
        if (machine->steps >= VM_MAX_STEPS || !machine->ok)
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
            } else {
                // Both ways, if this state wasn't seen at a branch
                if (!AddHash(machine, &machine->seen, &machine->seenCount, &machine->seenSize, StateHash(machine, state)))
                    return;
                if (machine->workCount < VM_MAX_STATES) {
                    State* other = CopyState(machine, state);
                    if (other == NULL) {
                        machine->ok = false;
                        return;
                    }
                    other->pc = ins.target;
                    other->result = Known(0);
                    machine->work[machine->workCount++] = other;
                    result->states++;
                }
                state->result = Known(1);
            }
            break;
        case BC_GAME:
            state->result = GameCall(machine, state, &ins);
            break;
        case BC_DATA_SERVICE:
        case BC_SERVICE2C:
            // They yield: the next frames
            Settle(machine, state);
            state->result = g_unknown;
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

// Runs a thread: its function called with its arguments, and every path of its branches
static void RunThread(Machine* machine, const Root* root) {
    State* state = NewState(machine);
    if (state == NULL) {
        machine->ok = false;
        return;
    }
    memcpy(state->variables, root->variables, machine->variableSize * 2);
    state->sp = VM_STACK_BASE + VM_STACK_SIZE;
    state->fp = 0;
    bool ok = true;
    for (uint32_t i = root->count; i-- > 0 && ok;)
        ok = Push(machine, state, root->arguments[i]);
    // As a call: the return address and fp (0, which ends the thread at the return)
    ok = ok && Push(machine, state, Known(0)) && Push(machine, state, Known(0));
    state->fp = state->sp;
    state->pc = root->function;
    machine->steps = 0;
    machine->seenCount = 0;
    if (machine->seen != NULL)
        memset(machine->seen, 0, machine->seenSize * sizeof(uint64_t));
    uint32_t states = 0;
    machine->work[machine->workCount++] = state;
    while (machine->workCount > 0 && machine->ok) {
        State* path = machine->work[--machine->workCount];
        if (states++ < VM_MAX_STATES && machine->steps < VM_MAX_STEPS && ok)
            RunPath(machine, path);
        FreeState(path);
    }
    if (states > VM_MAX_STATES || machine->steps >= VM_MAX_STEPS)
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

bool VM_RunEnvironment(const BC_Program* program, const VM_Environment* environment, VM_Result* result) {
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
    machine.roots = calloc(VM_MAX_THREADS, sizeof(Root));
    machine.rootHashes = calloc(VM_MAX_THREADS, sizeof(uint64_t));
    machine.work = calloc(VM_MAX_STATES + 1, sizeof(State*));
    uint8_t* variables = calloc(machine.variableSize * 2 + 1, 1);   // Unknown at the start
    if (machine.roots == NULL || machine.rootHashes == NULL || machine.work == NULL || variables == NULL)
        machine.ok = false;
    else
        AddRoot(&machine, header->entry, 0, NULL, variables);
    for (uint32_t r = 0; r < machine.rootCount && machine.ok && result->steps < VM_MAX_TOTAL_STEPS; r++) {
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
    free(machine.roots);
    free(machine.rootHashes);
    free(machine.work);
    free(machine.seen);
    free(machine.callTable);
    free(variables);
    if (!machine.ok) {
        VM_Free(result);
        return false;
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

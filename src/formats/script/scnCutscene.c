#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "formats/script/scnCutscene.h"

#define SCNCUTSCENE_MAX_STARTS  16      // Functions from which the emulation looks for the call that plays the slot

typedef struct {
    const BC_Program*   program;
    uint32_t            slot;
    const char*         file;
    uint32_t            frames;
    SCNCutscene*        cutscene;
    uint32_t            nodeCapacity;
    uint32_t            soundCapacity;
    bool                ok;
} Context;

// The state of a path for the capture: the AUTH file loaded in the slot
enum {
    PATH_NOT_LOADED,
    PATH_FILE,          // The file
    PATH_OTHER,         // Another file, or a file not known
};

static bool Known(const VM_Call* call, uint32_t argument) {
    return argument < call->count && (call->known & (1u << argument));
}

// Compares a string of the data of the program with a name, ignoring the case
static bool IsProgramString(const BC_Program* program, uint32_t value, const char* name) {
    int64_t offset = VM_DataOffset(program, value);
    if (offset < 0)
        return false;
    const SCN_Header* header = &program->scn->header;
    const uint8_t* data = program->scn->data;
    uint32_t end = header->end < header->size ? header->end : header->size;
    uint32_t i = header->dataStart + (uint32_t)offset;
    for (; i < end && *name != '\0'; i++, name++) {
        if (toupper(data[i]) != toupper((unsigned char)*name))
            return false;
    }
    return i < end && data[i] == '\0' && *name == '\0';
}

static bool OnCapture(void* context, const VM_Call* call, const uint8_t* path) {
    const Context* c = context;
    return call->id == SCNCUTSCENE_PLAY && Known(call, 0) && call->values[0] == c->slot && path[0] != PATH_OTHER;
}

static void OnCall(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Answer* answer) {
    (void)yielded;
    (void)answer;
    const Context* c = context;
    if (c->file != NULL && call->id == SCNCUTSCENE_SEQUENCE && Known(call, 0) && Known(call, 1) && call->values[0] == 0 &&
        call->values[1] == c->slot)
        path[0] = Known(call, 2) && IsProgramString(c->program, call->values[2], c->file) ? PATH_FILE : PATH_OTHER;
}

static void AddNode(Context* c, const SCNCutscene_Node* node) {
    SCNCutscene* cutscene = c->cutscene;
    if (cutscene->nodeCount == c->nodeCapacity) {
        uint32_t capacity = c->nodeCapacity == 0 ? 64 : c->nodeCapacity * 2;
        SCNCutscene_Node* nodes = realloc(cutscene->nodes, capacity * sizeof(SCNCutscene_Node));
        if (nodes == NULL) {
            c->ok = false;
            return;
        }
        cutscene->nodes = nodes;
        c->nodeCapacity = capacity;
    }
    cutscene->nodes[cutscene->nodeCount++] = *node;
}

static void AddSound(Context* c, const SCNCutscene_Sound* sound) {
    SCNCutscene* cutscene = c->cutscene;
    if (cutscene->soundCount == c->soundCapacity) {
        uint32_t capacity = c->soundCapacity == 0 ? 16 : c->soundCapacity * 2;
        SCNCutscene_Sound* sounds = realloc(cutscene->sounds, capacity * sizeof(SCNCutscene_Sound));
        if (sounds == NULL) {
            c->ok = false;
            return;
        }
        cutscene->sounds = sounds;
        c->soundCapacity = capacity;
    }
    cutscene->sounds[cutscene->soundCount++] = *sound;
}

// The value of a field of a node set by the calls so far: bit i of the result, values[i] is known (set by a call)
static uint8_t CurrentValue(const SCNCutscene* cutscene, uint32_t character, uint16_t node, uint8_t field, uint32_t* values) {
    uint8_t known = 0;
    for (uint32_t i = 0; i < cutscene->nodeCount; i++) {
        const SCNCutscene_Node* n = &cutscene->nodes[i];
        if (n->character != character || n->node != node || n->field != field)
            continue;
        for (int v = 0; v < 3; v++) {
            if (!(n->known & (1 << v)))
                continue;
            if (!n->add) {
                values[v] = n->values[v];
                known |= (uint8_t)(1 << v);
            } else if (field == SCNCUTSCENE_ROTATION)
                values[v] += n->values[v];
            else {
                float a, b;
                memcpy(&a, &values[v], 4);
                memcpy(&b, &n->values[v], 4);
                a += b;
                memcpy(&values[v], &a, 4);
            }
        }
    }
    return known;
}

// A call to NodeTransform: the values set or added, or those read (when the calls have set them)
static void NodeCall(Context* c, uint32_t frame, const VM_Call* call, VM_Memory* memory) {
    if (call->count < 5 || !Known(call, 0) || !Known(call, 1) || !Known(call, 4))
        return;
    uint32_t mode = call->values[4];
    for (int argument = 2; argument <= 3; argument++) {
        if (!Known(call, argument) || call->values[argument] == 0)
            continue;
        uint8_t field = argument == 3 ? SCNCUTSCENE_ROTATION : mode == SCNCUTSCENE_NODE_SCALE ? SCNCUTSCENE_SCALE :
            SCNCUTSCENE_POSITION;
        if (argument == 3 && mode == SCNCUTSCENE_NODE_SCALE)
            continue;
        if (mode == SCNCUTSCENE_NODE_READ) {
            uint32_t values[3] = { 0 };
            uint8_t known = CurrentValue(c->cutscene, call->values[0], (uint16_t)call->values[1], field, values);
            uint32_t bytes = 0;
            for (int v = 0; v < 3; v++)
                bytes |= (known >> v & 1) ? 0xFu << (v * 4) : 0;
            VM_WriteMemory(memory, call->values[argument], sizeof(values), (const uint8_t*)values, bytes);
            continue;
        }
        if (mode > SCNCUTSCENE_NODE_SCALE)
            continue;
        SCNCutscene_Node node = { frame, call->address, call->values[0], (uint16_t)call->values[1], field,
            mode == SCNCUTSCENE_NODE_ADD, { 0 }, 0 };
        uint8_t bytes[12];
        uint32_t known;
        if (!VM_ReadMemory(memory, call->values[argument], sizeof(bytes), bytes, &known))
            continue;
        for (int v = 0; v < 3; v++) {
            memcpy(&node.values[v], bytes + v * 4, 4);
            if (((known >> (v * 4)) & 0xF) == 0xF)
                node.known |= (uint8_t)(1 << v);
        }
        if (node.known != 0)
            AddNode(c, &node);
    }
}

static void OnTimedCall(void* context, uint32_t frame, uint32_t thread, const VM_Call* call, VM_Memory* memory,
    VM_Answer* answer) {
    (void)thread;
    Context* c = context;
    switch (call->id) {
    case SCNCUTSCENE_PLAY:
        if (Known(call, 0) && call->values[0] == 0xFFFFFFFF)
            *answer = (VM_Answer) { .known = true, .value = frame < c->frames ? 1 : 0 };
        break;
    case SCNCUTSCENE_NODE:
        NodeCall(c, frame, call, memory);
        break;
    case SCNCUTSCENE_SOUND:
        if (Known(call, 0))
            AddSound(c, &(SCNCutscene_Sound) { frame, call->address, call->values[0] });
        break;
    default:
        break;
    }
}

// The parameters of each function of the program that are the slot of a call to Cutscene: bit k, the parameter k is
// the argument 0 of a call to Cutscene, or an argument of a call to a function whose parameter is. NULL on memory error
static uint32_t* SlotParameters(const BC_Program* program) {
    uint32_t* parameters = calloc(program->functionCount + 1, sizeof(uint32_t));
    if (parameters == NULL)
        return NULL;
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t c = 0; c < program->callCount; c++) {
            const SH4_Call* call = &program->calls[c];
            int function = BC_FunctionAt(program, call->address);
            if (function < 0)
                continue;
            uint32_t bits = 0;
            if (call->kind == SH4_CALL_GAME && call->id == SCNCUTSCENE_PLAY && call->argumentCount >= 1 &&
                call->arguments[0].kind == SH4_VALUE_PARAMETER && call->arguments[0].value < 32)
                bits = 1u << call->arguments[0].value;
            int callee = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(program, call->target) : -1;
            for (uint32_t a = 0; callee >= 0 && a < call->argumentCount && a < 32; a++) {
                const SH4_Value* argument = &call->arguments[a];
                if ((parameters[callee] >> a & 1) && argument->kind == SH4_VALUE_PARAMETER && argument->value < 32)
                    bits |= 1u << argument->value;
            }
            if ((parameters[function] | bits) != parameters[function]) {
                parameters[function] |= bits;
                changed = true;
            }
        }
    }
    return parameters;
}

// The call plays the slot: a call to Cutscene with it, or to a function whose parameter that has it is a slot
static bool PlaysSlot(const BC_Program* program, const SH4_Call* call, const uint32_t* parameters, uint32_t slot) {
    if (call->kind == SH4_CALL_GAME)
        return call->id == SCNCUTSCENE_PLAY && call->argumentCount >= 1 && call->arguments[0].kind == SH4_VALUE_CONSTANT &&
            call->arguments[0].value == slot;
    int callee = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(program, call->target) : -1;
    for (uint32_t a = 0; callee >= 0 && a < call->argumentCount && a < 32; a++) {
        if ((parameters[callee] >> a & 1) && call->arguments[a].kind == SH4_VALUE_CONSTANT && call->arguments[a].value == slot)
            return true;
    }
    return false;
}

static void ClearCalls(SCNCutscene* cutscene) {
    cutscene->nodeCount = 0;
    cutscene->soundCount = 0;
}

bool SCNCutscene_Run(const BC_Program* program, uint32_t slot, const char* file, uint32_t frames, SCNCutscene* cutscene) {
    memset(cutscene, 0, sizeof(*cutscene));
    Context context = { program, slot, file, frames, cutscene, 0, 0, true };
    VM_Environment environment = { &context, OnCall, NULL, OnCapture };
    VM_Snapshot* snapshot = VM_Capture(program, &environment);
    // From the functions that give the slot as a constant to a call that plays it, when the emulation from the entry
    // point doesn't get to it (a call deep in the loops of the map, e.g. the cutscene 16 of scene/01/d000, played by
    // func(16) when Ryo takes the bus)
    uint32_t* parameters = snapshot == NULL ? SlotParameters(program) : NULL;
    int last = -1;
    for (uint32_t c = 0, tried = 0; c < program->callCount && parameters != NULL && snapshot == NULL && context.ok; c++) {
        int function = BC_FunctionAt(program, program->calls[c].address);
        if (function < 0 || function == last || !PlaysSlot(program, &program->calls[c], parameters, slot))
            continue;
        last = function;    // The calls are sorted by address: those of a function follow each other
        snapshot = VM_CaptureFrom(program, program->functions[function], &environment);
        if (++tried == SCNCUTSCENE_MAX_STARTS)
            break;
    }
    free(parameters);
    if (snapshot == NULL)
        return true;
    cutscene->found = true;
    cutscene->address = VM_SnapshotAddress(snapshot);
    int function = BC_FunctionAt(program, cutscene->address);
    cutscene->function = function >= 0 ? program->functions[function] : cutscene->address;
    // From the start of the function, so that what it does before the cutscene is kept (e.g. the doors of the bus
    // opened); from the call if the run doesn't reach it that way
    VM_TimedEnvironment timed = { &context, OnTimedCall };
    bool ok = VM_RunTimed(program, snapshot, true, frames, &timed, &cutscene->run);
    if (ok && context.ok && !cutscene->run.started) {
        ClearCalls(cutscene);
        cutscene->function = cutscene->address;
        ok = VM_RunTimed(program, snapshot, false, frames, &timed, &cutscene->run);
    }
    VM_FreeSnapshot(snapshot);
    if (!ok || !context.ok) {
        SCNCutscene_Free(cutscene);
        return false;
    }
    return true;
}

void SCNCutscene_Free(SCNCutscene* cutscene) {
    free(cutscene->nodes);
    free(cutscene->sounds);
    memset(cutscene, 0, sizeof(*cutscene));
}

bool SCNCutscene_MovesCharacter(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->nodeCount; i++) {
        if (cutscene->nodes[i].character == character)
            return true;
    }
    return false;
}

bool SCNCutscene_NodeAt(const SCNCutscene* cutscene, uint32_t character, uint16_t node, uint32_t frame,
    SCNCutscene_Transform* transform) {
    bool moved = false;
    for (uint32_t i = 0; i < cutscene->nodeCount && cutscene->nodes[i].frame <= frame; i++) {
        const SCNCutscene_Node* n = &cutscene->nodes[i];
        if (n->character != character || n->node != node)
            continue;
        moved = true;
        for (int v = 0; v < 3; v++) {
            if (!(n->known & (1 << v)))
                continue;
            if (n->field == SCNCUTSCENE_ROTATION) {
                int32_t value = (int32_t)n->values[v];
                transform->rotation[v] = n->add ? transform->rotation[v] + value : value;
            } else {
                float value;
                memcpy(&value, &n->values[v], sizeof(value));
                float* target = n->field == SCNCUTSCENE_POSITION ? &transform->position[v] : &transform->scale[v];
                *target = n->add ? *target + value : value;
            }
        }
    }
    return moved;
}

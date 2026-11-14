#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "scenario/scnCutscene.h"
#include "scenario/scnGoal.h"

#define SCNCUTSCENE_MAX_STARTS  16      // Functions from which the emulation looks for the call that plays the slot
#define SCNCUTSCENE_MAX_CALLERS 8       // Functions tried at each level up
#define SCNCUTSCENE_MAX_WAITS   64      // Of a path of the capture
#define SCNCUTSCENE_MAX_SETTERS 8       // Functions run to know where a character is
#define SCNCUTSCENE_SETTER_STEPS 100000 // Of each one
#define START_INSTRUCTION       1       // BC_Program.starts
#define START_TARGET            2

// What the program has set of a character on a path, in the memory of the game (VM_GAME_BASE): the entries whose ID is
// known are used. The values are known where the calls have set known values
#define CHARACTER_SIZE          48
#define CHARACTER_COUNT         (VM_GAME_SIZE / CHARACTER_SIZE)
#define CHARACTER_ID            0       // 4 bytes
#define CHARACTER_POSITION      4       // 3 floats
#define CHARACTER_ANGLE         16      // 3 integers, 0x10000 = 360 degrees
#define CHARACTER_POSITION_CALL 28      // Address of the last call that set the position
#define CHARACTER_ANGLE_CALL    32
#define CHARACTER_DISPLAY       36      // 1 byte: 1 shown, 0 hidden
#define CHARACTER_ACTIVE        37      // 1 byte: 1 awake, 0 asleep
#define CHARACTER_ATTACHED      38      // 1 byte, known when the character has been attached to another one
#define CHARACTER_DISPLAY_CALL  40
#define CHARACTER_ACTIVE_CALL   44

// Where the functions of the program that place a character put it, when they agree: the position and the angles that
// it has when the program reads them before it places it on the path (e.g. VM_0 of scene/01/d000, placed where Ryo is
// by the loop of the map before the event of the vending machine)
typedef struct {
    uint32_t    character;
    uint32_t    values[2][3];   // Position (floats), angles (integers)
    uint8_t     known[2];       // Bit i: values[i] is set by a function
    uint8_t     differ[2];      // Bit i: the functions set different values
} Setter;

typedef struct {
    const BC_Program*   program;
    uint32_t            slot;
    const char*         file;
    uint32_t            frames;
    const SCNCutscene_Scene* scene; // NULL if not known
    SCNCutscene*        cutscene;
    uint32_t            nodeCapacity;
    uint32_t            soundCapacity;
    uint32_t            placeCapacity;
    uint32_t            displayCapacity;
    bool                waitFirst;  // The call is captured after the first wait of its path
    uint32_t            setterCount;
    uint32_t            setterCapacity;
    Setter*             setters;
    bool                ok;
    uint32_t            attachCapacity;
    uint32_t            packageCapacity;
    uint32_t            loaderCount;
    struct LoaderCall*  loaders;    // Calls to the functions that load the package of a character
    bool                packagesRead; // At the first call of the run with time
} Context;

// A call to a function of the program that loads the package of a character (SCNCUTSCENE_PACKAGE until
// SCNCUTSCENE_EXISTS), with two variables of the program as the arguments of the character and of the name
typedef struct LoaderCall {
    uint32_t    address;
    uint32_t    character;  // Offsets of the variables
    uint32_t    name;
    uint32_t    folder;     // Offset in the data of the name of the folder
} LoaderCall;

static void Initialize(void* context, VM_Memory* memory);
static const Setter* SetterOf(Context* c, uint32_t character);

// The state of a path for the capture: path[0], the AUTH file loaded in the slot; path[1], 1 once it has waited
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

// The characters of the memory of the game

static uint32_t ReadWord(VM_Memory* memory, uint32_t address, bool* known) {
    uint8_t bytes[4] = { 0 };
    uint32_t bits = 0;
    *known = VM_ReadMemory(memory, address, 4, bytes, &bits) && bits == 0xF;
    uint32_t value;
    memcpy(&value, bytes, 4);
    return value;
}

static void WriteWord(VM_Memory* memory, uint32_t address, uint32_t value, bool known) {
    VM_WriteMemory(memory, address, 4, (const uint8_t*)&value, known ? 0xF : 0);
}

// Address of the entry of a character, 0 if it has none and it can't be added (create false, or no room)
static uint32_t CharacterEntry(VM_Memory* memory, uint32_t character, bool create) {
    uint32_t free = 0;
    for (uint32_t i = 0; i < CHARACTER_COUNT; i++) {
        uint32_t address = VM_GAME_BASE + i * CHARACTER_SIZE;
        bool known;
        uint32_t id = ReadWord(memory, address + CHARACTER_ID, &known);
        if (known && id == character)
            return address;
        if (!known && free == 0)
            free = address;
    }
    if (!create || free == 0)
        return 0;
    WriteWord(memory, free + CHARACTER_ID, character, true);
    return free;
}

// The character is moved by the AUTH file during the cutscene
static bool IsSceneCharacter(const Context* c, uint32_t character) {
    for (uint32_t i = 0; c->scene != NULL && i < c->scene->characterCount; i++) {
        if (c->scene->characters[i] == character)
            return true;
    }
    return false;
}

// SetPosition and SetAngle: the values of the vector selected by the flags, set or added
static void SetsVector(VM_Memory* memory, const VM_Call* call) {
    if (call->count < 3 || !Known(call, 0) || !Known(call, 1) || !Known(call, 2))
        return;
    bool angle = call->id == SCNCUTSCENE_SET_ANGLE;
    uint8_t bytes[12];
    uint32_t known;
    if (!VM_ReadMemory(memory, call->values[2], sizeof(bytes), bytes, &known))
        return;
    uint32_t entry = CharacterEntry(memory, call->values[0], true);
    if (entry == 0)
        return;
    uint32_t field = entry + (angle ? CHARACTER_ANGLE : CHARACTER_POSITION);
    const uint32_t selected[3] = { SCNCUTSCENE_PLACE_X, SCNCUTSCENE_PLACE_Y, SCNCUTSCENE_PLACE_Z };
    bool add = (call->values[1] & SCNCUTSCENE_PLACE_ADD) != 0;
    for (int v = 0; v < 3; v++) {
        if (!(call->values[1] & selected[v]))
            continue;
        uint32_t value;
        memcpy(&value, bytes + v * 4, 4);
        bool valueKnown = ((known >> (v * 4)) & 0xF) == 0xF;
        if (add) {
            bool currentKnown;
            uint32_t current = ReadWord(memory, field + v * 4, &currentKnown);
            valueKnown &= currentKnown;
            if (angle)
                value += current;
            else {
                float a, b;
                memcpy(&a, &current, 4);
                memcpy(&b, &value, 4);
                a += b;
                memcpy(&value, &a, 4);
            }
        }
        WriteWord(memory, field + v * 4, value, valueKnown);
    }
    WriteWord(memory, entry + (angle ? CHARACTER_ANGLE_CALL : CHARACTER_POSITION_CALL), call->address, true);
}

// Attached to another character: its position and angles are relative to it, not known
static void Attaches(VM_Memory* memory, const VM_Call* call) {
    if (call->count < 1 || !Known(call, 0))
        return;
    uint32_t entry = CharacterEntry(memory, call->values[0], true);
    for (int v = 0; entry != 0 && v < 3; v++) {
        WriteWord(memory, entry + CHARACTER_POSITION + v * 4, 0, false);
        WriteWord(memory, entry + CHARACTER_ANGLE + v * 4, 0, false);
    }
    uint8_t attached = 1;
    if (entry != 0)
        VM_WriteMemory(memory, entry + CHARACTER_ATTACHED, 1, &attached, 1);
}

// CharacterDisplay and CharacterActive. Returns the flag set (1 or 0), -1 if none
static int SetsDisplay(VM_Memory* memory, const VM_Call* call) {
    if (call->count < 2 || !Known(call, 0) || !Known(call, 1))
        return -1;
    bool active = call->id == SCNCUTSCENE_ACTIVE;
    uint32_t value = call->values[1];
    int on = active ? (value == 1 ? 1 : value == 0 ? 0 : -1) : (value == 1 ? 1 : value == 2 ? 0 : -1);
    uint32_t entry = on >= 0 ? CharacterEntry(memory, call->values[0], true) : 0;
    if (entry == 0)
        return -1;
    uint8_t byte = (uint8_t)on;
    VM_WriteMemory(memory, entry + (active ? CHARACTER_ACTIVE : CHARACTER_DISPLAY), 1, &byte, 1);
    WriteWord(memory, entry + (active ? CHARACTER_ACTIVE_CALL : CHARACTER_DISPLAY_CALL), call->address, true);
    return on;
}

// GetPosition and GetAngle write the vector that the program has set for the character: of Ryo at the start of the
// cutscene before it (timed false), or the one of the functions that place the character, unknown for the characters of
// the AUTH file during it and for the nodes
static void ReadsVector(Context* c, VM_Memory* memory, const VM_Call* call, bool timed, VM_Answer* answer) {
    bool angle = call->id == SCNCUTSCENE_GET_ANGLE;
    uint32_t argument = call->id == SCNCUTSCENE_GET_POSITION ? 2 : angle ? 1 : 0;
    if (argument == 0 || !Known(call, argument))
        return;
    answer->address = call->values[argument];
    answer->size = 12;
    answer->knownBytes = 0;
    if (!Known(call, 0) || (!angle && (!Known(call, 1) || call->values[1] != 0xFFFFFFFF)))
        return;
    uint32_t character = call->values[0];
    if (timed && IsSceneCharacter(c, character))
        return;
    uint32_t entry = CharacterEntry(memory, character, false);
    bool ryo = !timed && character == SCNCUTSCENE_RYO && c->scene != NULL && c->scene->ryo;
    uint8_t byte;
    uint32_t attached = 0;
    if (entry != 0)
        VM_ReadMemory(memory, entry + CHARACTER_ATTACHED, 1, &byte, &attached);
    const Setter* setter = NULL;
    for (int v = 0; v < 3; v++) {
        bool known = false;
        uint32_t value = entry != 0 ? ReadWord(memory, entry + (angle ? CHARACTER_ANGLE : CHARACTER_POSITION) + v * 4,
            &known) : 0;
        if (!known && ryo) {
            known = true;
            if (angle)
                value = (uint32_t)(int32_t)lroundf(c->scene->ryoAngle[v] * 65536.0f / 360.0f);
            else
                memcpy(&value, &c->scene->ryoPosition[v], 4);
        }
        // Not placed on the path, nor attached: where the program places it
        if (!known && !ryo && !attached && character != SCNCUTSCENE_RYO) {
            if (setter == NULL)
                setter = SetterOf(c, character);
            if (setter != NULL && ((setter->known[angle] & ~setter->differ[angle]) >> v & 1)) {
                known = true;
                value = setter->values[angle][v];
            }
        }
        if (!known)
            continue;
        if (angle)
            value &= 0xFFFF;
        memcpy(answer->bytes + v * 4, &value, 4);
        answer->knownBytes |= (uint16_t)(0xF << (v * 4));
    }
}

// What the calls to the game tell of the characters, before and during the cutscene
static void CharacterCall(Context* c, VM_Memory* memory, const VM_Call* call, bool timed, VM_Answer* answer) {
    switch (call->id) {
    case SCNCUTSCENE_GET_POSITION:
    case SCNCUTSCENE_GET_ANGLE:
        ReadsVector(c, memory, call, timed, answer);
        break;
    case SCNCUTSCENE_SET_POSITION:
    case SCNCUTSCENE_SET_ANGLE:
        SetsVector(memory, call);
        break;
    case SCNCUTSCENE_ATTACH:
        Attaches(memory, call);
        break;
    default:
        break;
    }
}

static bool OnCapture(void* context, const VM_Call* call, const uint8_t* path) {
    const Context* c = context;
    return call->id == SCNCUTSCENE_PLAY && Known(call, 0) && call->values[0] == c->slot && path[0] != PATH_OTHER &&
        (!c->waitFirst || path[1] != 0);
}

// The variables in which the program reads Ryo's position and angles (GetPosition and GetAngle of AKIR to a variable,
// e.g. var_D4 of scenario/act01_d000.scn, read by the thread of the loop of the game and used by that of the loop of
// the map to know where Ryo is): his position and angles at the start of the cutscene
static void Initialize(void* context, VM_Memory* memory) {
    const Context* c = context;
    if (c->scene == NULL || !c->scene->ryo)
        return;
    for (uint32_t i = 0; i < c->program->callCount; i++) {
        const SH4_Call* call = &c->program->calls[i];
        bool angle = call->id == SCNCUTSCENE_GET_ANGLE;
        uint32_t argument = angle ? 1 : 2;
        if (call->kind != SH4_CALL_GAME || (call->id != SCNCUTSCENE_GET_POSITION && !angle) ||
            call->argumentCount <= argument || call->arguments[0].kind != SH4_VALUE_CONSTANT ||
            call->arguments[0].value != SCNCUTSCENE_RYO || call->arguments[argument].kind != SH4_VALUE_VARIABLE ||
            (!angle && (call->arguments[1].kind != SH4_VALUE_CONSTANT || call->arguments[1].value != 0xFFFFFFFF)))
            continue;
        uint32_t values[3];
        for (int v = 0; v < 3; v++) {
            if (angle)
                values[v] = (uint32_t)(int32_t)lroundf(c->scene->ryoAngle[v] * 65536.0f / 360.0f) & 0xFFFF;
            else
                memcpy(&values[v], &c->scene->ryoPosition[v], 4);
        }
        VM_WriteMemory(memory, VM_VARIABLES_BASE + call->arguments[argument].value, sizeof(values), (const uint8_t*)values,
            0xFFF);
    }
}

static void OnCall(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Memory* memory, VM_Answer* answer) {
    Context* c = context;
    if (yielded)
        path[1] = 1;
    CharacterCall(c, memory, call, false, answer);
    // The cutscenes played before end at once: their loops (while Cutscene(-1) returns 1) don't fork at every frame
    if (call->id == SCNCUTSCENE_PLAY && Known(call, 0) && call->values[0] == 0xFFFFFFFF)
        *answer = (VM_Answer) { .known = true, .value = 0 };
    if (call->id == SCNCUTSCENE_DISPLAY || call->id == SCNCUTSCENE_ACTIVE)
        SetsDisplay(memory, call);
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

static void AddPlace(Context* c, const SCNCutscene_Place* place) {
    SCNCutscene* cutscene = c->cutscene;
    if (cutscene->placeCount == c->placeCapacity) {
        uint32_t capacity = c->placeCapacity == 0 ? 16 : c->placeCapacity * 2;
        SCNCutscene_Place* places = realloc(cutscene->places, capacity * sizeof(SCNCutscene_Place));
        if (places == NULL) {
            c->ok = false;
            return;
        }
        cutscene->places = places;
        c->placeCapacity = capacity;
    }
    cutscene->places[cutscene->placeCount++] = *place;
}

static void AddDisplay(Context* c, const SCNCutscene_Display* display) {
    SCNCutscene* cutscene = c->cutscene;
    if (cutscene->displayCount == c->displayCapacity) {
        uint32_t capacity = c->displayCapacity == 0 ? 16 : c->displayCapacity * 2;
        SCNCutscene_Display* displays = realloc(cutscene->displays, capacity * sizeof(SCNCutscene_Display));
        if (displays == NULL) {
            c->ok = false;
            return;
        }
        cutscene->displays = displays;
        c->displayCapacity = capacity;
    }
    cutscene->displays[cutscene->displayCount++] = *display;
}

// What the program has set of its characters before the cutscene, kept in the memory of the game of the snapshot: the
// places and the flags at the frame 0
static void AddSnapshotCharacters(Context* c, const VM_Snapshot* snapshot) {
    const uint32_t selected[3] = { SCNCUTSCENE_PLACE_X, SCNCUTSCENE_PLACE_Y, SCNCUTSCENE_PLACE_Z };
    for (uint32_t i = 0; i < CHARACTER_COUNT; i++) {
        uint8_t entry[CHARACTER_SIZE];
        uint32_t known[2];
        if (!VM_ReadSnapshotGame(snapshot, VM_GAME_BASE + i * CHARACTER_SIZE, 32, entry, &known[0]) ||
            !VM_ReadSnapshotGame(snapshot, VM_GAME_BASE + i * CHARACTER_SIZE + 32, CHARACTER_SIZE - 32, entry + 32, &known[1]))
            continue;
        uint64_t bits = known[0] | (uint64_t)known[1] << 32;
        if ((bits & 0xF) != 0xF)
            continue;
        uint32_t character, address;
        memcpy(&character, entry + CHARACTER_ID, 4);
        for (int a = 0; a < 2; a++) {
            uint32_t field = a ? CHARACTER_ANGLE : CHARACTER_POSITION;
            uint32_t call = a ? CHARACTER_ANGLE_CALL : CHARACTER_POSITION_CALL;
            SCNCutscene_Place place = { 0, 0, character, a == 1, 0, { 0 }, 0 };
            for (int v = 0; v < 3; v++) {
                if (((bits >> (field + v * 4)) & 0xF) != 0xF)
                    continue;
                memcpy(&place.values[v], entry + field + v * 4, 4);
                place.known |= (uint8_t)(1 << v);
                place.flags |= selected[v];
            }
            if (((bits >> call) & 0xF) == 0xF) {
                memcpy(&address, entry + call, 4);
                place.address = address;
            }
            if (place.known != 0)
                AddPlace(c, &place);
        }
        for (int a = 0; a < 2; a++) {
            uint32_t field = a ? CHARACTER_ACTIVE : CHARACTER_DISPLAY;
            uint32_t call = a ? CHARACTER_ACTIVE_CALL : CHARACTER_DISPLAY_CALL;
            if (!((bits >> field) & 1))
                continue;
            SCNCutscene_Display display = { 0, 0, character, a == 1, entry[field] != 0 };
            if (((bits >> call) & 0xF) == 0xF) {
                memcpy(&address, entry + call, 4);
                display.address = address;
            }
            AddDisplay(c, &display);
        }
    }
}

static void AddAttach(Context* c, const SCNCutscene_Attach* attach) {
    SCNCutscene* cutscene = c->cutscene;
    if (cutscene->attachCount == c->attachCapacity) {
        uint32_t capacity = c->attachCapacity == 0 ? 16 : c->attachCapacity * 2;
        SCNCutscene_Attach* attaches = realloc(cutscene->attaches, capacity * sizeof(SCNCutscene_Attach));
        if (attaches == NULL) {
            c->ok = false;
            return;
        }
        cutscene->attaches = attaches;
        c->attachCapacity = capacity;
    }
    cutscene->attaches[cutscene->attachCount++] = *attach;
}

static void AddPackage(Context* c, const SCNCutscene_Package* package) {
    SCNCutscene* cutscene = c->cutscene;
    for (uint32_t i = 0; i < cutscene->packageCount; i++) {
        if (cutscene->packages[i].character == package->character)
            return;     // The first call
    }
    if (cutscene->packageCount == c->packageCapacity) {
        uint32_t capacity = c->packageCapacity == 0 ? 4 : c->packageCapacity * 2;
        SCNCutscene_Package* packages = realloc(cutscene->packages, capacity * sizeof(SCNCutscene_Package));
        if (packages == NULL) {
            c->ok = false;
            return;
        }
        cutscene->packages = packages;
        c->packageCapacity = capacity;
    }
    cutscene->packages[cutscene->packageCount++] = *package;
}

// Attach and Detach: the vectors of Attach, relative to the node
static void AttachCall(Context* c, uint32_t frame, const VM_Call* call, VM_Memory* memory) {
    if (!Known(call, 0))
        return;
    SCNCutscene_Attach attach = { frame, call->address, call->values[0], 0, 0, { 0 }, { 0 }, 0 };
    if (call->id == SCNCUTSCENE_ATTACH) {
        if (call->count < 5 || !Known(call, 1) || !Known(call, 2) || call->values[1] == 0)
            return;
        attach.parent = call->values[1];
        attach.node = (uint16_t)call->values[2];
        for (int a = 0; a < 2; a++) {
            uint8_t bytes[12];
            uint32_t known = 0;
            if (!Known(call, 3 + a) || !VM_ReadMemory(memory, call->values[3 + a], sizeof(bytes), bytes, &known))
                continue;
            for (int v = 0; v < 3; v++) {
                if (((known >> (v * 4)) & 0xF) != 0xF)
                    continue;
                memcpy(a == 0 ? (void*)&attach.position[v] : (void*)&attach.angles[v], bytes + v * 4, 4);
                attach.known |= (uint8_t)(1 << (a * 3 + v));
            }
        }
    }
    AddAttach(c, &attach);
}

// The variable pushed as an argument of a call just before it (0: the last push), -1 if it isn't one
static int64_t PushedVariable(const BC_Program* program, uint32_t address, uint32_t argument) {
    uint32_t pc = address;
    BC_Instruction ins = { 0 };
    for (uint32_t i = 0; i <= argument; i++) {
        // The flow can't come between the pushes
        if (pc == 0 || (program->starts[pc] & START_TARGET))
            return -1;
        uint32_t previous = pc - 1;
        while (previous > 0 && !(program->starts[previous] & START_INSTRUCTION))
            previous--;
        if (!BC_Decode(program->scn->data, program->codeEnd, previous, &ins) || previous + ins.size != pc ||
            ins.kind != BC_PUSH)
            return -1;
        pc = previous;
    }
    return ins.area == BC_AREA_VARIABLE && ins.width == 4 && ins.operand >= 0 ? ins.operand : -1;
}

// The calls to the functions that load the package of a character (e.g. func(var_118, var_11C) of
// scenario/act01_d000.scn, whose function loads the package of /scene/01/stream/ of its argument 1 until the character of
// its argument 0 exists)
static void FindLoaders(Context* c) {
    const BC_Program* program = c->program;
    for (uint32_t i = 0; i < program->callCount && c->ok; i++) {
        const SH4_Call* load = &program->calls[i];
        if (load->kind != SH4_CALL_GAME || load->id != SCNCUTSCENE_PACKAGE || load->argumentCount < 4 ||
            load->arguments[0].kind != SH4_VALUE_CONSTANT || load->arguments[0].value != 0 ||
            load->arguments[1].kind != SH4_VALUE_CONSTANT || load->arguments[1].value != 0 ||
            load->arguments[2].kind != SH4_VALUE_DATA || load->arguments[3].kind != SH4_VALUE_PARAMETER)
            continue;
        // The character, in the same function
        int character = -1;
        for (uint32_t j = 0; j < program->callCount; j++) {
            const SH4_Call* exists = &program->calls[j];
            if (exists->function == load->function && exists->kind == SH4_CALL_GAME && exists->id == SCNCUTSCENE_EXISTS &&
                exists->argumentCount >= 2 && exists->arguments[0].kind == SH4_VALUE_CONSTANT && exists->arguments[0].value == 1 &&
                exists->arguments[1].kind == SH4_VALUE_PARAMETER && exists->arguments[1].value != load->arguments[3].value)
                character = (int)exists->arguments[1].value;
        }
        if (character < 0)
            continue;
        uint32_t start = program->functions[load->function];
        for (uint32_t j = 0; j < program->callCount; j++) {
            const SH4_Call* call = &program->calls[j];
            if (call->kind != SH4_CALL_LOCAL || call->target != start)
                continue;
            int64_t characterVariable = PushedVariable(program, call->address, (uint32_t)character);
            int64_t nameVariable = PushedVariable(program, call->address, load->arguments[3].value);
            if (characterVariable < 0 || nameVariable < 0)
                continue;
            LoaderCall* loaders = realloc(c->loaders, (c->loaderCount + 1) * sizeof(LoaderCall));
            if (loaders == NULL) {
                c->ok = false;
                return;
            }
            c->loaders = loaders;
            c->loaders[c->loaderCount++] = (LoaderCall) { call->address, (uint32_t)characterVariable, (uint32_t)nameVariable,
                load->arguments[2].value };
        }
    }
}

// The packages of the calls to the functions that load the package of a character, with the values of the variables
static void ReadPackages(Context* c, VM_Memory* memory) {
    for (uint32_t i = 0; i < c->loaderCount; i++) {
        const LoaderCall* loader = &c->loaders[i];
        bool characterKnown, nameKnown;
        uint32_t character = ReadWord(memory, VM_VARIABLES_BASE + loader->character, &characterKnown);
        uint32_t name = ReadWord(memory, VM_VARIABLES_BASE + loader->name, &nameKnown);
        int64_t offset = nameKnown ? VM_DataOffset(c->program, name) : -1;
        const char* text = offset >= 0 ? BC_DataString(c->program, (uint32_t)offset) : NULL;
        const char* folder = BC_DataString(c->program, loader->folder);
        if (!characterKnown || character == 0 || text == NULL || folder == NULL || text[0] == '\0' ||
            strlen(text) >= SCNCUTSCENE_NAME_SIZE || strlen(folder) >= SCNCUTSCENE_FOLDER_SIZE)
            continue;
        SCNCutscene_Package package = { character, loader->address, { 0 }, { 0 } };
        memcpy(package.folder, folder, strlen(folder) + 1);
        memcpy(package.name, text, strlen(text) + 1);
        AddPackage(c, &package);
    }
}

// A call to SetPosition or SetAngle: the values of the vector selected by the flags
static void PlaceCall(Context* c, uint32_t frame, const VM_Call* call, VM_Memory* memory) {
    if (call->count < 3 || !Known(call, 0) || !Known(call, 1) || !Known(call, 2))
        return;
    SCNCutscene_Place place = { frame, call->address, call->values[0], call->id == SCNCUTSCENE_SET_ANGLE, call->values[1],
        { 0 }, 0 };
    uint8_t bytes[12];
    uint32_t known;
    if (!VM_ReadMemory(memory, call->values[2], sizeof(bytes), bytes, &known))
        return;
    const uint32_t selected[3] = { SCNCUTSCENE_PLACE_X, SCNCUTSCENE_PLACE_Y, SCNCUTSCENE_PLACE_Z };
    for (int v = 0; v < 3; v++) {
        memcpy(&place.values[v], bytes + v * 4, 4);
        if ((place.flags & selected[v]) && ((known >> (v * 4)) & 0xF) == 0xF)
            place.known |= (uint8_t)(1 << v);
    }
    if (place.known != 0)
        AddPlace(c, &place);
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
    if (!c->packagesRead) {
        c->packagesRead = true;
        ReadPackages(c, memory);
    }
    CharacterCall(c, memory, call, true, answer);
    switch (call->id) {
    case SCNCUTSCENE_PLAY:
        if (Known(call, 0) && call->values[0] == 0xFFFFFFFF)
            *answer = (VM_Answer) { .known = true, .value = frame < c->frames ? 1 : 0 };
        break;
    case SCNCUTSCENE_NODE:
        NodeCall(c, frame, call, memory);
        break;
    case SCNCUTSCENE_SET_POSITION:
    case SCNCUTSCENE_SET_ANGLE:
        PlaceCall(c, frame, call, memory);
        break;
    case SCNCUTSCENE_ATTACH:
    case SCNCUTSCENE_DETACH:
        AttachCall(c, frame, call, memory);
        break;
    case SCNCUTSCENE_SOUND:
        if (Known(call, 0))
            AddSound(c, &(SCNCutscene_Sound) { frame, call->address, call->values[0] });
        break;
    case SCNCUTSCENE_DISPLAY:
    case SCNCUTSCENE_ACTIVE: {
        int on = SetsDisplay(memory, call);
        if (on >= 0)
            AddDisplay(c, &(SCNCutscene_Display) { frame, call->address, call->values[0], call->id == SCNCUTSCENE_ACTIVE,
                on == 1 });
        break;
    }
    default:
        break;
    }
}

static void ClearCalls(SCNCutscene* cutscene) {
    cutscene->nodeCount = 0;
    cutscene->soundCount = 0;
    cutscene->placeCount = 0;
    cutscene->displayCount = 0;
    cutscene->attachCount = 0;
}

// The run of a function that places a character
typedef struct {
    Context*    context;
    Setter*     setter;
} SetterRun;

static void OnSetterCall(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Memory* memory,
    VM_Answer* answer) {
    (void)yielded;
    (void)path;
    const SetterRun* run = context;
    Setter* setter = run->setter;
    if (call->id == SCNCUTSCENE_PLAY && Known(call, 0) && call->values[0] == 0xFFFFFFFF)
        *answer = (VM_Answer) { .known = true, .value = 0 };
    bool angle = call->id == SCNCUTSCENE_SET_ANGLE;
    if ((call->id != SCNCUTSCENE_SET_POSITION && !angle) || call->count < 3 || !Known(call, 0) || !Known(call, 1) ||
        !Known(call, 2) || call->values[0] != setter->character || (call->values[1] & SCNCUTSCENE_PLACE_ADD))
        return;
    uint8_t bytes[12];
    uint32_t known;
    if (!VM_ReadMemory(memory, call->values[2], sizeof(bytes), bytes, &known))
        return;
    const uint32_t selected[3] = { SCNCUTSCENE_PLACE_X, SCNCUTSCENE_PLACE_Y, SCNCUTSCENE_PLACE_Z };
    for (int v = 0; v < 3; v++) {
        if (!(call->values[1] & selected[v]) || ((known >> (v * 4)) & 0xF) != 0xF)
            continue;
        uint32_t value;
        memcpy(&value, bytes + v * 4, 4);
        if (!(setter->known[angle] >> v & 1)) {
            setter->values[angle][v] = value;
            setter->known[angle] |= (uint8_t)(1 << v);
        } else if (setter->values[angle][v] != value)
            setter->differ[angle] |= (uint8_t)(1 << v);
    }
}

static void InitializeSetter(void* context, VM_Memory* memory) {
    Initialize(((SetterRun*)context)->context, memory);
}

// Where the functions that place a character put it: those that call SetPosition or SetAngle with its ID, run from their
// start with Ryo's position known (e.g. the function of the loop of scene/01/d000 that places VM_0 and VM_1 by the zone
// where Ryo is). NULL on memory error
static const Setter* SetterOf(Context* c, uint32_t character) {
    for (uint32_t i = 0; i < c->setterCount; i++) {
        if (c->setters[i].character == character)
            return &c->setters[i];
    }
    if (c->setterCount == c->setterCapacity) {
        uint32_t capacity = c->setterCapacity == 0 ? 8 : c->setterCapacity * 2;
        Setter* setters = realloc(c->setters, capacity * sizeof(Setter));
        if (setters == NULL) {
            c->ok = false;
            return NULL;
        }
        c->setters = setters;
        c->setterCapacity = capacity;
    }
    Setter* setter = &c->setters[c->setterCount++];
    memset(setter, 0, sizeof(*setter));
    setter->character = character;
    const BC_Program* program = c->program;
    int functions[SCNCUTSCENE_MAX_SETTERS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < program->callCount && count < SCNCUTSCENE_MAX_SETTERS; i++) {
        const SH4_Call* call = &program->calls[i];
        if (call->kind != SH4_CALL_GAME || (call->id != SCNCUTSCENE_SET_POSITION && call->id != SCNCUTSCENE_SET_ANGLE) ||
            call->argumentCount < 1 || call->arguments[0].kind != SH4_VALUE_CONSTANT || call->arguments[0].value != character)
            continue;
        int function = BC_FunctionAt(program, call->address);
        bool seen = function < 0;
        for (uint32_t f = 0; f < count && !seen; f++)
            seen = functions[f] == function;
        if (!seen)
            functions[count++] = function;
    }
    SetterRun run = { c, setter };
    VM_Environment environment = { &run, OnSetterCall, NULL, NULL, true, NULL, SCNCUTSCENE_MAX_WAITS, InitializeSetter,
        SCNCUTSCENE_SETTER_STEPS };
    for (uint32_t f = 0; f < count; f++) {
        VM_Result result;
        if (!VM_RunFrom(program, program->functions[functions[f]], &environment, &result)) {
            c->ok = false;
            return NULL;
        }
        VM_Free(&result);
    }
    return setter;
}

// The function is started as a thread
static bool IsThread(const BC_Program* program, uint32_t function) {
    for (uint32_t c = 0; c < program->callCount; c++) {
        const SH4_Call* call = &program->calls[c];
        if (call->kind == SH4_CALL_GAME && call->id == VM_START_THREAD && call->argumentCount >= 1 &&
            call->arguments[0].kind == SH4_VALUE_CONSTANT && call->arguments[0].value == function)
            return true;
    }
    return false;
}

// The functions (indexes) that call a function of the program, or start it as a thread. Up to max
static uint32_t Callers(const BC_Program* program, uint32_t function, uint32_t* callers, uint32_t max) {
    uint32_t count = 0;
    for (uint32_t c = 0; c < program->callCount && count < max; c++) {
        const SH4_Call* call = &program->calls[c];
        bool calls = call->kind == SH4_CALL_LOCAL && call->target == function;
        bool starts = call->kind == SH4_CALL_GAME && call->id == VM_START_THREAD && call->argumentCount >= 1 &&
            call->arguments[0].kind == SH4_VALUE_CONSTANT && call->arguments[0].value == function;
        int caller = calls || starts ? BC_FunctionAt(program, call->address) : -1;
        bool seen = caller < 0 || program->functions[caller] == function;
        for (uint32_t i = 0; i < count && !seen; i++)
            seen = callers[i] == (uint32_t)caller;
        if (!seen)
            callers[count++] = (uint32_t)caller;
    }
    return count;
}

bool SCNCutscene_Run(const BC_Program* program, uint32_t slot, const char* file, uint32_t frames,
    const SCNCutscene_Scene* scene, SCNCutscene* cutscene) {
    memset(cutscene, 0, sizeof(*cutscene));
    Context context = { program, slot, file, frames, scene, cutscene, 0, 0, 0, 0, false, 0, 0, NULL, true };
    uint32_t* parameters = SCNGoal_SlotParameters(program);
    uint32_t* distances = parameters != NULL ? SCNGoal_Distances(program, parameters, slot) : NULL;
    if (distances == NULL) {
        free(parameters);
        return false;
    }
    VM_Environment environment = { &context, OnCall, NULL, OnCapture, true, distances, SCNCUTSCENE_MAX_WAITS, Initialize, 0 };
    VM_Snapshot* snapshot = VM_Capture(program, &environment);
    int start = snapshot != NULL ? BC_FunctionAt(program, program->scn->header.entry) : -1;
    // From the functions that give the slot as a constant to a call that plays it, when the emulation from the entry
    // point doesn't get to it (a call deep in the loops of the map, e.g. the cutscene 16 of scene/01/d000, played by
    // func(16) when Ryo takes the bus)
    int last = -1;
    for (uint32_t c = 0, tried = 0; c < program->callCount && snapshot == NULL && context.ok; c++) {
        int function = BC_FunctionAt(program, program->calls[c].address);
        if (function < 0 || function == last || !SCNGoal_PlaysSlot(program, &program->calls[c], parameters, slot))
            continue;
        last = function;    // The calls are sorted by address: those of a function follow each other
        snapshot = VM_CaptureFrom(program, program->functions[function], &environment);
        start = function;
        if (++tried == SCNCUTSCENE_MAX_STARTS)
            break;
    }
    // Then up, from the functions that call it, as long as they get to the call (e.g. the event of the vending machine
    // of scene/01/d000 that places JIHN, then the loop of the map that places VM_0 where Ryo is)
    for (uint32_t level = 0; snapshot != NULL && start >= 0 && start != BC_FunctionAt(program, program->scn->header.entry) &&
        level < SCNCUTSCENE_MAX_LEVELS && context.ok; level++) {
        uint32_t callers[SCNCUTSCENE_MAX_CALLERS];
        uint32_t count = Callers(program, program->functions[start], callers, SCNCUTSCENE_MAX_CALLERS);
        VM_Snapshot* up = NULL;
        for (uint32_t i = 0; i < count && up == NULL; i++) {
            // From a thread that loops frame after frame (the loop of the map), after it has run once (e.g. the loop of
            // scene/01/d000 calls the event of the vending machine, then places the machines VM_0 and VM_1 where Ryo is)
            context.waitFirst = IsThread(program, program->functions[callers[i]]);
            up = VM_CaptureFrom(program, program->functions[callers[i]], &environment);
            context.waitFirst = false;
            if (up != NULL)
                start = (int)callers[i];
        }
        if (up == NULL)
            break;
        VM_FreeSnapshot(snapshot);
        snapshot = up;
        cutscene->levels++;
    }
    free(parameters);
    free(distances);
    if (snapshot == NULL) {
        free(context.setters);
        return context.ok;
    }
    cutscene->found = true;
    cutscene->start = start >= 0 ? program->functions[start] : program->scn->header.entry;
    cutscene->address = VM_SnapshotAddress(snapshot);
    int function = BC_FunctionAt(program, cutscene->address);
    cutscene->function = function >= 0 ? program->functions[function] : cutscene->address;
    // From the start of the function, so that what it does before the cutscene is kept (e.g. the doors of the bus
    // opened); from the call if the run doesn't reach it that way
    VM_TimedEnvironment timed = { &context, OnTimedCall };
    FindLoaders(&context);
    AddSnapshotCharacters(&context, snapshot);
    bool ok = VM_RunTimed(program, snapshot, true, frames, &timed, &cutscene->run);
    if (ok && context.ok && !cutscene->run.started) {
        ClearCalls(cutscene);
        cutscene->function = cutscene->address;
        AddSnapshotCharacters(&context, snapshot);
        ok = VM_RunTimed(program, snapshot, false, frames, &timed, &cutscene->run);
    }
    VM_FreeSnapshot(snapshot);
    free(context.setters);
    free(context.loaders);
    if (!ok || !context.ok) {
        SCNCutscene_Free(cutscene);
        return false;
    }
    return true;
}

void SCNCutscene_Free(SCNCutscene* cutscene) {
    free(cutscene->nodes);
    free(cutscene->sounds);
    free(cutscene->places);
    free(cutscene->displays);
    free(cutscene->attaches);
    free(cutscene->packages);
    memset(cutscene, 0, sizeof(*cutscene));
}

bool SCNCutscene_AttachesCharacter(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->attachCount; i++) {
        if (cutscene->attaches[i].character == character && cutscene->attaches[i].parent != 0)
            return true;
    }
    return false;
}

const SCNCutscene_Attach* SCNCutscene_AttachAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame) {
    const SCNCutscene_Attach* last = NULL;
    for (uint32_t i = 0; i < cutscene->attachCount && cutscene->attaches[i].frame <= frame; i++) {
        if (cutscene->attaches[i].character == character)
            last = &cutscene->attaches[i];
    }
    return last;
}

const SCNCutscene_Package* SCNCutscene_PackageOf(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->packageCount; i++) {
        if (cutscene->packages[i].character == character)
            return &cutscene->packages[i];
    }
    return NULL;
}

bool SCNCutscene_DisplaysCharacter(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->displayCount; i++) {
        if (cutscene->displays[i].character == character)
            return true;
    }
    return false;
}

bool SCNCutscene_ShownAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, bool* shown) {
    bool found = false, display = true, active = true;
    for (uint32_t i = 0; i < cutscene->displayCount && cutscene->displays[i].frame <= frame; i++) {
        const SCNCutscene_Display* d = &cutscene->displays[i];
        if (d->character != character)
            continue;
        found = true;
        if (d->active)
            active = d->on;
        else
            display = d->on;
    }
    if (found)
        *shown = display && active;
    return found;
}

bool SCNCutscene_MovesCharacter(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->nodeCount; i++) {
        if (cutscene->nodes[i].character == character)
            return true;
    }
    return false;
}

bool SCNCutscene_PlacesCharacter(const SCNCutscene* cutscene, uint32_t character) {
    for (uint32_t i = 0; i < cutscene->placeCount; i++) {
        if (cutscene->places[i].character == character)
            return true;
    }
    return false;
}

bool SCNCutscene_PlaceAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, float position[3], float angle[3]) {
    bool placed = false;
    for (uint32_t i = 0; i < cutscene->placeCount && cutscene->places[i].frame <= frame; i++) {
        const SCNCutscene_Place* p = &cutscene->places[i];
        if (p->character != character)
            continue;
        placed = true;
        bool add = (p->flags & SCNCUTSCENE_PLACE_ADD) != 0;
        for (int v = 0; v < 3; v++) {
            if (!(p->known & (1 << v)))
                continue;
            float value;
            if (p->angle)
                value = (int32_t)p->values[v] * 360.0f / 65536.0f;
            else
                memcpy(&value, &p->values[v], sizeof(value));
            float* target = p->angle ? &angle[v] : &position[v];
            *target = add ? *target + value : value;
        }
    }
    return placed;
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

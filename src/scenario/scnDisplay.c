#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formats/map/mapinfo.h"
#include "scenario/scnDisplay.h"
#include "scenario/scnVm.h"

// The state of a path: the last display of each slot before the first yield, and the last state written of each door
typedef struct {
    uint32_t    set;        // Bit s: the slot s was shown or hidden
    uint32_t    shown;      // Bit s: the last call showed it
    uint32_t    computed;   // Bit s: the last call gave a value that isn't known
    uint8_t     doors[SCNDISPLAY_DOORS / 4]; // 2 bits per door: SCNDisplay_Door
} PathSlots;

_Static_assert(sizeof(PathSlots) <= VM_PATH_SIZE, "The state of a path must fit in the path");

typedef struct {
    const BC_Program*       program;
    const SCNDisplay_Time*  time;
    SCNDisplay*             display;
    uint32_t*               shown;      // Per thread (VM_MAX_THREADS): bit s, the slot s is shown on a settled path
    uint32_t*               hidden;     // The same, hidden
    uint32_t                lastThread; // Highest thread settled + 1
    int64_t                 records;    // Offset of the door records in the data, -1 if none
    uint8_t                 doorStates[SCNDISPLAY_DOORS]; // Per door: bit SCNDisplay_Door, at the end of a path
} Context;

static SCNDisplay_Door PathDoor(const PathSlots* slots, uint32_t door) {
    return (SCNDisplay_Door)((slots->doors[door / 4] >> (door % 4 * 2)) & 3);
}

static void SetPathDoor(PathSlots* slots, uint32_t door, SCNDisplay_Door state) {
    slots->doors[door / 4] = (uint8_t)((slots->doors[door / 4] & ~(3 << (door % 4 * 2))) | (state << (door % 4 * 2)));
}

// ArrayAccess on the door records: reads and writes the words in the memory of the path, and keeps the state written
static void AccessRecords(Context* c, const VM_Call* call, uint8_t* path, VM_Memory* memory, VM_Answer* answer) {
    if (c->records < 0 || call->count < 3 || (call->known & 7) != 7 || VM_DataOffset(c->program, call->values[0]) != c->records)
        return;
    uint32_t operation = call->values[1], index = call->values[2], address = call->values[0] + index * 4;
    uint8_t bytes[4];
    uint32_t known = 0;
    if (index >= c->display->doorCount * (MAPINFO_DOOR_RECORD_SIZE / 4))
        return;
    if (operation == SCNDISPLAY_READ_WORD) {
        if (VM_ReadMemory(memory, address, 4, bytes, &known) && known == 0xF)
            *answer = (VM_Answer) { .known = true, .value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24 };
    } else if (operation == SCNDISPLAY_WRITE_WORD && call->count >= 4) {
        bool valueKnown = (call->known & 8) != 0;
        uint32_t value = call->values[3];
        for (int i = 0; i < 4; i++)
            bytes[i] = (uint8_t)(value >> (i * 8));
        VM_WriteMemory(memory, address, 4, bytes, valueKnown ? 0xF : 0);
        if (index % (MAPINFO_DOOR_RECORD_SIZE / 4) == MAPINFO_DOOR_STATE_WORD) {
            PathSlots slots;
            memcpy(&slots, path, sizeof(slots));
            SetPathDoor(&slots, index / (MAPINFO_DOOR_RECORD_SIZE / 4), !valueKnown ? SCNDISPLAY_DOOR_VARIES :
                (value & MAPINFO_DOOR_OPEN) ? SCNDISPLAY_DOOR_OPEN : SCNDISPLAY_DOOR_CLOSED);
            memcpy(path, &slots, sizeof(slots));
            c->display->doorWrites++;
        }
    }
}

// A string of the data of the program, empty if the value isn't the address of one
static void ProgramString(const BC_Program* program, uint32_t value, char* text, size_t size) {
    text[0] = '\0';
    int64_t offset = VM_DataOffset(program, value);
    if (offset < 0)
        return;
    const SCN_Header* header = &program->scn->header;
    const uint8_t* data = program->scn->data;
    uint32_t start = header->dataStart + (uint32_t)offset, end = header->end < header->size ? header->end : header->size;
    size_t length = 0;
    for (uint32_t i = start; i < end && data[i] != '\0' && length + 1 < size; i++) {
        if (data[i] < 0x20 || data[i] > 0x7E) {
            text[0] = '\0';
            return;
        }
        text[length++] = (char)data[i];
    }
    text[length] = '\0';
}

static bool SameName(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

static void AddPackage(SCNDisplay* display, const char* name) {
    if (name[0] == '\0' || SCNDisplay_Loads(display, name) || display->packageCount == SCNDISPLAY_PACKAGES)
        return;
    snprintf(display->packages[display->packageCount++], SCNDISPLAY_NAME_SIZE, "%s", name);
}

static bool Known(const VM_Call* call, uint32_t argument) {
    return argument < call->count && (call->known & (1u << argument));
}

static void OnCall(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Memory* memory,
    VM_Answer* answer) {
    Context* c = context;
    const SCNDisplay_Time* time = c->time;
    switch (call->id) {
    case SCNDISPLAY_GET_DATE:
        if (Known(call, 0)) {
            const uint8_t bytes[7] = { (uint8_t)(time->year - 1900), (uint8_t)time->month, (uint8_t)time->day, 0,
                (uint8_t)time->hour, (uint8_t)time->minute, 0 };
            answer->address = call->values[0];
            answer->size = sizeof(bytes);
            memcpy(answer->bytes, bytes, sizeof(bytes));
            answer->knownBytes = 0x77;  // The day of the week isn't given
            c->display->dateQueries++;
        }
        break;
    case SCNDISPLAY_WEATHER:
        if (Known(call, 0) && Known(call, 1) && call->values[0] == SCNDISPLAY_EVENTS_OP && call->values[1] <= 5) {
            uint32_t n = call->values[1];
            answer->known = true;
            answer->value = n == 0 ? time->events : n == 1 ? time->events == 0 : (time->events >> (n - 2)) & 1;
            c->display->eventQueries++;
        }
        break;
    case SCNDISPLAY_MAP_MODEL:
        if (!yielded && Known(call, 0) && call->values[0] < SCNDISPLAY_SLOTS) {
            PathSlots slots;
            memcpy(&slots, path, sizeof(slots));
            uint32_t bit = 1u << call->values[0];
            slots.set |= bit;
            slots.shown &= ~bit;
            slots.computed &= ~bit;
            if (!Known(call, 1))
                slots.computed |= bit;
            else if (call->values[1] != 0)
                slots.shown |= bit;
            memcpy(path, &slots, sizeof(slots));
            c->display->slotCalls++;
        }
        break;
    case SCNDISPLAY_PACKAGE:
        // As at the start of the map: no package of the program is loaded, and a load is accepted and finishes at once
        // (the programs wait for it in loops), so that the branches of the loads follow only one way
        if (Known(call, 0) && Known(call, 1) && call->values[0] == 0) {
            if (call->values[1] == 0 && Known(call, 3)) {
                char name[SCNDISPLAY_NAME_SIZE];
                ProgramString(c->program, call->values[3], name, sizeof(name));
                AddPackage(c->display, name);
                *answer = (VM_Answer) { .known = true, .value = 1 };
            } else if (call->values[1] == 2 || call->values[1] == 5)
                *answer = (VM_Answer) { .known = true, .value = 0 };
        }
        break;
    case SCNDISPLAY_ARRAY_ACCESS:
        AccessRecords(c, call, path, memory, answer);
        break;
    default:
        break;
    }
}

static void OnSettled(void* context, uint32_t thread, const uint8_t* path) {
    Context* c = context;
    if (thread >= VM_MAX_THREADS)
        return;
    PathSlots slots;
    memcpy(&slots, path, sizeof(slots));
    if (slots.set == 0)
        return;
    c->shown[thread] |= slots.shown | slots.computed;
    c->hidden[thread] |= (slots.set & ~slots.shown) | slots.computed;
    if (thread + 1 > c->lastThread)
        c->lastThread = thread + 1;
}

// The doors are opened and closed after the first frame, and only those of the area of the map where Ryo is (e.g.
// scene/01/d000 checks the hours every minute, by areas): the state of a door is the one at the end of the paths, open
// if a path opens it (the paths of the other areas leave it as it starts, closed)
static void OnEnded(void* context, uint32_t thread, const uint8_t* path) {
    (void)thread;
    Context* c = context;
    PathSlots slots;
    memcpy(&slots, path, sizeof(slots));
    for (uint32_t d = 0; d < c->display->doorCount; d++)
        c->doorStates[d] |= (uint8_t)(1 << PathDoor(&slots, d));
}

bool SCNDisplay_Run(const BC_Program* program, const SCNDisplay_Time* time, SCNDisplay* display) {
    memset(display, 0, sizeof(*display));
    Context context = { program, time, display, calloc(VM_MAX_THREADS, sizeof(uint32_t)), calloc(VM_MAX_THREADS, sizeof(uint32_t)), 0,
        -1, { 0 } };
    const SCN_Program* scn = program->scn;
    MAPINFO_DoorTable doors;
    if (MAPINFO_FindDoorTable(scn->data, scn->header.size, &doors) && doors.recordCount > 0 &&
        doors.recordsOffset >= scn->header.dataStart) {
        context.records = doors.recordsOffset - scn->header.dataStart;
        display->doorCount = doors.recordCount < SCNDISPLAY_DOORS ? doors.recordCount : SCNDISPLAY_DOORS;
    }
    VM_Environment environment = { &context, OnCall, OnSettled, .ended = OnEnded };
    VM_Result result;
    bool ok = context.shown != NULL && context.hidden != NULL && VM_RunEnvironment(program, &environment, &result);
    if (ok) {
        display->threads = result.threads;
        display->skipped = result.skipped;
        display->truncated = result.truncated;
        VM_Free(&result);
        // The last thread that shows or hides each slot
        for (uint32_t s = 0; s < SCNDISPLAY_SLOTS; s++) {
            uint32_t bit = 1u << s;
            for (uint32_t t = context.lastThread; t-- > 0;) {
                bool shown = (context.shown[t] & bit) != 0, hidden = (context.hidden[t] & bit) != 0;
                if (shown || hidden) {
                    display->slots[s] = shown && hidden ? SCNDISPLAY_VARIES : shown ? SCNDISPLAY_SHOWN : SCNDISPLAY_HIDDEN;
                    break;
                }
            }
        }
        for (uint32_t d = 0; d < display->doorCount; d++) {
            uint8_t states = context.doorStates[d];
            display->doors[d] = (states & (1 << SCNDISPLAY_DOOR_OPEN)) ? SCNDISPLAY_DOOR_OPEN :
                (states & (1 << SCNDISPLAY_DOOR_VARIES)) ? SCNDISPLAY_DOOR_VARIES :
                (states & (1 << SCNDISPLAY_DOOR_CLOSED)) ? SCNDISPLAY_DOOR_CLOSED : SCNDISPLAY_DOOR_UNSET;
        }
    }
    free(context.shown);
    free(context.hidden);
    return ok;
}

bool SCNDisplay_Loads(const SCNDisplay* display, const char* package) {
    for (uint32_t i = 0; i < display->packageCount; i++) {
        if (SameName(display->packages[i], package))
            return true;
    }
    return false;
}

int SCNDisplay_SlotOfFile(const char* name) {
    const char* base = name;
    for (const char* p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    if (toupper((unsigned char)base[0]) != 'M' || toupper((unsigned char)base[1]) != 'A' || toupper((unsigned char)base[2]) != 'P')
        return -1;
    const char* p = base + 3;
    int slot = 0, digits = 0;
    for (; isdigit((unsigned char)*p) && digits < 3; p++, digits++)
        slot = slot * 10 + (*p - '0');
    if (*p != '.' || toupper((unsigned char)p[1]) != 'M' || toupper((unsigned char)p[2]) != 'T' || p[3] != '5' || p[4] != '\0')
        return -1;
    return slot < SCNDISPLAY_SLOTS ? slot : -1;
}

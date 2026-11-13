#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formats/script/scnDisplay.h"
#include "formats/script/scnVm.h"

// The state of a path: the last display of each slot before the first yield
typedef struct {
    uint32_t    set;        // Bit s: the slot s was shown or hidden
    uint32_t    shown;      // Bit s: the last call showed it
    uint32_t    computed;   // Bit s: the last call gave a value that isn't known
} PathSlots;

typedef struct {
    const BC_Program*       program;
    const SCNDisplay_Time*  time;
    SCNDisplay*             display;
    uint32_t*               shown;      // Per thread (VM_MAX_THREADS): bit s, the slot s is shown on a settled path
    uint32_t*               hidden;     // The same, hidden
    uint32_t                lastThread; // Highest thread settled + 1
} Context;

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

static void OnCall(void* context, const VM_Call* call, bool yielded, uint8_t* path, VM_Answer* answer) {
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

bool SCNDisplay_Run(const BC_Program* program, const SCNDisplay_Time* time, SCNDisplay* display) {
    memset(display, 0, sizeof(*display));
    Context context = { program, time, display, calloc(VM_MAX_THREADS, sizeof(uint32_t)), calloc(VM_MAX_THREADS, sizeof(uint32_t)), 0 };
    VM_Environment environment = { &context, OnCall, OnSettled };
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

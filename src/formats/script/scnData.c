#include <stdlib.h>
#include <string.h>
#include "formats/script/scnData.h"
#include "formats/script/sh4Listing.h"

static const char* const g_kindNames[SD_KIND_COUNT] = {
    "package", "AFS package", "cutscene", "sequence", "wake", "sleep", "character", "sound bank", "camera", "flag read", "flag set", "byte read", "byte set", "file",
};

const char* SD_KindName(SD_Kind kind) {
    return kind < SD_KIND_COUNT ? g_kindNames[kind] : "?";
}

static bool IsIdCharacter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool SD_IsCharacterId(uint32_t value) {
    bool letter = false;
    for (int i = 0; i < 4; i++) {
        char c = (char)(value >> (i * 8));
        if (!IsIdCharacter(c))
            return false;
        letter |= c < '0' || c > '9';
    }
    return letter;
}

// An argument of a call: a constant, a string of the data, or unknown
typedef struct {
    bool        constant;
    uint32_t    value;
    const char* string;     // NULL if it isn't one
} Argument;

typedef struct {
    const SH4_Program*  sh4;
    const BC_Program*   bytecode;
    const SCN_Pair*     pair;
    const SH4_Call*     call;
    const SH4_Call*     paired;     // Bytecode call of an SH-4 one with the same ID, NULL if none
} Source;

static Argument ValueArgument(const SH4_Value* value, const SH4_Program* sh4, const BC_Program* bytecode) {
    Argument argument = { false, 0, NULL };
    if (value->kind == SH4_VALUE_CONSTANT) {
        argument.constant = true;
        argument.value = value->value;
    } else if (value->kind == SH4_VALUE_DATA) {
        argument.string = sh4 != NULL ? SH4_DataString(sh4, value->value) : BC_DataString(bytecode, value->value);
    }
    return argument;
}

static Argument GetArgument(const Source* source, uint32_t index) {
    Argument argument = { false, 0, NULL };
    if (index < source->call->argumentCount)
        argument = ValueArgument(&source->call->arguments[index], source->sh4, source->sh4 != NULL ? NULL : source->bytecode);
    if (!argument.constant && argument.string == NULL && source->paired != NULL && index < source->paired->argumentCount)
        argument = ValueArgument(&source->paired->arguments[index], NULL, source->bytecode);
    return argument;
}

static uint32_t ArgumentCount(const Source* source) {
    uint32_t count = source->call->argumentCount;
    if (source->paired != NULL && source->paired->argumentCount > count)
        count = source->paired->argumentCount;
    return count;
}

static bool IsConstant(Argument argument, uint32_t value) {
    return argument.constant && argument.value == value;
}

// A file name: a name and an extension of 2 to 4 letters or digits, without spaces or formats
static bool IsFileName(const char* string) {
    size_t length = strlen(string);
    const char* dot = strrchr(string, '.');
    if (length >= SD_NAME_SIZE || dot == NULL || dot == string || length - (size_t)(dot - string) - 1 < 2 ||
        length - (size_t)(dot - string) - 1 > 4)
        return false;
    for (const char* c = string; *c != '\0'; c++) {
        if (c != dot && !IsIdCharacter(*c) && *c != '/' && *c != '-' && *c != '$' && *c != '@' && *c != '#')
            return false;
    }
    for (const char* c = dot + 1; *c != '\0'; c++) {
        if (!IsIdCharacter(*c) || *c == '_')
            return false;
    }
    return true;
}

static bool Add(SD_Data* data, uint32_t* capacity, const Source* source, SD_Kind kind, int32_t number, const char* name,
    const char* folder) {
    if (data->count == *capacity) {
        uint32_t newCapacity = *capacity == 0 ? 64 : *capacity * 2;
        SD_Reference* references = realloc(data->references, newCapacity * sizeof(SD_Reference));
        if (references == NULL)
            return false;
        data->references = references;
        *capacity = newCapacity;
    }
    SD_Reference* reference = &data->references[data->count++];
    memset(reference, 0, sizeof(*reference));
    reference->kind = (uint8_t)kind;
    reference->game = (int16_t)source->call->id;
    reference->function = source->call->function;
    reference->address = source->call->address;
    reference->number = number;
    reference->value = SD_UNKNOWN;
    if (name != NULL)
        snprintf(reference->name, sizeof(reference->name), "%s", name);
    if (folder != NULL)
        snprintf(reference->folder, sizeof(reference->folder), "%s", folder);
    return true;
}

// The 4 characters of an ID, in the order of its bytes in memory
static void IdText(uint32_t id, char text[5]) {
    for (int i = 0; i < 4; i++)
        text[i] = (char)(id >> (i * 8));
    text[4] = '\0';
}

static bool AddId(SD_Data* data, uint32_t* capacity, const Source* source, SD_Kind kind, uint32_t id) {
    char name[5];
    IdText(id, name);
    return Add(data, capacity, source, kind, -1, name, NULL);
}

// The references of a call to the game
// With generic false, only the references of the functions read by their ID (not the characters and files of any call)
static bool ReadCall(SD_Data* data, uint32_t* capacity, const Source* source, const bool* characterFunctions, bool generic) {
    int32_t id = source->call->id;
    uint32_t count = ArgumentCount(source);
    Argument a[SH4_MAX_ARGUMENTS];
    for (uint32_t i = 0; i < SH4_MAX_ARGUMENTS; i++)
        a[i] = i < count ? GetArgument(source, i) : (Argument) { false, 0, NULL };
    bool used[SH4_MAX_ARGUMENTS] = { false };   // Strings read as something else than a file
    bool ok = true;
    if (id == SD_GAME_PACKAGE && IsConstant(a[0], 0) && IsConstant(a[1], 0) && a[3].string != NULL) {
        ok &= Add(data, capacity, source, SD_PACKAGE, -1, a[3].string, a[2].string);
        used[2] = used[3] = true;
    } else if (id == SD_GAME_PACKAGE && IsConstant(a[0], 0) && IsConstant(a[1], 8) && a[2].string != NULL && a[3].constant) {
        ok &= Add(data, capacity, source, SD_AFS_PACKAGE, (int32_t)a[3].value, a[2].string, NULL);
        used[2] = true;
    } else if (id == SD_GAME_PACKAGE) {
        for (uint32_t i = 0; i < count; i++)
            used[i] = true;     // Package names
    } else if (id == SD_GAME_CUTSCENE && a[0].constant && a[0].value < SD_CUTSCENE_SLOTS) {
        ok &= Add(data, capacity, source, SD_CUTSCENE, (int32_t)a[0].value, NULL, NULL);
    } else if (id == SD_GAME_SEQUENCE && IsConstant(a[0], 0) && a[1].constant && a[1].value < SD_CUTSCENE_SLOTS &&
        a[2].string != NULL && a[3].string != NULL) {
        ok &= Add(data, capacity, source, SD_SEQUENCE, (int32_t)a[1].value, a[2].string, a[3].string);
        used[2] = used[3] = true;
    } else if (id == SD_GAME_ACTIVE && a[0].constant && SD_IsCharacterId(a[0].value) && a[1].constant && a[1].value <= 1) {
        ok &= AddId(data, capacity, source, a[1].value == 1 ? SD_WAKE : SD_SLEEP, a[0].value);
    } else if (id == SD_GAME_SOUND_BANK && IsConstant(a[0], 0) && a[1].constant && a[2].string != NULL) {
        ok &= Add(data, capacity, source, SD_SOUND_BANK, (int32_t)a[1].value, a[2].string, NULL);
        used[2] = true;
    } else if (id == SD_GAME_FLAGS && a[0].constant && a[1].constant && (a[0].value == 11 || a[0].value == 12 ||
        a[0].value == 15 || a[0].value == 16) && a[1].value < (a[0].value <= 12 ? SD_FLAGS : SD_FLAG_BYTES)) {
        static const SD_Kind kinds[] = { SD_FLAG_READ, SD_FLAG_SET, 0, 0, SD_BYTE_READ, SD_BYTE_SET };
        SD_Kind kind = kinds[a[0].value - 11];
        ok &= Add(data, capacity, source, kind, (int32_t)a[1].value, NULL, NULL);
        if (ok && (kind == SD_FLAG_SET || kind == SD_BYTE_SET) && a[2].constant)
            data->references[data->count - 1].value = (int32_t)a[2].value;
    } else if (id == SD_GAME_CAMERA && a[0].constant && a[0].value <= 9999) {
        char first[5] = "", second[5] = "";
        if (a[1].constant && SD_IsCharacterId(a[1].value))
            IdText(a[1].value, first);
        if (a[2].constant && SD_IsCharacterId(a[2].value))
            IdText(a[2].value, second);
        ok &= Add(data, capacity, source, SD_CAMERA, (int32_t)a[0].value, first, second);
    } else if (id == SD_GAME_SOUND_BANKS) {
        for (uint32_t i = 0; i < count && i < 8; i++) {
            if (a[i].string != NULL)
                ok &= Add(data, capacity, source, SD_SOUND_BANK, (int32_t)i, a[i].string, NULL);
            used[i] = true;
        }
    }
    for (uint32_t i = 0; i < count && generic; i++) {
        if (a[i].constant && SD_IsCharacterId(a[i].value) && id >= 0 && id < SD_GAME_FUNCTIONS &&
            (characterFunctions == NULL || characterFunctions[id]))
            ok &= AddId(data, capacity, source, SD_CHARACTER, a[i].value);
        else if (!used[i] && a[i].string != NULL && IsFileName(a[i].string))
            ok &= Add(data, capacity, source, SD_FILE, -1, a[i].string, NULL);
    }
    return ok;
}

// The calls to the game whose arguments are parameters of their function: the values are taken from the calls of the
// program to that function, and from the calls to those functions while they are parameters too

#define SD_MAX_RESOLVED 512     // Instances of a call to the game
#define SD_MAX_VISITS   20000   // Calls followed from a call to the game

typedef struct {
    SD_Data*            data;
    uint32_t*           capacity;
    const SH4_Program*  sh4;
    const BC_Program*   bytecode;
    const int32_t*      sh4Calls;   // Per call of the bytecode: the paired SH-4 call, -1 if none. NULL without SH-4
    const uint32_t*     firstCaller; // Per function of the bytecode: its first local caller in callers (CSR)
    const uint32_t*     callers;    // Calls of the bytecode, by called function
    const SH4_Call*     game;
    uint32_t            via;
    uint32_t            resolved;
    uint32_t            visits;
    bool                ok;
} Indirect;

static bool IsIndirectGame(int32_t id) {
    return id == SD_GAME_PACKAGE || id == SD_GAME_SEQUENCE || id == SD_GAME_CUTSCENE || id == SD_GAME_ACTIVE ||
        id == SD_GAME_CAMERA || id == SD_GAME_SOUND_BANK || id == SD_GAME_SOUND_BANKS || id == SD_GAME_FLAGS;
}

static bool HasParameter(const SH4_Value* values, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (values[i].kind == SH4_VALUE_PARAMETER)
            return true;
    }
    return false;
}

// values: the arguments of the call to the game, those of kind SH4_VALUE_PARAMETER being parameters of function, which
// site calls (the call to the game itself at first)
static void Resolve(Indirect* context, const SH4_Value* values, uint32_t function, const SH4_Call* site, int depth) {
    const SH4_Call* game = context->game;
    if (context->visits++ >= SD_MAX_VISITS || !context->ok)
        return;
    if (!HasParameter(values, game->argumentCount)) {
        if (site == game || context->resolved >= SD_MAX_RESOLVED)
            return;
        context->resolved++;
        SH4_Call call = *game;
        memcpy(call.arguments, values, sizeof(call.arguments));
        // The call that gives the values, in the SH-4 program if there is one
        call.address = site->address;
        call.function = site->function;
        if (context->sh4Calls != NULL) {
            int32_t s = context->sh4Calls[site - context->bytecode->calls];
            if (s < 0)
                return;
            call.address = context->sh4->calls[s].address;
            call.function = context->sh4->calls[s].function;
        }
        Source source = { NULL, context->bytecode, NULL, &call, NULL };
        uint32_t first = context->data->count;
        context->ok &= ReadCall(context->data, context->capacity, &source, NULL, false);
        for (uint32_t r = first; r < context->data->count; r++)
            context->data->references[r].via = context->via;
        return;
    }
    if (depth == SD_MAX_DEPTH)
        return;
    for (uint32_t k = context->firstCaller[function]; k < context->firstCaller[function + 1]; k++) {
        const SH4_Call* caller = &context->bytecode->calls[context->callers[k]];
        SH4_Value next[SH4_MAX_ARGUMENTS];
        for (uint32_t i = 0; i < SH4_MAX_ARGUMENTS; i++) {
            next[i] = values[i];
            if (values[i].kind == SH4_VALUE_PARAMETER) {
                uint32_t parameter = values[i].value;
                next[i] = parameter < caller->argumentCount ? caller->arguments[parameter] : (SH4_Value) { 0 };
            }
        }
        Resolve(context, next, caller->function, caller, depth + 1);
    }
}

static bool ReadIndirect(SD_Data* data, uint32_t* capacity, const SH4_Program* sh4, const BC_Program* bytecode,
    const SCN_Pair* pair) {
    uint32_t functions = bytecode->functionCount, calls = bytecode->callCount;
    uint32_t* firstCaller = calloc(functions + 2, sizeof(uint32_t));
    uint32_t* callers = calloc(calls + 1, sizeof(uint32_t));
    uint32_t* targets = calloc(calls + 1, sizeof(uint32_t));    // Called function + 1 of each local call, 0 if none
    int32_t* sh4Calls = sh4 != NULL ? malloc((calls + 1) * sizeof(int32_t)) : NULL;
    bool ok = firstCaller != NULL && callers != NULL && targets != NULL && (sh4 == NULL || sh4Calls != NULL);
    for (uint32_t c = 0; c < calls && ok; c++) {
        const SH4_Call* call = &bytecode->calls[c];
        int f = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(bytecode, call->target) : -1;
        if (f >= 0 && bytecode->functions[f] == call->target) {
            targets[c] = (uint32_t)f + 1;
            firstCaller[f + 1]++;
        }
    }
    for (uint32_t f = 0; f < functions && ok; f++)
        firstCaller[f + 1] += firstCaller[f];
    uint32_t* filled = ok ? calloc(functions + 1, sizeof(uint32_t)) : NULL;    // Callers of each function placed
    ok &= filled != NULL;
    for (uint32_t c = 0; c < calls && ok; c++) {
        if (targets[c] > 0) {
            uint32_t f = targets[c] - 1;
            callers[firstCaller[f] + filled[f]++] = c;
        }
    }
    free(filled);
    if (ok && sh4Calls != NULL) {
        for (uint32_t c = 0; c < calls; c++)
            sh4Calls[c] = -1;
        for (uint32_t s = 0; s < pair->callCount; s++) {
            if (pair->calls[s] >= 0)
                sh4Calls[pair->calls[s]] = (int32_t)s;
        }
    }
    Indirect context = { data, capacity, sh4, bytecode, sh4Calls, firstCaller, callers, NULL, 0, 0, 0, ok };
    for (uint32_t c = 0; c < calls && context.ok; c++) {
        const SH4_Call* call = &bytecode->calls[c];
        if (call->kind != SH4_CALL_GAME || !IsIndirectGame(call->id) || !HasParameter(call->arguments, call->argumentCount))
            continue;
        context.game = call;
        context.via = call->address;
        if (sh4Calls != NULL)
            context.via = sh4Calls[c] >= 0 ? sh4->calls[sh4Calls[c]].address : 0;
        context.resolved = context.visits = 0;
        if (sh4Calls == NULL || sh4Calls[c] >= 0)
            Resolve(&context, call->arguments, call->function, call, 0);
    }
    free(firstCaller);
    free(callers);
    free(targets);
    free(sh4Calls);
    return context.ok;
}

bool SD_ReadProgram(SD_Data* data, const SH4_Program* sh4, const BC_Program* bytecode, const SCN_Pair* pair,
    const bool* characterFunctions) {
    memset(data, 0, sizeof(*data));
    uint32_t capacity = 0;
    const SH4_Call* calls = sh4 != NULL ? sh4->calls : bytecode != NULL ? bytecode->calls : NULL;
    uint32_t callCount = sh4 != NULL ? sh4->callCount : bytecode != NULL ? bytecode->callCount : 0;
    for (uint32_t c = 0; c < callCount; c++) {
        if (calls[c].kind != SH4_CALL_GAME || calls[c].id < 0)
            continue;
        Source source = { sh4, bytecode, pair, &calls[c], NULL };
        if (sh4 != NULL && bytecode != NULL && pair != NULL) {
            const SH4_Call* paired = SCN_PairedCall(pair, bytecode, c);
            if (paired != NULL && paired->id == calls[c].id)
                source.paired = paired;
        } else if (sh4 == NULL) {
            source.bytecode = bytecode;
        }
        if (!ReadCall(data, &capacity, &source, characterFunctions, true)) {
            SD_FreeData(data);
            return false;
        }
    }
    if (bytecode != NULL && (sh4 == NULL || pair != NULL) && !ReadIndirect(data, &capacity, sh4, bytecode, pair)) {
        SD_FreeData(data);
        return false;
    }
    return true;
}

void SD_FreeData(SD_Data* data) {
    free(data->references);
    memset(data, 0, sizeof(*data));
}

#include <stdlib.h>
#include <string.h>
#include "formats/script/scnData.h"
#include "formats/script/sh4Listing.h"

static const char* const g_kindNames[SD_KIND_COUNT] = {
    "package", "AFS package", "cutscene", "sequence", "wake", "sleep", "character", "sound bank", "camera", "file",
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
static bool ReadCall(SD_Data* data, uint32_t* capacity, const Source* source, const bool* characterFunctions) {
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
    for (uint32_t i = 0; i < count; i++) {
        if (a[i].constant && SD_IsCharacterId(a[i].value) && id >= 0 && id < SD_GAME_FUNCTIONS &&
            (characterFunctions == NULL || characterFunctions[id]))
            ok &= AddId(data, capacity, source, SD_CHARACTER, a[i].value);
        else if (!used[i] && a[i].string != NULL && IsFileName(a[i].string))
            ok &= Add(data, capacity, source, SD_FILE, -1, a[i].string, NULL);
    }
    return ok;
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
        if (!ReadCall(data, &capacity, &source, characterFunctions)) {
            SD_FreeData(data);
            return false;
        }
    }
    return true;
}

void SD_FreeData(SD_Data* data) {
    free(data->references);
    memset(data, 0, sizeof(*data));
}

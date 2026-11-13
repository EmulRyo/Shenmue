#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include "app/gameDB.h"
#include "core/file.h"
#include "formats/archive/pks.h"
#include "formats/script/scn.h"
#include "formats/script/scnBytecode.h"
#include "formats/script/scnPair.h"
#include "formats/script/sh4Program.h"
#include "views/mapProgram.h"

#define MAPPROGRAM_PATH_SIZE 1200

// Splits the last parts of a path: parts[0] is the last one. Returns false if it has fewer parts
static bool SplitPath(const char* path, char* root, size_t rootSize, const char** parts, int count) {
    snprintf(root, rootSize, "%s", path);
    for (int p = 0; p < count; p++) {
        char* separator = strrchr(root, '/');
        char* backslash = strrchr(root, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
        if (separator == NULL)
            return false;
        *separator = '\0';
        parts[p] = separator + 1;
    }
    return true;
}

static void ToLower(char* text) {
    for (; *text != '\0'; text++)
        *text = (char)tolower((unsigned char)*text);
}

bool MapProgram_ScnPath(const char* mapinfoPath, char* name, size_t nameSize, char* path, size_t pathSize) {
    // The last 4 parts of the path: scene, NN, <map>, mapinfo.bin
    char root[MAPPROGRAM_PATH_SIZE];
    const char* parts[4] = { NULL };
    if (!SplitPath(mapinfoPath, root, sizeof(root), parts, 4) || strlen(parts[2]) != 2 || strlen(parts[1]) >= 16)
        return false;
    snprintf(name, nameSize, "act%s_%s.scn", parts[2], parts[1]);
    ToLower(name);
    snprintf(path, pathSize, "%s/scenario/%s", root, name);
    return true;
}

bool MapProgram_Emulate(SD_Data* data, const SH4_Program* sh4, const BC_Program* bytecode, const SCN_Pair* pair) {
    VM_Result emulation;
    if (!VM_Run(bytecode, &emulation))
        return false;
    printf("[MapProgram]: emulation: %u threads (%u not run), %llu instructions, %u calls\n", emulation.threads,
        emulation.skipped, (unsigned long long)emulation.steps, emulation.callCount);
    bool ok = SD_ReadEmulation(data, sh4, bytecode, pair, &emulation);
    VM_Free(&emulation);
    return ok;
}

bool MapProgram_Read(const char* mapinfoPath, const MAPINFO_File* file, SD_Data* data) {
    memset(data, 0, sizeof(*data));
    const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(file, "SCN3");
    SCN_Program scn;
    if (chunk == NULL || !SCN_Read(file->data + chunk->offset, chunk->size, &scn))
        return false;
    const bool* characterFunctions = GameDB_CharacterFunctions();
    bool read = false;
    if (!SCN_IsSH4(&scn)) {
        BC_Program bytecode;
        if (BC_AnalyzeProgram(&bytecode, &scn)) {
            read = SD_ReadProgram(data, NULL, &bytecode, NULL, characterFunctions) &&
                MapProgram_Emulate(data, NULL, &bytecode, NULL);
            BC_FreeProgram(&bytecode);
        }
        return read;
    }
    SH4_Program sh4;
    if (!SH4_AnalyzeProgram(&sh4, scn.data, scn.header.codeStart, scn.header.dataStart, scn.header.end, scn.header.entry))
        return false;
    // With the arguments of the bytecode of its SCN file if it is found
    char name[64], path[MAPPROGRAM_PATH_SIZE];
    size_t size = 0;
    uint8_t* scnData = MapProgram_ScnPath(mapinfoPath, name, sizeof(name), path, sizeof(path)) && FileExists(path) ?
        File_LoadToMemoryDecompressed(path, &size) : NULL;
    SCN_Program pairScn;
    BC_Program bytecode;
    SCN_Pair pair;
    if (scnData != NULL && SCN_Read(scnData, size, &pairScn) && BC_AnalyzeProgram(&bytecode, &pairScn)) {
        if (SCN_PairPrograms(&pair, &sh4, &bytecode)) {
            read = SD_ReadProgram(data, &sh4, &bytecode, &pair, characterFunctions) &&
                MapProgram_Emulate(data, &sh4, &bytecode, &pair);
            SCN_FreePair(&pair);
        }
        BC_FreeProgram(&bytecode);
    }
    if (!read)
        read = SD_ReadProgram(data, &sh4, NULL, NULL, characterFunctions);
    free(scnData);
    SH4_FreeProgram(&sh4);
    return read;
}

// The name without its extension, in uppercase (e.g. "seqdata1.bin": "SEQDATA1")
static void BaseName(const char* name, char* base, size_t size) {
    size_t i = 0;
    for (; name[i] != '\0' && name[i] != '.' && i + 1 < size; i++)
        base[i] = (char)toupper((unsigned char)name[i]);
    base[i] = '\0';
}

static bool SameBase(const char* filename, const char* base) {
    char other[64];
    BaseName(filename, other, sizeof(other));
    return strcmp(other, base) == 0;
}

// The AUTH file of the base name in a PKS file
static uint8_t* LoadPacked(const char* pks, const char* base, size_t* size) {
    IPAC_FileList list = { 0 };
    uint8_t* data = NULL;
    if (FileExists(pks) && PKS_LoadFiles(pks, &list)) {
        for (uint32_t i = 0; i < list.count && data == NULL; i++) {
            const IPAC_File* file = &list.files[i];
            if (strcmp(file->type, "AUTH") == 0 && SameBase(file->filename, base) && (data = malloc(file->size)) != NULL) {
                memcpy(data, file->data, file->size);
                *size = file->size;
            }
        }
    }
    IPAC_FreeFiles(&list);
    return data;
}

// The AUTH file of the base name in the extracted folder of a package
static uint8_t* LoadExtracted(const char* folder, const char* base, size_t* size) {
    if (!DirectoryExists(folder))
        return NULL;
    FilePathList files = LoadDirectoryFiles(folder);
    uint8_t* data = NULL;
    for (uint32_t i = 0; i < files.count && data == NULL; i++) {
        if (IsFileExtension(files.paths[i], ".auth") && SameBase(GetFileName(files.paths[i]), base))
            data = File_LoadToMemoryDecompressed(files.paths[i], size);
    }
    UnloadDirectoryFiles(files);
    return data;
}

uint8_t* MapProgram_LoadSequence(const char* mapinfoPath, const SD_Reference* sequence, size_t* size) {
    char root[MAPPROGRAM_PATH_SIZE], path[MAPPROGRAM_PATH_SIZE], base[64], lower[SD_NAME_SIZE * 2];
    const char* parts[4] = { NULL };
    if (sequence->kind != SD_SEQUENCE || !SplitPath(mapinfoPath, root, sizeof(root), parts, 4))
        return NULL;
    BaseName(sequence->name, base, sizeof(base));
    if (strchr(sequence->folder, '/') != NULL) {
        // A file of a scene folder, from the folder that contains scene/
        const char* folder = sequence->folder;
        while (*folder == '/')
            folder++;
        snprintf(lower, sizeof(lower), "%s/%s", folder, sequence->name);
        ToLower(lower);
        snprintf(path, sizeof(path), "%s/%s", root, lower);
        return FileExists(path) ? File_LoadToMemoryDecompressed(path, size) : NULL;
    }
    // A package of the folder of the map
    snprintf(lower, sizeof(lower), "%s", sequence->folder);
    ToLower(lower);
    char mapFolder[MAPPROGRAM_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(mapinfoPath));
    snprintf(path, sizeof(path), "%s/%s", mapFolder, lower);
    uint8_t* data = LoadExtracted(path, base, size);
    if (data == NULL) {
        snprintf(path, sizeof(path), "%s/%s.pks", mapFolder, lower);
        data = LoadPacked(path, base, size);
    }
    return data;
}

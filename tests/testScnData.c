#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include "core/file.h"
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/scnData.h"
#include "test.h"

// What the scene programs of the 136 maps (the SH-4 code with the arguments of its SCN file, or the bytecode) do with
// their map: the packages they load are PKF/PKS files of the scene folders, the cutscenes they play are TRCK chunks of
// the map or AUTH files they load in the table of cutscenes, and the characters they wake, put to sleep or use are
// defined in the files of the map, and the cameras they show are those of the ECAM chunk

#define LINE_SIZE 1024

// The functions of the game that look up characters, from the column character of the database
static bool ReadCharacterFunctions(bool* functions) {
    FILE* file = fopen(SHENMUE_RESOURCES "/Shenmue1GameFunctions.tsv", "r");
    if (file == NULL)
        return false;
    char line[LINE_SIZE];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        size_t length = strlen(line);
        int id = (int)strtol(line, NULL, 16);
        if (length > 4 && strcmp(line + length - 4, "\tyes") == 0 && id >= 0 && id < SD_GAME_FUNCTIONS)
            functions[id] = true;
    }
    fclose(file);
    return true;
}

typedef struct {
    uint64_t    kinds[SD_KIND_COUNT];
    uint64_t    packagesFound;
    uint64_t    sequencesFound;
    uint64_t    cutscenesOfMap;     // Slot of a TRCK chunk of the map
    uint64_t    cutscenesLoaded;    // Slot loaded by game_13E in the same program
    uint64_t    idsFound;           // Wake, sleep and character IDs found in the files of the map
    uint64_t    idsMissing;
    uint64_t    camerasFound;       // Cameras of the ECAM chunk of the map
    uint64_t    relativeCameras;    // With the flag 0x200 (relative)
    uint64_t    relativeWithCharacter;  // Relative, shown with a character
    uint64_t    otherWithCharacter;     // Not relative, shown with a character
} DataStats;

// The folder of the scene of a folder argument ("scene/01/JOMO", "/scene/01/D000/"), lowercase. The map folder if it
// isn't one
static void FolderPath(const TestContext* context, const CorpusFile* map, const char* folder, char* path, size_t size) {
    while (*folder == '/')
        folder++;
    if (strncmp(folder, "scene/", 6) != 0 && strncmp(folder, "SCENE/", 6) != 0) {
        snprintf(path, size, "%s/scene/%02d/%s", context->content, map->disc, map->map);
        return;
    }
    int length = snprintf(path, size, "%s/", context->content);
    for (const char* c = folder; *c != '\0' && length + 1 < (int)size; c++)
        path[length++] = (char)tolower((unsigned char)*c);
    path[length] = '\0';
    while (length > 0 && path[length - 1] == '/')
        path[--length] = '\0';
}

static bool PackageExists(const char* folder, const char* name) {
    char lower[SD_NAME_SIZE];
    size_t i = 0;
    for (; name[i] != '\0' && i + 1 < sizeof(lower); i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = '\0';
    return FileExists(TextFormat("%s/%s.pkf", folder, lower)) || FileExists(TextFormat("%s/%s.pks", folder, lower));
}

// The bytes of the mapinfo.bin and of the PKF files of the folder of the map, where the IDs of the characters are
static uint8_t* LoadMapBytes(const char* folder, const uint8_t* mapinfo, size_t mapinfoSize, size_t* size) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".pkf", false);
    uint8_t* bytes = malloc(mapinfoSize);
    if (bytes != NULL)
        memcpy(bytes, mapinfo, mapinfoSize);
    *size = mapinfoSize;
    for (uint32_t i = 0; i < files.count && bytes != NULL; i++) {
        size_t fileSize;
        uint8_t* data = File_LoadToMemoryDecompressed(files.paths[i], &fileSize);
        uint8_t* grown = data != NULL ? realloc(bytes, *size + fileSize) : bytes;
        if (data != NULL && grown != NULL) {
            memcpy(grown + *size, data, fileSize);
            *size += fileSize;
        }
        bytes = grown;
        free(data);
    }
    UnloadDirectoryFiles(files);
    return bytes;
}

static bool ContainsId(const uint8_t* bytes, size_t size, const char* id) {
    size_t length = strlen(id);
    for (size_t i = 0; i + length <= size; i++) {
        if (bytes[i] == (uint8_t)id[0] && memcmp(bytes + i, id, length) == 0)
            return true;
    }
    return false;
}

static bool IsChecked(char (*checked)[5], const uint32_t* count, const char* id) {
    for (uint32_t i = 0; checked != NULL && i < *count; i++) {
        if (strcmp(checked[i], id) == 0)
            return true;
    }
    return false;
}

static void AddChecked(char (*checked)[5], uint32_t* count, const char* id) {
    if (checked != NULL)
        snprintf(checked[(*count)++], 5, "%s", id);
}

static void CheckData(const TestContext* context, const CorpusFile* map, const MAPINFO_File* file, const SD_Data* data,
    DataStats* stats) {
    char folder[CORPUS_PATH_SIZE];
    FolderPath(context, map, "", folder, sizeof(folder));
    size_t size;
    uint8_t* bytes = LoadMapBytes(folder, file->data, file->size, &size);
    bool loaded[SD_CUTSCENE_SLOTS] = { false };
    char (*checked)[5] = calloc(data->count + 1, 5);  // IDs found in the files, each one searched once
    uint32_t checkedCount = 0;
    for (uint32_t i = 0; i < data->count; i++) {
        if (data->references[i].kind == SD_SEQUENCE)
            loaded[data->references[i].number] = true;
    }
    for (uint32_t i = 0; i < data->count; i++) {
        const SD_Reference* reference = &data->references[i];
        stats->kinds[reference->kind]++;
        char path[CORPUS_PATH_SIZE];
        switch (reference->kind) {
        case SD_PACKAGE:
            FolderPath(context, map, reference->folder, path, sizeof(path));
            stats->packagesFound += PackageExists(path, reference->name);
            break;
        case SD_SEQUENCE:
            // A package of the folder of the map, or a folder with the file (e.g. "SCENE/01/DNOZ", seqdata2.bin)
            FolderPath(context, map, reference->folder, path, sizeof(path));
            if (strchr(reference->folder, '/') == NULL)
                stats->sequencesFound += PackageExists(folder, reference->folder);
            else
                stats->sequencesFound += FileExists(TextFormat("%s/%s", path, TextToLower(reference->name)));
            break;
        case SD_CAMERA: {
            const MAPINFO_Camera* camera = NULL;
            char id[8];
            snprintf(id, sizeof(id), "%04d", reference->number);
            for (uint32_t c = 0; c < file->cameraCount && camera == NULL; c++)
                camera = strcmp(file->cameras[c].id, id) == 0 ? &file->cameras[c] : NULL;
            if (camera != NULL) {
                bool relative = (camera->flags & MAPINFO_CAMERA_RELATIVE) != 0, character = reference->name[0] != '\0';
                stats->camerasFound++;
                stats->relativeCameras += relative;
                stats->relativeWithCharacter += relative && character;
                stats->otherWithCharacter += !relative && character;
            } else if (stats->kinds[SD_CAMERA] - stats->camerasFound < 5) {
                Test_Info("%s: camera %s not in the ECAM chunk", map->name, id);
            }
            break;
        }
        case SD_CUTSCENE:
            stats->cutscenesOfMap += (uint32_t)reference->number < file->cutsceneCount;
            stats->cutscenesLoaded += (uint32_t)reference->number >= file->cutsceneCount && loaded[reference->number];
            break;
        case SD_WAKE:
        case SD_SLEEP:
        case SD_CHARACTER:
            if (IsChecked(checked, &checkedCount, reference->name)) {
                stats->idsFound++;
            } else if (bytes != NULL && ContainsId(bytes, size, reference->name)) {
                AddChecked(checked, &checkedCount, reference->name);
                stats->idsFound++;
            } else {
                if (stats->idsMissing < 5)
                    CHECK(false, "%s: character %s of the game function 0x%X not in the files of the map", map->name,
                        reference->name, reference->game);
                stats->idsMissing++;
            }
            break;
        default:
            break;
        }
    }
    free(checked);
    free(bytes);
}

void Test_ScnData(const TestContext* context) {
    bool characterFunctions[SD_GAME_FUNCTIONS] = { false };
    SetTraceLogLevel(LOG_WARNING);  // The decompression of the PKF files
    CHECK(ReadCharacterFunctions(characterFunctions), "the database of the functions of the game can't be read");
    CorpusList maps = Corpus_MapinfoFiles(context->content);
    DataStats stats = { 0 };
    uint32_t programs = 0;
    for (uint32_t i = 0; i < maps.count; i++) {
        const CorpusFile* map = &maps.files[i];
        MAPINFO_File file;
        if (!MAPINFO_Load(map->path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program scn;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &scn)) {
            MAPINFO_Free(&file);
            continue;
        }
        SD_Data data = { 0 };
        bool read = false;
        if (SCN_IsSH4(&scn)) {
            // The SH-4 code, with the arguments of the bytecode of its SCN file
            SH4_Program sh4;
            if (SH4_AnalyzeProgram(&sh4, scn.data, scn.header.codeStart, scn.header.dataStart, scn.header.end,
                scn.header.entry)) {
                CorpusFile scnFile = *map;
                snprintf(scnFile.name, sizeof(scnFile.name), "scenario/act%02d_%s.scn", map->disc, map->map);
                snprintf(scnFile.path, sizeof(scnFile.path), "%s/%s", context->content, scnFile.name);
                uint32_t size;
                uint8_t* scnData = Corpus_Load(&scnFile, &size);
                SCN_Program scn2;
                BC_Program bytecode;
                SCN_Pair pair;
                if (scnData != NULL && SCN_Read(scnData, size, &scn2) && BC_AnalyzeProgram(&bytecode, &scn2)) {
                    if (SCN_PairPrograms(&pair, &sh4, &bytecode)) {
                        read = SD_ReadProgram(&data, &sh4, &bytecode, &pair, characterFunctions);
                        SCN_FreePair(&pair);
                    }
                    BC_FreeProgram(&bytecode);
                }
                free(scnData);
                SH4_FreeProgram(&sh4);
            }
        } else {
            BC_Program bytecode;
            if (BC_AnalyzeProgram(&bytecode, &scn)) {
                read = SD_ReadProgram(&data, NULL, &bytecode, NULL, characterFunctions);
                BC_FreeProgram(&bytecode);
            }
        }
        CHECK(read, "%s: the data of the program can't be read", map->name);
        if (read) {
            programs++;
            CheckData(context, map, &file, &data, &stats);
            SD_FreeData(&data);
        }
        MAPINFO_Free(&file);
    }
    Corpus_Free(&maps);
    CHECK_EQ(programs, 136, "programs read");
    CHECK_EQ(stats.kinds[SD_PACKAGE], 201, "packages loaded");
    CHECK_EQ(stats.packagesFound, 179, "packages loaded found in their folder");
    CHECK_EQ(stats.kinds[SD_SEQUENCE], 204, "AUTH files loaded in the table of cutscenes");
    CHECK_EQ(stats.sequencesFound, 184, "AUTH files whose package or file is found");
    CHECK_EQ(stats.kinds[SD_CUTSCENE], 333, "cutscenes played");
    CHECK_EQ(stats.cutscenesOfMap + stats.cutscenesLoaded, 270, "cutscenes played of the map or loaded by the program");
    CHECK_EQ(stats.kinds[SD_WAKE], 1025, "characters woken");
    CHECK_EQ(stats.kinds[SD_SLEEP], 1445, "characters put to sleep");
    CHECK_EQ(stats.idsMissing, 0, "character IDs not found in the files of the map");
    CHECK_EQ(stats.kinds[SD_CAMERA], 3011, "cameras shown");
    // The 4 others are the camera 0 of scene/03/mfsy, which has none
    CHECK_EQ(stats.camerasFound, 3007, "cameras shown that are in the ECAM chunk of the map");
    CHECK_EQ(stats.relativeWithCharacter, 963, "relative cameras shown with a character");
    Test_Info("%llu cameras shown (%llu in the ECAM chunk), %llu relative (%llu with a character), %llu not relative with "
        "a character", (unsigned long long)stats.kinds[SD_CAMERA], (unsigned long long)stats.camerasFound,
        (unsigned long long)stats.relativeCameras, (unsigned long long)stats.relativeWithCharacter,
        (unsigned long long)stats.otherWithCharacter);
    Test_Info("%u programs: %llu packages loaded (%llu found), %llu AUTH files loaded in the table of cutscenes (%llu "
        "found), %llu cutscenes played (%llu of the map, %llu loaded)", programs,
        (unsigned long long)stats.kinds[SD_PACKAGE], (unsigned long long)stats.packagesFound,
        (unsigned long long)stats.kinds[SD_SEQUENCE], (unsigned long long)stats.sequencesFound,
        (unsigned long long)stats.kinds[SD_CUTSCENE], (unsigned long long)stats.cutscenesOfMap,
        (unsigned long long)stats.cutscenesLoaded);
    Test_Info("characters: %llu woken, %llu put to sleep, %llu used; %llu sound banks, %llu other files",
        (unsigned long long)stats.kinds[SD_WAKE], (unsigned long long)stats.kinds[SD_SLEEP],
        (unsigned long long)stats.kinds[SD_CHARACTER], (unsigned long long)stats.kinds[SD_SOUND_BANK],
        (unsigned long long)stats.kinds[SD_FILE]);
}

// The items of the tree of the view of the maps and their texts: the information, the cutscenes, the cutscenes that the
// program loads and the flags of the game that it uses
#include "views/mapinfo/mapinfoViewInternal.h"

static const char* g_shapeTypes[] = { "point", "segment", "polyline", "circle", "triangle", "rectangle", "polygon" };

static void AddItem(ItemKind kind, uint32_t index, const char* text, bool failed) {
    uint32_t item = g_map.itemCount++;
    g_map.items[item] = (Item) { kind, index };
    snprintf(g_map.labels[item], MAPINFOVIEW_LABEL_SIZE, "%s", text);
    g_map.failedItems[item] = failed;
    g_map.visibleItems[item] = true;
}

// The references of the program of the map (g_map.program) of a kind, with a number (any if negative) and a name (any
// if NULL), the names compared ignoring the case
static bool ProgramMatches(const SD_Reference* reference, SD_Kind kind, int32_t number, const char* name) {
    return reference->kind == kind && (number < 0 || reference->number == number) &&
        (name == NULL || Text_EqualsIgnoreCase(reference->name, name));
}

uint32_t MAPINFOItems_ProgramCount(SD_Kind kind, int32_t number, const char* name) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_map.program.count; i++)
        count += ProgramMatches(&g_map.program.references[i], kind, number, name);
    return count;
}

bool MAPINFOItems_SameReference(const SD_Reference* a, const SD_Reference* b) {
    return a->kind == b->kind && a->number == b->number && Text_EqualsIgnoreCase(a->name, b->name) &&
        Text_EqualsIgnoreCase(a->folder, b->folder);
}

// The references of a kind, each number, name and folder counted once
static uint32_t ProgramDistinct(SD_Kind kind) {
    uint32_t count = 0;
    const SD_Reference* references = g_map.program.references;
    for (uint32_t i = 0; i < g_map.program.count; i++) {
        bool first = references[i].kind == kind;
        for (uint32_t j = 0; j < i && first; j++)
            first = !MAPINFOItems_SameReference(&references[j], &references[i]);
        count += first;
    }
    return count;
}

// "no", or the number of calls of the program and their addresses (up to MAPINFOVIEW_PROGRAM_CALLS of them)
#define MAPINFOVIEW_PROGRAM_CALLS 6
const char* MAPINFOItems_ProgramCalls(SD_Kind kind, int32_t number, const char* name) {
    static char text[MAPINFOVIEW_LABEL_SIZE];
    uint32_t count = MAPINFOItems_ProgramCount(kind, number, name), shown = 0;
    if (count == 0)
        return "no";
    int length = snprintf(text, sizeof(text), "%u call%s at", count, count == 1 ? "" : "s");
    for (uint32_t i = 0; i < g_map.program.count && length < (int)sizeof(text); i++) {
        const SD_Reference* reference = &g_map.program.references[i];
        if (!ProgramMatches(reference, kind, number, name))
            continue;
        if (shown++ == MAPINFOVIEW_PROGRAM_CALLS) {
            snprintf(text + length, sizeof(text) - length, " ...");
            break;
        }
        length += snprintf(text + length, sizeof(text) - length, " 0x%X", reference->address);
    }
    return text;
}

// The distinct cutscenes that the program loads in the table of cutscenes, and whether their AUTH file is found.
// Returns false on memory error
bool MAPINFOItems_FindSequences(void) {
    g_map.sequences = calloc(g_map.program.count + 1, sizeof(uint32_t));
    g_map.sequenceFound = calloc(g_map.program.count + 1, sizeof(bool));
    if (g_map.sequences == NULL || g_map.sequenceFound == NULL)
        return false;
    const SD_Reference* references = g_map.program.references;
    for (uint32_t i = 0; i < g_map.program.count; i++) {
        bool found = references[i].kind != SD_SEQUENCE;
        for (uint32_t j = 0; j < g_map.sequenceCount && !found; j++)
            found = MAPINFOItems_SameReference(&references[g_map.sequences[j]], &references[i]);
        if (found)
            continue;
        size_t size = 0;
        uint8_t* data = MapProgram_LoadSequence(g_map.filename, &references[i], &size);
        g_map.sequenceFound[g_map.sequenceCount] = data != NULL;
        free(data);
        g_map.sequences[g_map.sequenceCount++] = i;
    }
    return true;
}

const SD_Reference* MAPINFOItems_Sequence(uint32_t index) {
    return &g_map.program.references[g_map.sequences[index]];
}

// Its slot, its file and its package (or folder)
// The number of a camera ID ("2019": 2019), -1 if it isn't 4 digits
int32_t MAPINFOItems_CameraNumber(const char* id) {
    for (int i = 0; i < 4; i++) {
        if (id[i] < '0' || id[i] > '9')
            return -1;
    }
    return id[4] == '\0' ? atoi(id) : -1;
}

// The characters with which the program shows a camera, separated by commas (each one once), empty if none
const char* MAPINFOItems_CameraCharacters(int32_t number) {
    static char text[MAPINFOVIEW_LABEL_SIZE];
    int length = 0;
    text[0] = '\0';
    for (uint32_t i = 0; i < g_map.program.count && number >= 0; i++) {
        const SD_Reference* reference = &g_map.program.references[i];
        const char* ids[2] = { reference->name, reference->folder };
        for (int c = 0; c < 2 && reference->kind == SD_CAMERA && reference->number == number; c++) {
            if (ids[c][0] != '\0' && strstr(text, ids[c]) == NULL && length < (int)sizeof(text))
                length += snprintf(text + length, sizeof(text) - length, "%s%s", length > 0 ? ", " : "", ids[c]);
        }
    }
    return text;
}

// Its ID, and whether the program shows it and with which characters
static const char* CameraText(uint32_t index) {
    const char* id = g_map.file.cameras[index].id;
    int32_t number = MAPINFOItems_CameraNumber(id);
    if (number < 0 || MAPINFOItems_ProgramCount(SD_CAMERA, number, NULL) == 0)
        return id;
    const char* characters = MAPINFOItems_CameraCharacters(number);
    return characters[0] != '\0' ? TextFormat("%s  (shown, with %s)", id, characters) : TextFormat("%s  (shown)", id);
}

static const char* SequenceText(uint32_t index) {
    const SD_Reference* sequence = MAPINFOItems_Sequence(index);
    return TextFormat("%3d  %s of %s%s", sequence->number, sequence->name, sequence->folder,
        g_map.sequenceFound[index] ? "" : " (not found)");
}

// The flags and the bytes of the game used by the program: SD_FLAG_READ and SD_FLAG_SET give a flag, SD_BYTE_READ and
// SD_BYTE_SET a byte (stored as SD_FLAGS + its number)
static bool IsFlagKind(uint8_t kind) {
    return kind == SD_FLAG_READ || kind == SD_FLAG_SET || kind == SD_BYTE_READ || kind == SD_BYTE_SET;
}

int32_t MAPINFOItems_FlagKey(const SD_Reference* reference) {
    return (reference->kind == SD_BYTE_READ || reference->kind == SD_BYTE_SET ? SD_FLAGS : 0) + reference->number;
}

static int CompareFlags(const void* a, const void* b) {
    return *(const int32_t*)a - *(const int32_t*)b;
}

// Returns false on memory error
bool MAPINFOItems_FindFlags(void) {
    g_map.flags = calloc(g_map.program.count + 1, sizeof(int32_t));
    if (g_map.flags == NULL)
        return false;
    for (uint32_t i = 0; i < g_map.program.count; i++) {
        const SD_Reference* reference = &g_map.program.references[i];
        if (!IsFlagKind(reference->kind))
            continue;
        int32_t key = MAPINFOItems_FlagKey(reference);
        bool found = false;
        for (uint32_t f = 0; f < g_map.flagCount && !found; f++)
            found = g_map.flags[f] == key;
        if (!found)
            g_map.flags[g_map.flagCount++] = key;
    }
    qsort(g_map.flags, g_map.flagCount, sizeof(int32_t), CompareFlags);
    return true;
}

const char* MAPINFOItems_FlagName(uint32_t index) {
    int32_t key = g_map.flags[index];
    return key >= SD_FLAGS ? TextFormat("Byte %d", key - SD_FLAGS) : TextFormat("Flag %d", key);
}

// The calls of the program that read or set the flag of an item, by kind
static uint32_t FlagCalls(uint32_t index, bool set) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_map.program.count; i++) {
        const SD_Reference* reference = &g_map.program.references[i];
        count += IsFlagKind(reference->kind) && MAPINFOItems_FlagKey(reference) == g_map.flags[index] &&
            (reference->kind == SD_FLAG_SET || reference->kind == SD_BYTE_SET) == set;
    }
    return count;
}

static const char* FlagText(uint32_t index) {
    uint32_t reads = FlagCalls(index, false), sets = FlagCalls(index, true);
    return TextFormat("%s  (%s%s%s)", MAPINFOItems_FlagName(index), reads > 0 ? TextFormat("read %u", reads) : "",
        reads > 0 && sets > 0 ? ", " : "", sets > 0 ? TextFormat("set %u", sets) : "");
}

// Name of the subgroup of the characters of a package in the tree
const char* MAPINFOItems_PackageText(const MapPackage* package) {
    const char* loaded = MAPINFOItems_ProgramCount(SD_PACKAGE, -1, package->name) > 0 ? ", loaded by the program" : "";
    return package->variantOf >= 0 ? TextFormat("%s, variant of %s%s", package->name, g_map.packages[package->variantOf].name,
        loaded) : TextFormat("%s%s", package->name, loaded);
}

// The center of the points of a shape (see DrawShape)
Vector3 MAPINFOItems_ShapeCenter(const MAPINFO_Shape* shape) {
    const float* v = shape->values;
    if (shape->type == MAPINFO_SHAPE_CIRCLE && shape->valueCount >= 3)
        return ShapePoint(v[1], ShapeHeight(shape, 0), v[2]);
    if (shape->type == MAPINFO_SHAPE_RECTANGLE && shape->valueCount >= 2)
        return ShapePoint(v[0], ShapeHeight(shape, 0), v[1]);
    Vector3 sum = Vector3Zero();
    uint32_t count = shape->valueCount / 2;
    for (uint32_t i = 0; i < count; i++)
        sum = Vector3Add(sum, ShapePoint(v[i * 2], ShapeHeight(shape, i), v[i * 2 + 1]));
    return count > 0 ? Vector3Scale(sum, 1.0f / count) : sum;
}

const char* MAPINFOItems_ShapeType(const MAPINFO_Shape* shape) {
    return shape->type < sizeof(g_shapeTypes) / sizeof(g_shapeTypes[0]) ? g_shapeTypes[shape->type] : "unknown";
}

static const char* ScnText(uint32_t version) {
    return version == 0x20000 ? "SH-4 code" : version == 0x100 ? "bytecode" : "unknown version";
}

static void AddInfoItems(void) {
    const MAPINFO_File* file = &g_map.file;
    const GameDB_Map* map = GameDB_MapOfFile(g_map.filename);
    if (map != NULL)
        AddItem(ITEM_INFO, 0, map->area[0] != '\0' ? TextFormat("Map: %s (%s)", map->name, map->area) : TextFormat("Map: %s", map->name), false);
    const MAPINFO_Chunk* scn = MAPINFO_FindChunk(file, "SCN3");
    if (scn != NULL)
        AddItem(ITEM_INFO, 0, TextFormat("SCN3: %s (0x%X), 0x%X bytes", ScnText(file->scnVersion), file->scnVersion, scn->size), false);
    if (file->fog.present)
        AddItem(ITEM_INFO, 0, TextFormat("Fog: %g to %g, density %g, color #%06X", file->fog.nearDistance, file->fog.farDistance,
            file->fog.density, file->fog.color & 0xFFFFFF), false);
    uint32_t cutscenes = 0;
    for (uint32_t i = 0; i < file->cutsceneCount; i++)
        cutscenes += g_map.cutscenes[i].valid;
    if (file->cutsceneCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("SCEX: %u cutscenes, %u empty", cutscenes, file->cutsceneCount - cutscenes), false);
    if (g_map.program.count > 0) {
        AddItem(ITEM_INFO, 0, TextFormat("Program: loads %u packages and %u cutscenes, plays %u cutscenes",
            ProgramDistinct(SD_PACKAGE) + ProgramDistinct(SD_AFS_PACKAGE), ProgramDistinct(SD_SEQUENCE),
            ProgramDistinct(SD_CUTSCENE)), false);
        AddItem(ITEM_INFO, 0, TextFormat("Program: wakes %u characters, puts %u to sleep", ProgramDistinct(SD_WAKE),
            ProgramDistinct(SD_SLEEP)), false);
        AddItem(ITEM_INFO, 0, TextFormat("Program: reads %u flags of the game and sets %u", ProgramDistinct(SD_FLAG_READ),
            ProgramDistinct(SD_FLAG_SET)), false);
    }
    for (uint32_t i = 0; i < file->bankCount; i++) {
        if (strcmp(file->banks[i].name, "FREE") != 0)
            AddItem(ITEM_INFO, 0, TextFormat("Sound bank %u: %s", file->banks[i].slot, file->banks[i].name), false);
    }
    // The chunks of a single value, by the probable meaning of their IDs: number of MAP models (MAP01...), MAP Type,
    // DIRecTion, SCRoLl
    const char* values[][2] = {
        { "MAPR", "Map models" }, { "MAPT", "Map type" }, { "DIRT", "Direction" }, { "SCRL", "Scroll" },
    };
    for (int i = 0; i < 4; i++) {
        uint32_t value;
        if (MAPINFO_ChunkValue(file, values[i][0], &value))
            AddItem(ITEM_INFO, 0, TextFormat("%s (%s): %d", values[i][1], values[i][0], (int32_t)value), false);
    }
    const char* floats[] = { "SCOF", "BLKS" };
    for (int i = 0; i < 2; i++) {
        uint32_t value;
        float f;
        if (MAPINFO_ChunkValue(file, floats[i], &value)) {
            memcpy(&f, &value, sizeof(f));
            AddItem(ITEM_INFO, 0, TextFormat("%s: %g", floats[i], f), false);
        }
    }
}

// What the cutscenes of the SCEX chunk contain. Returns false on memory error
bool MAPINFOItems_ReadCutscenes(void) {
    const MAPINFO_File* file = &g_map.file;
    g_map.cutscenes = calloc(file->cutsceneCount + 1, sizeof(MapCutscene));
    if (g_map.cutscenes == NULL)
        return false;
    for (uint32_t i = 0; i < file->cutsceneCount; i++) {
        MapCutscene* cutscene = &g_map.cutscenes[i];
        AUTH_Sequence sequence;
        // The empty chunks are only a header
        if (file->cutscenes[i].size <= 8 || !AUTH_LoadFromMemory(file->data + file->cutscenes[i].offset, file->cutscenes[i].size,
            TextFormat("%s #%u", g_map.filename, i), &sequence))
            continue;
        cutscene->valid = true;
        cutscene->duration = sequence.duration;
        cutscene->characterCount = sequence.characterCount;
        cutscene->commandCount = sequence.commandCount;
        for (uint32_t c = 0; c < sequence.commandCount; c++) {
            uint8_t opcode = sequence.commands[c].opcode;
            cutscene->motions += opcode == AUTH_COMMAND_MOTION;
            cutscene->voices += opcode == AUTH_COMMAND_VOICE;
            cutscene->sounds += opcode == AUTH_COMMAND_SOUND;
        }
        int length = 0;
        for (uint32_t c = 0; c < sequence.characterCount && length < (int)sizeof(cutscene->ids); c++)
            length += snprintf(cutscene->ids + length, sizeof(cutscene->ids) - length, "%s%s", c > 0 ? " " : "", sequence.characters[c].id);
        AUTH_Free(&sequence);
    }
    return true;
}

// The names of the characters of a cutscene (their ID for those without a name), separated by commas, up to max of
// them (followed by "..." if it has more)
const char* MAPINFOItems_CutsceneNames(const MapCutscene* cutscene, int max) {
    static char names[MAPINFOVIEW_LABEL_SIZE * 2];
    names[0] = '\0';
    int length = 0, count = 0;
    for (const char* id = cutscene->ids; *id != '\0' && length < (int)sizeof(names);) {
        const char* end = strchr(id, ' ');
        size_t size = end != NULL ? (size_t)(end - id) : strlen(id);
        if (count == max) {
            snprintf(names + length, sizeof(names) - length, ", ...");
            break;
        }
        char text[16];
        snprintf(text, sizeof(text), "%.*s", (int)size, id);
        length += snprintf(names + length, sizeof(names) - length, "%s%s", count > 0 ? ", " : "", GameDB_CharacterDisplay(text));
        count++;
        id += size;
        while (*id == ' ')
            id++;
    }
    return names;
}

// Its number and up to MAPINFOVIEW_CUTSCENE_IDS of its characters
static const char* CutsceneText(uint32_t index) {
    const MapCutscene* cutscene = &g_map.cutscenes[index];
    if (!cutscene->valid)
        return TextFormat("%3u  empty", index);
    return TextFormat("%3u  %s%s", index, MAPINFOItems_CutsceneNames(cutscene, MAPINFOVIEW_CUTSCENE_IDS),
        MAPINFOItems_ProgramCount(SD_CUTSCENE, (int32_t)index, NULL) > 0 ? "  (played)" : "");
}

// The items of each kind, in ItemKind order
bool MAPINFOItems_Create(void) {
    const MAPINFO_File* file = &g_map.file;
    uint32_t count = 16 + file->bankCount + g_map.mapModelCount + file->doorCount + file->cameraCount + file->lightCount +
        file->shapeCount + g_map.characterCount + file->cutsceneCount + g_map.sequenceCount + g_map.flagCount;
    g_map.items = malloc(count * sizeof(Item));
    g_map.labels = malloc(count * sizeof(*g_map.labels));
    g_map.failedItems = malloc(count * sizeof(bool));
    bool* visibleItems = malloc(count * sizeof(bool));
    if (g_map.items == NULL || g_map.labels == NULL || g_map.failedItems == NULL || visibleItems == NULL) {
        free(visibleItems);
        return false;
    }
    // Set at the end: all the items are visible before
    g_map.visibleItems = visibleItems;

    g_map.firstItems[ITEM_INFO] = g_map.itemCount;
    AddInfoItems();
    g_map.firstItems[ITEM_MAP_MODEL] = g_map.itemCount;
    for (uint32_t i = 0; i < g_map.mapModelCount; i++)
        AddItem(ITEM_MAP_MODEL, i, g_map.mapModelNames[i], false);
    g_map.firstItems[ITEM_DOOR] = g_map.itemCount;
    for (uint32_t i = 0; i < file->doorCount; i++) {
        const MAPINFO_Door* door = &file->doors[i];
        const char* error = g_map.doorErrors != NULL ? g_map.doorErrors[i] : NULL;
        AddItem(ITEM_DOOR, i, door->model, error != NULL);
    }
    g_map.firstItems[ITEM_CAMERA] = g_map.itemCount;
    for (uint32_t i = 0; i < file->cameraCount; i++) {
        AddItem(ITEM_CAMERA, i, CameraText(i), false);
    }
    g_map.firstItems[ITEM_LIGHT] = g_map.itemCount;
    for (uint32_t i = 0; i < file->lightCount; i++) {
        AddItem(ITEM_LIGHT, i, TextFormat("%u.%u", file->lights[i].group, file->lights[i].id), false);
    }
    g_map.firstItems[ITEM_SHAPE] = g_map.itemCount;
    for (uint32_t i = 0; i < file->shapeCount; i++)
        AddItem(ITEM_SHAPE, i, TextFormat("%u  %s", i, MAPINFOItems_ShapeType(&file->shapes[i])), false);
    g_map.firstItems[ITEM_CHARACTER] = g_map.itemCount;
    for (uint32_t i = 0; i < g_map.characterCount; i++)
        AddItem(ITEM_CHARACTER, i, GameDB_CharacterLabel(g_map.characters[i].info.id), g_map.characters[i].error != NULL);
    g_map.firstItems[ITEM_CUTSCENE] = g_map.itemCount;
    for (uint32_t i = 0; i < file->cutsceneCount; i++)
        AddItem(ITEM_CUTSCENE, i, CutsceneText(i), false);
    g_map.firstItems[ITEM_SEQUENCE] = g_map.itemCount;
    for (uint32_t i = 0; i < g_map.sequenceCount; i++)
        AddItem(ITEM_SEQUENCE, i, SequenceText(i), false);
    g_map.firstItems[ITEM_FLAG] = g_map.itemCount;
    for (uint32_t i = 0; i < g_map.flagCount; i++)
        AddItem(ITEM_FLAG, i, FlagText(i), false);
    return true;
}

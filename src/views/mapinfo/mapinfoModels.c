// The models of the view of the maps: of the map, of its doors and of the characters of its CHRT scripts, and the
// packages of the characters
#include "views/mapinfo/mapinfoViewInternal.h"

#define MAPINFOVIEW_PACKAGE      "mpk00" // Base package of the map (PKF/PKS files or their extracted folder)
static bool IsCharacterCommand(const CHRT_Script* script, uint32_t command) {
    return script->commands[command].parent == -1 && CHRT_IsCommand(&script->commands[command], "Character");
}

static uint32_t CountCharacters(const CHRT_Script* script) {
    uint32_t count = 0;
    for (uint32_t c = 0; c < script->commandCount; c++)
        count += IsCharacterCommand(script, c);
    return count;
}

// Appends the characters of a script of g_map.scripts, of a package of g_map.packages (-1 for the CHRD chunk)
static void AddCharacters(uint32_t script, int package) {
    const CHRT_Script* source = &g_map.scripts[script];
    MapCharacter* characters = realloc(g_map.characters, (g_map.characterCount + source->commandCount + 1) * sizeof(MapCharacter));
    if (characters == NULL)
        return;
    g_map.characters = characters;
    for (uint32_t c = 0; c < source->commandCount; c++) {
        if (!IsCharacterCommand(source, c))
            continue;
        MapCharacter* character = &characters[g_map.characterCount++];
        memset(character, 0, sizeof(*character));
        CHRT_ReadCharacter(source, c, &character->info);
        character->original = character->info;
        character->script = script;
        character->package = package;
        character->model = -1;
    }
}

const char* MAPINFOModels_PackageName(int package) {
    return package >= 0 ? g_map.packages[package].name : NULL;
}

Vector3 MAPINFOModels_CharacterPosition(const CHRT_Character* character) {
    return (Vector3) { character->position[0], character->position[1], character->position[2] };
}

// Drawn with its model: in the scene, with an image, also the sleeping ones (e.g. the taxi TAXI of scene/01/jd99). The
// arms of Ryo for the close-ups aren't: they are 10 times bigger than the map and wait at the origin of the map or at
// the place of the close-up
static bool IsDisplayed(const CHRT_Character* character) {
    return character->positioned && !character->arm && character->image != NULL;
}

// As the nodes of the models: rotation around X, Y and Z, and translation. The models of the persons have their origin
// at the hips (the motions place them), so they are lifted to stand at their position
static Matrix CharacterTransform(const CHRT_Character* character, const MT5Render_Model* model) {
    return Scene3D_PlaceTransform(character->human ? -MT5Render_Bottom(model) : 0.0f, character->angle, character->position);
}

// Models of the map: MAP.mt5, MAP01.mt5... in the folder, in its subfolders (mpk00, map0...) and in its PKF/PKS files,
// and the packed files of type MAPM, whatever their name (e.g. JYUU.MAPM in the packages of op99.afs of scene/01/op00)
static bool IsMapModel(const MapFile* file) {
    if (!IsFileExtension(file->name, ".mt5"))
        return false;
    if (file->packed != NULL && strcmp(file->packed->type, "MAPM") == 0)
        return true;
    const char* name = GetFileName(file->name);
    if (toupper((unsigned char)name[0]) != 'M' || toupper((unsigned char)name[1]) != 'A' || toupper((unsigned char)name[2]) != 'P')
        return false;
    for (name += 3; isdigit((unsigned char)*name); name++)
        ;
    return *name == '.';
}

// The external textures of the packed files are searched in the files of all the PKF/PKS files (e.g. the MAP.mt5 of
// mpk00.pks uses the textures of mpk00.pkf)
static bool LoadMapModel(const MapFile* file, const IPAC_FileList* packed, MT5Render_Model* model) {
    return file->packed != NULL ? MT5Render_LoadPackedModel(file->packed, packed, model) : MT5Render_LoadModel(file->path, model);
}

static bool LoadMapScript(const MapFile* file, CHRT_Script* script) {
    return file->packed != NULL ? CHRT_LoadFromMemory(file->packed->data, file->packed->size, file->name, script) : CHRT_Load(file->path, script);
}

// Model file (MT5 or CHRM) of a model name of the CHRT scripts, preferably of a package (the variants can have models
// with the same name), of any package if it is NULL. Returns -1 if there is none
static int ModelFile(const MapFile* files, uint32_t count, const char* name, const char* package) {
    char base[64];
    CHRT_ModelBase(name, base, sizeof(base));
    int found = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (IsFileExtension(files[i].name, ".chrt") || !Text_EqualsIgnoreCase(GetFileNameWithoutExt(files[i].name), base))
            continue;
        if (package == NULL || Text_EqualsIgnoreCase(files[i].package, package))
            return (int)i;
        if (found < 0)
            found = (int)i;
    }
    return found;
}

// Model file of the model of a door: the file with its name, or the file of the DefImage with its name in the CHRT
// scripts of the map (the CHRD chunk and the CHRT files, e.g. mpk00/CHARA.chrt). Returns -1 if there is none
static int DoorModelFile(const MapFile* files, uint32_t count, const char* name) {
    int file = ModelFile(files, count, name, NULL);
    for (uint32_t i = 0; file < 0 && i < g_map.scriptCount; i++) {
        const char* image = CHRT_ImageFile(&g_map.scripts[i], name);
        if (image != NULL)
            file = ModelFile(files, count, image, NULL);
    }
    return file;
}

// Model name of the image of a character: the file of the DefImage with its name in its script or in the other scripts
// of the map, or the name itself (some names are defined in other files, and they are usually the name of the model
// file)
static const char* CharacterModelName(const MapCharacter* character) {
    const char* name = character->info.image;
    const char* image = CHRT_ImageFile(&g_map.scripts[character->script], name);
    for (uint32_t i = 0; image == NULL && i < g_map.scriptCount; i++)
        image = CHRT_ImageFile(&g_map.scripts[i], name);
    return image != NULL ? image : name;
}

// Model of a file, loaded the first time. Returns -1 if it can't be loaded
static int FileModel(const MapFile* files, int f, int* fileModels, const IPAC_FileList* packed) {
    if (fileModels[f] < 0 && LoadMapModel(&files[f], packed, &g_map.models[g_map.modelCount]))
        fileModels[f] = (int)g_map.modelCount++;
    return fileModels[f];
}

static void AddInstance(uint32_t model, Matrix transform) {
    MT5Render_Model* instanced = &g_map.models[model];
    g_map.instances[g_map.instanceCount++] = (MT5Render_Instance) { instanced, transform,
        Vector3Transform(instanced->mesh.center, transform), 0 };
}

// In the bounding sphere of a model of the map, or true if there is none. Some characters wait outside the map at
// positions below CHRT_OFF_SCENE, e.g. (300, 0, 0)
static bool InMapModels(Vector3 position) {
    for (uint32_t i = 0; i < g_map.mapModelCount; i++) {
        const MT5Mesh* mesh = &g_map.models[i].mesh;
        if (Vector3Distance(position, mesh->center) <= mesh->radius)
            return true;
    }
    return g_map.mapModelCount == 0;
}

// A model of the folder of the scene, loaded or not
typedef struct {
    char    name[64];   // Without prefixes and extension
    int     model;      // -1 if it can't be loaded
} SceneModel;

// Model of a model name in the folder of the scene of the map (e.g. scene/01/model/chara/ykb_m.mt5, Ryo, for
// scene/01/d000, and scene/01/model/chara/c85n310g.mt5, the taxi, for scene/01/jd99), loaded the first time, for the
// characters whose model isn't in the map. Returns -1 if there is none or it can't be loaded
static int FolderSceneModel(const char* folder, const char* name, SceneModel* sceneModels, uint32_t* sceneModelCount) {
    char base[64];
    CHRT_ModelBase(name, base, sizeof(base));
    for (uint32_t i = 0; i < *sceneModelCount; i++) {
        if (Text_EqualsIgnoreCase(sceneModels[i].name, base))
            return sceneModels[i].model;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s.mt5", GetPrevDirectoryPath(folder), MAPFILES_SCENE_MODELS, TextToLower(base));
    SceneModel* added = &sceneModels[(*sceneModelCount)++];
    snprintf(added->name, sizeof(added->name), "%s", base);
    added->model = FileExists(path) && MT5Render_LoadModel(path, &g_map.models[g_map.modelCount]) ? (int)g_map.modelCount++ : -1;
    return added->model;
}

// A character is shown if its package is (the CHRD chunk always is) and no character shown before has its ID: the
// first shown package with an ID wins. The instances of the characters are those of the shown ones with their model
void MAPINFOModels_ShowCharacters(void) {
    g_map.instanceCount = g_map.characterInstances;
    g_map.modelledCharacters = 0;
    for (uint32_t i = 0; i < g_map.characterCount; i++) {
        MapCharacter* character = &g_map.characters[i];
        character->shown = character->package < 0 || g_map.packages[character->package].shown;
        for (uint32_t c = 0; c < i && character->shown; c++) {
            const MapCharacter* previous = &g_map.characters[c];
            character->shown = !previous->shown || previous->script == character->script || strcmp(previous->info.id, character->info.id) != 0;
        }
        if (character->shown && character->model >= 0 && MAPINFOView_IsItemVisible(ITEM_CHARACTER, i)) {
            AddInstance((uint32_t)character->model, CharacterTransform(&character->info, &g_map.models[character->model]));
            g_map.modelledCharacters++;
        }
    }
}

// As the nodes of the models: scale, rotation and translation
Matrix MAPINFOModels_DoorTransform(const MAPINFO_Door* door) {
    Matrix transform = MatrixMultiply(MatrixScale(door->scale[0], door->scale[1], door->scale[2]), MatrixRotateY(door->angle * DEG2RAD));
    return MatrixMultiply(transform, MatrixTranslate(door->position[0], door->position[1], door->position[2]));
}

// The instances of the models of the map, of the doors at their placements and of the shown characters in the map. The
// characters whose model isn't in the map are drawn with the model of the folder of the scene
static void PlaceModels(const char* folder, const MapFile* files, uint32_t count, int* fileModels, const IPAC_FileList* packed) {
    const MAPINFO_File* file = &g_map.file;
    for (uint32_t i = 0; i < count; i++) {
        if (IsMapModel(&files[i]) && FileModel(files, (int)i, fileModels, packed) >= 0) {
            snprintf(g_map.mapModelNames[g_map.instanceCount], MAPINFOVIEW_LABEL_SIZE, "%s%s%s", files[i].name,
                files[i].package[0] != '\0' ? "  " : "", files[i].package);
            g_map.mapModelSlots[g_map.instanceCount] = SCNDisplay_SlotOfFile(files[i].name);
            snprintf(g_map.mapModelPackages[g_map.instanceCount], IPAC_FILENAME_SIZE, "%s", files[i].package);
            AddInstance((uint32_t)fileModels[i], MatrixIdentity());
        }
    }
    g_map.mapModelCount = g_map.modelCount;

    for (uint32_t i = 0; i < file->doorCount; i++) {
        const MAPINFO_Door* door = &file->doors[i];
        g_map.doorInstances[i] = -1;
        int f = DoorModelFile(files, count, door->model);
        if (f < 0) {
            g_map.doorErrors[i] = "no model file";
            continue;
        }
        if (FileModel(files, f, fileModels, packed) < 0) {
            g_map.doorErrors[i] = "invalid model file";
            continue;
        }
        g_map.doorInstances[i] = (int32_t)g_map.instanceCount;
        AddInstance((uint32_t)fileModels[f], MAPINFOModels_DoorTransform(door));
        g_map.loadedDoors++;
    }

    g_map.characterInstances = g_map.instanceCount;
    SceneModel* sceneModels = calloc(g_map.characterCount + 1, sizeof(SceneModel));
    uint32_t sceneModelCount = 0;
    for (uint32_t i = 0; i < g_map.characterCount && sceneModels != NULL; i++) {
        MapCharacter* character = &g_map.characters[i];
        if (!IsDisplayed(&character->info) || !InMapModels(MAPINFOModels_CharacterPosition(&character->info)))
            continue;
        const char* name = CharacterModelName(character);
        int f = ModelFile(files, count, name, MAPINFOModels_PackageName(character->package));
        int model = f >= 0 ? FileModel(files, f, fileModels, packed) : FolderSceneModel(folder, name, sceneModels, &sceneModelCount);
        if (model < 0)
            character->error = f < 0 ? "no model file" : "invalid model file";
        character->model = model;
    }
    free(sceneModels);
    MAPINFOModels_ShowCharacters();
}

// The base package first, then those with more characters
static int ComparePackages(const void* a, const void* b) {
    const MapPackage* first = a;
    const MapPackage* second = b;
    bool firstBase = Text_EqualsIgnoreCase(first->name, MAPINFOVIEW_PACKAGE), secondBase = Text_EqualsIgnoreCase(second->name, MAPINFOVIEW_PACKAGE);
    if (firstBase != secondBase)
        return firstBase ? -1 : 1;
    if (first->characterCount != second->characterCount)
        return first->characterCount > second->characterCount ? -1 : 1;
    return strcmp(first->name, second->name);
}

static bool ShareCharacter(int package, int other) {
    for (uint32_t i = 0; i < g_map.characterCount; i++) {
        if (g_map.characters[i].package != package)
            continue;
        for (uint32_t j = 0; j < g_map.characterCount; j++) {
            if (g_map.characters[j].package == other && strcmp(g_map.characters[i].info.id, g_map.characters[j].info.id) == 0)
                return true;
        }
    }
    return false;
}

// The scripts of the map: the CHRD chunk, then the CHRT files of the packages (CHARA.chrt), with their characters
static void LoadScripts(const MapFile* files, uint32_t count) {
    const MAPINFO_File* file = &g_map.file;
    if (file->characters != NULL &&
        CHRT_LoadFromMemory(file->characters, file->charactersSize, g_map.filename, &g_map.scripts[g_map.scriptCount]))
        AddCharacters(g_map.scriptCount++, -1);
    g_map.packages = calloc(count + 1, sizeof(MapPackage));
    if (g_map.packages == NULL)
        return;
    for (uint32_t i = 0; i < count; i++) {
        if (!IsFileExtension(files[i].name, ".chrt") || !LoadMapScript(&files[i], &g_map.scripts[g_map.scriptCount]))
            continue;
        MapPackage* package = &g_map.packages[g_map.packageCount++];
        package->script = g_map.scriptCount++;
        snprintf(package->name, sizeof(package->name), "%s", files[i].package[0] != '\0' ? files[i].package : GetFileNameWithoutExt(files[i].name));
        package->characterCount = CountCharacters(&g_map.scripts[package->script]);
    }
    qsort(g_map.packages, g_map.packageCount, sizeof(MapPackage), ComparePackages);
    for (uint32_t p = 0; p < g_map.packageCount; p++)
        AddCharacters(g_map.packages[p].script, (int)p);
    for (uint32_t p = 0; p < g_map.packageCount; p++) {
        MapPackage* package = &g_map.packages[p];
        package->variantOf = -1;
        for (uint32_t q = 0; q < p && package->variantOf < 0; q++) {
            if (ShareCharacter((int)p, (int)q))
                package->variantOf = (int)q;
        }
        package->shown = package->variantOf < 0;
        package->expanded = package->shown;
    }
}

// The scripts, the models of the map, the models of the doors at their placements and the models of the characters,
// from the MT5, CHRM and CHRT files of the map (see mapFiles.h). Each file is loaded once
void MAPINFOModels_Load(const char* folder) {
    const MAPINFO_File* file = &g_map.file;
    MapFiles mapFiles;
    if (!MapFiles_Load(folder, ".mt5;.chrm;.chrt", &mapFiles))
        return;
    const MapFile* files = mapFiles.files;
    uint32_t count = mapFiles.count;
    int* fileModels = malloc((count + 1) * sizeof(int)); // Model of each file, -1 if not loaded
    g_map.scripts = calloc(count + 2, sizeof(CHRT_Script));
    g_map.doorErrors = calloc(file->doorCount + 1, sizeof(const char*));
    g_map.doorInstances = calloc(file->doorCount + 1, sizeof(int32_t));
    g_map.mapModelNames = calloc(count + 1, sizeof(*g_map.mapModelNames));
    g_map.mapModelSlots = calloc(count + 1, sizeof(int));
    g_map.mapModelPackages = calloc(count + 1, sizeof(*g_map.mapModelPackages));
    g_map.mapModelVariants = calloc(count + 1, sizeof(bool));
    if (fileModels != NULL && g_map.scripts != NULL && g_map.doorErrors != NULL && g_map.doorInstances != NULL &&
        g_map.mapModelNames != NULL && g_map.mapModelSlots != NULL && g_map.mapModelPackages != NULL &&
        g_map.mapModelVariants != NULL) {
        for (uint32_t i = 0; i < count; i++)
            fileModels[i] = -1;

        LoadScripts(files, count);
        // The models of the files, and of the folder of the scene for the characters
        g_map.models = calloc(count + g_map.characterCount + 1, sizeof(MT5Render_Model));
        g_map.instances = calloc(count + file->doorCount + g_map.characterCount + 1, sizeof(MT5Render_Instance));
        g_map.visibleInstances = calloc(count + file->doorCount + g_map.characterCount + 1, sizeof(MT5Render_Instance));
        if (g_map.models != NULL && g_map.instances != NULL && g_map.visibleInstances != NULL)
            PlaceModels(folder, files, count, fileModels, &mapFiles.packed);
    }
    free(fileModels);
    MapFiles_Free(&mapFiles);
}

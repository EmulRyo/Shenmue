// The models of the characters of a cutscene, found in the CHRT scripts of its package, of its scene, of its map and of
// the packages that the program of the map loads
#include "core/log.h"
#include "views/auth/authViewInternal.h"

#define AUTHVIEW_HUMANS         "stream/humans" // Folder (or PKF/PKS pair) of each character of the scene, with its script and models
#define AUTHVIEW_STREAM         "stream"        // Packages of the scene that the programs load (e.g. vend, the vending machine)

// Model file of a model name of the CHRT scripts in the files of a PKF/PKS pair (e.g. FUK_M.MT5 of
// scene/01/stream/humans/FUKU.pkf). Returns NULL if there is none
static const IPAC_File* PackageModel(const IPAC_FileList* files, const char* file) {
    char base[64];
    CHRT_ModelBase(file, base, sizeof(base));
    for (uint32_t i = 0; i < files->count; i++) {
        const IPAC_File* candidate = &files->files[i];
        if (IsFileExtension(candidate->filename, ".chrm;.mt5") && Text_EqualsIgnoreCase(GetFileNameWithoutExt(candidate->filename), base))
            return candidate;
    }
    return NULL;
}

// Model file of a model name of the CHRT scripts in the files of the map of a cutscene of a mapinfo.bin, preferably of a
// package (the PKF/PKS files can have models with the same name). path is the file, or <package>/<name> for a packed
// file. Returns NULL if there is none
static const MapFile* MapModel(const char* file, const char* package, char* path, size_t size) {
    char base[64];
    CHRT_ModelBase(file, base, sizeof(base));
    const MapFile* found = NULL;
    for (uint32_t i = 0; i < g_auth.mapFiles.count; i++) {
        const MapFile* candidate = &g_auth.mapFiles.files[i];
        if (!IsFileExtension(candidate->name, ".chrm;.mt5") || !Text_EqualsIgnoreCase(GetFileNameWithoutExt(candidate->name), base))
            continue;
        if (found == NULL || (package != NULL && Text_EqualsIgnoreCase(candidate->package, package) && !Text_EqualsIgnoreCase(found->package, package)))
            found = candidate;
    }
    if (found != NULL && found->packed != NULL)
        snprintf(path, size, "%s/%s", found->package, found->name);
    else if (found != NULL)
        snprintf(path, size, "%s", found->path);
    return found;
}

// Model of a character of a CHRT script: the file of its first Image (of the DefImage with that name, or the name itself),
// in the folder of the script (none for a packed script), in the files of the PKF/PKS pair of the script (files, NULL if
// none), in the files of the map of a cutscene of a mapinfo.bin (preferably of the package of the script), or in the
// folder of the models of the scene (e.g. scene/01/model/chara/ykb_m.mt5, Ryo). packed is the file if it is packed, NULL
// otherwise, and textures the files with its textures; info its Character block if not NULL (its image points into the
// script). Returns false if the script has no such character with a model file
static bool ScriptModel(const CHRT_Script* script, const char* id, const char* folder, const char* package, const IPAC_FileList* files,
    char* path, size_t size, const IPAC_File** packed, const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    for (uint32_t i = 0; i < script->commandCount; i++) {
        const CHRT_Command* command = &script->commands[i];
        if (!CHRT_IsCommand(command, "Character") || command->argCount == 0 || command->args[0].type != CHRT_ARG_LITERAL ||
            strcmp(command->args[0].literal, id) != 0)
            continue;
        CHRT_Character character;
        CHRT_ReadCharacter(script, i, &character);
        if (character.image == NULL || character.arm)
            continue;
        if (info != NULL)
            *info = character;
        const char* file = CHRT_ImageFile(script, character.image);
        if (file == NULL)
            file = character.image;
        char models[AUTHVIEW_PATH_SIZE];
        snprintf(models, sizeof(models), "%s/%s", g_auth.sceneFolder, MAPFILES_SCENE_MODELS);
        const IPAC_File* packageFile = files != NULL ? PackageModel(files, file) : NULL;
        if (packageFile != NULL) {
            snprintf(path, size, "%s/%s", id, packageFile->filename);
            *packed = packageFile;
            *textures = files;
            *human = character.human;
            return true;
        }
        const MapFile* mapFile = NULL;
        if ((folder != NULL && CHRT_ModelPath(folder, file, path, size)) || (mapFile = MapModel(file, package, path, size)) != NULL ||
            (g_auth.sceneFolder[0] != '\0' && CHRT_ModelPath(models, TextToLower(file), path, size))) {
            *packed = mapFile != NULL ? mapFile->packed : NULL;
            *textures = &g_auth.mapFiles.packed;
            *human = character.human;
            return true;
        }
    }
    return false;
}

// Model of a character in a CHRT file
static bool ScriptFileModel(const char* scriptPath, const char* id, char* path, size_t size, const IPAC_File** packed,
    const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    CHRT_Script script;
    if (!FileExists(scriptPath) || !CHRT_Load(scriptPath, &script))
        return false;
    char folder[AUTHVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(scriptPath));
    bool found = ScriptModel(&script, id, folder, NULL, NULL, path, size, packed, textures, human, info);
    CHRT_Free(&script);
    return found;
}

// Model of a character in the CHRT script of its PKF/PKS pair in the scene when it isn't extracted (e.g.
// scene/01/stream/humans/FUKU.pkf, with CHARA.chrt and FUK_M.MT5), whose files are loaded into files. Returns the
// script where it was found, NULL if none
static const char* HumanPackageModel(const char* id, IPAC_FileList* files, char* path, size_t size, const IPAC_File** packed,
    const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    const char* extensions[2] = { "pkf", "pks" };
    uint32_t pksFirst = 0;
    for (int i = 0; i < 2; i++) {
        char package[AUTHVIEW_PATH_SIZE];
        snprintf(package, sizeof(package), "%s/%s/%s.%s", g_auth.sceneFolder, AUTHVIEW_HUMANS, id, extensions[i]);
        if (FileExists(package) && i == 0)
            PKF_LoadFiles(package, files);
        else if (FileExists(package))
            PKS_LoadFiles(package, files);
        if (i == 0)
            pksFirst = files->count;
    }
    for (uint32_t i = 0; i < files->count; i++) {
        const IPAC_File* file = &files->files[i];
        CHRT_Script script;
        if (!IsFileExtension(file->filename, ".chrt") || !CHRT_LoadFromMemory(file->data, file->size, file->filename, &script))
            continue;
        bool found = ScriptModel(&script, id, NULL, NULL, files, path, size, packed, textures, human, info);
        CHRT_Free(&script);
        if (found)
            return TextFormat("%s/%s.%s/%s", AUTHVIEW_HUMANS, id, extensions[i < pksFirst ? 0 : 1], file->filename);
    }
    return NULL;
}

// Model of a character in the CHRT scripts of a PKF/PKS pair (the PKF file and the PKS file of the same name), whose
// files are then loaded into files. Returns the script where it was found (a file name of the package), NULL if none
static const char* PackageScriptsModel(const char* pkf, const char* id, IPAC_FileList* files, char* path, size_t size,
    const IPAC_File** packed, const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    if (!FileExists(pkf))
        return NULL;
    IPAC_FileList loaded = { 0 };
    char pks[AUTHVIEW_PATH_SIZE];
    snprintf(pks, sizeof(pks), "%.*ss", (int)strlen(pkf) - 1, pkf);
    PKF_LoadFiles(pkf, &loaded);
    if (FileExists(pks))
        PKS_LoadFiles(pks, &loaded);
    const char* source = NULL;
    for (uint32_t i = 0; i < loaded.count && source == NULL; i++) {
        const IPAC_File* file = &loaded.files[i];
        CHRT_Script script;
        if (!IsFileExtension(file->filename, ".chrt") || !CHRT_LoadFromMemory(file->data, file->size, file->filename, &script))
            continue;
        if (ScriptModel(&script, id, NULL, NULL, &loaded, path, size, packed, textures, human, info))
            source = file->filename;
        CHRT_Free(&script);
    }
    if (source != NULL) {
        // The files of the package stay with the character (its model and textures point into them)
        IPAC_FreeFiles(files);
        *files = loaded;
        if (*textures == &loaded)
            *textures = files;
    } else
        IPAC_FreeFiles(&loaded);
    return source;
}

// Model of a character in the CHRT script of the packages of the stream folder of the scene, which the programs load
// (e.g. the vending machine JIHN of scene/01/stream/vend.pkf, loaded by the program of scene/01/d000 for its event),
// whose files are loaded into files. Returns the script where it was found, NULL if none
static const char* StreamPackageModel(const char* id, IPAC_FileList* files, char* path, size_t size, const IPAC_File** packed,
    const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    char folder[AUTHVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s/%s", g_auth.sceneFolder, AUTHVIEW_STREAM);
    if (!DirectoryExists(folder))
        return NULL;
    const char* source = NULL;
    FilePathList packages = LoadDirectoryFilesEx(folder, ".pkf", false);
    for (unsigned int p = 0; p < packages.count && source == NULL; p++) {
        const char* script = PackageScriptsModel(packages.paths[p], id, files, path, size, packed, textures, human, info);
        if (script != NULL)
            source = TextFormat("%s/%s/%s", AUTHVIEW_STREAM, GetFileName(packages.paths[p]), script);
    }
    UnloadDirectoryFiles(packages);
    return source;
}

// Model of a character in the package that the program of the map loads for it (scnCutscene.h, e.g. the can CAN1 of
// scene/01/stream/sprt.pkf, the drink of the vending machine of scene/01/d000 before the choice of the player), whose
// files are loaded into files. The folder of the package is in the folder of the scene (e.g. "/scene/01/stream/").
// Returns the script where it was found, NULL if none
static const char* ProgramPackageModel(uint32_t idValue, const char* id, IPAC_FileList* files, char* path, size_t size,
    const IPAC_File** packed, const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    const SCNCutscene_Package* package = SCNCutscene_PackageOf(&g_auth.program, idValue);
    if (package == NULL || g_auth.sceneFolder[0] == '\0')
        return NULL;
    // After /scene/NN/
    const char* folder = package->folder;
    for (int parts = 0; parts < 3 && folder != NULL; parts++) {
        const char* slash = strpbrk(folder, "/\\");
        folder = slash != NULL ? slash + 1 : NULL;
    }
    if (folder == NULL)
        return NULL;
    char name[SCNCUTSCENE_NAME_SIZE];
    snprintf(name, sizeof(name), "%s", TextToLower(package->name));
    size_t length = strlen(folder);
    bool separator = length > 0 && (folder[length - 1] == '/' || folder[length - 1] == '\\');
    char pkf[AUTHVIEW_PATH_SIZE];
    snprintf(pkf, sizeof(pkf), "%s/%.*s/%s.pkf", g_auth.sceneFolder, (int)(separator ? length - 1 : length), folder, name);
    const char* script = PackageScriptsModel(pkf, id, files, path, size, packed, textures, human, info);
    return script != NULL ? TextFormat("%.*s/%s.pkf/%s", (int)(separator ? length - 1 : length), folder, name, script) : NULL;
}

// Model of a character in the CHRT files of the map of a cutscene of a mapinfo.bin (e.g. the CHARA.chrt of
// scene/01/jomo/common01.pks), extracted or not. Returns the script where it was found, NULL if none
static const char* MapScriptsModel(const char* id, char* path, size_t size, const IPAC_File** packed, const IPAC_FileList** textures,
    bool* human, CHRT_Character* info) {
    for (uint32_t i = 0; i < g_auth.mapFiles.count; i++) {
        const MapFile* file = &g_auth.mapFiles.files[i];
        CHRT_Script script;
        if (!IsFileExtension(file->name, ".chrt") || !(file->packed != NULL ?
            CHRT_LoadFromMemory(file->packed->data, file->packed->size, file->name, &script) : CHRT_Load(file->path, &script)))
            continue;
        char folder[AUTHVIEW_PATH_SIZE];
        if (file->path != NULL)
            snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(file->path));
        bool found = ScriptModel(&script, id, file->path != NULL ? folder : NULL, file->package, NULL, path, size, packed, textures, human, info);
        CHRT_Free(&script);
        if (found)
            return TextFormat("%s%s%s", file->package, file->package[0] != '\0' ? "/" : "", file->name);
    }
    return NULL;
}

// Model file of a character: in the CHRT scripts of the folder of the cutscene (e.g. the bus driver BUSS of
// scene/01/d000/buss), or of the PKF/PKS files of the map for a cutscene of a mapinfo.bin; in the folder of the character
// in the scene (scene/01/stream/humans/YAMA), or in its PKF/PKS pair if it isn't extracted (loaded into humanFiles); or in
// the characters of the mapinfo.bin of the map, the parent folder of an AUTH file (Ryo, AKIR, in scene/01/d000). packed
// is the file if it is packed, and textures the files with its textures. Returns the script where it was found, NULL if
// none
static const char* FindCharacterModel(const char* id, const CHRT_Script* mapScript, const char* mapFolder, IPAC_FileList* humanFiles,
    char* path, size_t size, const IPAC_File** packed, const IPAC_FileList** textures, bool* human, CHRT_Character* info) {
    const char* source = NULL;
    if (g_auth.cutscene >= 0)
        source = MapScriptsModel(id, path, size, packed, textures, human, info);
    else {
        char folder[AUTHVIEW_PATH_SIZE];
        snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(g_auth.path));
        FilePathList scripts = LoadDirectoryFilesEx(folder, ".chrt", false);
        for (unsigned int i = 0; i < scripts.count && source == NULL; i++) {
            if (ScriptFileModel(scripts.paths[i], id, path, size, packed, textures, human, info))
                source = TextFormat("%s", GetFileName(scripts.paths[i]));
        }
        UnloadDirectoryFiles(scripts);
    }
    if (source != NULL)
        return source;
    char humans[AUTHVIEW_PATH_SIZE];
    snprintf(humans, sizeof(humans), "%s/%s/%s/CHARA.chrt", g_auth.sceneFolder, AUTHVIEW_HUMANS, id);
    if (g_auth.sceneFolder[0] != '\0' && ScriptFileModel(humans, id, path, size, packed, textures, human, info))
        return TextFormat("%s/%s/CHARA.chrt", AUTHVIEW_HUMANS, id);
    if (g_auth.sceneFolder[0] != '\0' && (source = HumanPackageModel(id, humanFiles, path, size, packed, textures, human, info)) != NULL)
        return source;
    if (mapScript != NULL && ScriptModel(mapScript, id, mapFolder, NULL, NULL, path, size, packed, textures, human, info))
        return g_auth.cutscene >= 0 ? "mapinfo.bin" : "mapinfo.bin of the map";
    if (g_auth.sceneFolder[0] != '\0' && (source = StreamPackageModel(id, humanFiles, path, size, packed, textures, human, info)) != NULL)
        return source;
    return NULL;
}

// Index of the loaded model of a file (on disk, or packed: path is then its package and name), for one character.
// Returns -1 if it can't be loaded
static int LoadModelFile(const char* path, const IPAC_File* packed, const IPAC_FileList* textures) {
    AUTHView_Model* model = &g_auth.models[g_auth.modelCount++];
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->loaded = packed != NULL ? MT5Render_LoadPackedModel(packed, textures, &model->model) :
        MT5Render_LoadModel(path, &model->model);
    if (model->loaded) {
        model->lift = -MT5Render_Bottom(&model->model);
        model->top = MT5Render_Top(&model->model);
        model->body = MT5Pose_FindBody(&model->model.mesh, &model->bodyNodes);
        if (model->body)
            model->pose = malloc((model->model.mesh.nodeCount + 1) * sizeof(Matrix));
    }
    return model->loaded ? (int)(model - g_auth.models) : -1;
}

bool AUTHModels_LoadCharacters(void) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    g_auth.models = calloc(sequence->characterCount + 1, sizeof(AUTHView_Model));
    g_auth.characterModels = calloc(sequence->characterCount + 1, sizeof(AUTHView_CharacterModel));
    g_auth.instances = calloc(sequence->characterCount + 1, sizeof(MT5Render_Instance));
    g_auth.humanFiles = calloc(sequence->characterCount + 1, sizeof(IPAC_FileList));
    if (g_auth.models == NULL || g_auth.characterModels == NULL || g_auth.instances == NULL || g_auth.humanFiles == NULL)
        return false;

    // The characters of the map
    char mapFolder[AUTHVIEW_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(g_auth.mapinfoPath));
    MAPINFO_File mapinfo;
    CHRT_Script mapScript;
    bool hasMapinfo = FileExists(g_auth.mapinfoPath) && MAPINFO_Load(g_auth.mapinfoPath, &mapinfo);
    bool hasMapScript = hasMapinfo && mapinfo.characters != NULL &&
        CHRT_LoadFromMemory(mapinfo.characters, mapinfo.charactersSize, g_auth.mapinfoPath, &mapScript);

    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        const AUTH_Character* character = &sequence->characters[i];
        AUTHView_CharacterModel* characterModel = &g_auth.characterModels[i];
        characterModel->model = -1;
        if (!HasMovement(character))
            continue;
        char path[AUTHVIEW_PATH_SIZE];
        const IPAC_File* packed = NULL;
        const IPAC_FileList* textures = NULL;
        characterModel->source = FindCharacterModel(character->id, hasMapScript ? &mapScript : NULL, mapFolder, &g_auth.humanFiles[i],
            path, sizeof(path), &packed, &textures, &characterModel->human, NULL);
        if (characterModel->source != NULL) {
            // A copy of the source is kept: TextFormat reuses its buffers
            char* source = malloc(strlen(characterModel->source) + 1);
            if (source != NULL)
                strcpy(source, characterModel->source);
            characterModel->source = source;
            characterModel->model = LoadModelFile(path, packed, textures);
        }
        if (characterModel->model < 0)
            Log_Warning("[AUTHView_Open]: No model for %s (%s)\n", character->id, characterModel->source != NULL ? "invalid model file" : "not found");
        else
            Log_Debug("  %s: %s (%s)\n", character->id, GetFileName(path), characterModel->source);
    }
    if (hasMapScript)
        CHRT_Free(&mapScript);
    if (hasMapinfo)
        MAPINFO_Free(&mapinfo);
    return true;
}

static bool IsDeclared(uint32_t idValue) {
    for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
        if (g_auth.sequence.characters[i].idValue == idValue)
            return true;
    }
    return false;
}

// The characters placed or attached by the program that the AUTH file doesn't declare, with their model and the
// position and angle of their script
bool AUTHModels_LoadPlaced(void) {
    const SCNCutscene* program = &g_auth.program;
    if (program->placeCount + program->attachCount == 0)
        return true;
    g_auth.placed = calloc(program->placeCount + program->attachCount, sizeof(AUTHView_Placed));
    if (g_auth.placed == NULL)
        return false;
    for (uint32_t i = 0; i < program->placeCount + program->attachCount; i++) {
        uint32_t idValue = i < program->placeCount ? program->places[i].character : program->attaches[i - program->placeCount].character;
        bool known = IsDeclared(idValue);
        for (uint32_t p = 0; p < g_auth.placedCount && !known; p++)
            known = g_auth.placed[p].idValue == idValue;
        if (!known) {
            AUTHView_Placed* placed = &g_auth.placed[g_auth.placedCount++];
            placed->idValue = idValue;
            memcpy(placed->id, &idValue, 4);
            placed->model = -1;
        }
    }
    uint32_t capacity = g_auth.sequence.characterCount + g_auth.placedCount + 1;
    AUTHView_Model* models = realloc(g_auth.models, capacity * sizeof(AUTHView_Model));
    if (models != NULL)
        g_auth.models = models;
    MT5Render_Instance* instances = realloc(g_auth.instances, capacity * sizeof(MT5Render_Instance));
    if (instances != NULL)
        g_auth.instances = instances;
    if (models == NULL || instances == NULL)
        return false;
    memset(g_auth.models + g_auth.modelCount, 0, (capacity - g_auth.modelCount) * sizeof(AUTHView_Model));

    char mapFolder[AUTHVIEW_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(g_auth.mapinfoPath));
    MAPINFO_File mapinfo;
    CHRT_Script mapScript;
    bool hasMapinfo = FileExists(g_auth.mapinfoPath) && MAPINFO_Load(g_auth.mapinfoPath, &mapinfo);
    bool hasMapScript = hasMapinfo && mapinfo.characters != NULL &&
        CHRT_LoadFromMemory(mapinfo.characters, mapinfo.charactersSize, g_auth.mapinfoPath, &mapScript);
    for (uint32_t i = 0; i < g_auth.placedCount; i++) {
        AUTHView_Placed* placed = &g_auth.placed[i];
        char path[AUTHVIEW_PATH_SIZE];
        const IPAC_File* packed = NULL;
        const IPAC_FileList* textures = NULL;
        CHRT_Character info;
        const char* source = ProgramPackageModel(placed->idValue, placed->id, &placed->files, path, sizeof(path), &packed,
            &textures, &placed->human, &info);
        if (source == NULL)
            source = FindCharacterModel(placed->id, hasMapScript ? &mapScript : NULL, mapFolder, &placed->files, path,
                sizeof(path), &packed, &textures, &placed->human, &info);
        if (source == NULL) {
            Log_Warning("[AUTHView_Open]: No model for %s, placed by the program (not found)\n", placed->id);
            continue;
        }
        char* copy = malloc(strlen(source) + 1);
        if (copy != NULL)
            strcpy(copy, source);
        placed->source = copy;
        memcpy(placed->position, info.position, sizeof(placed->position));
        memcpy(placed->angle, info.angle, sizeof(placed->angle));
        placed->model = LoadModelFile(path, packed, textures);
        Log_Debug("  %s: %s (%s), placed by the program\n", placed->id, GetFileName(path), placed->source);
    }
    if (hasMapScript)
        CHRT_Free(&mapScript);
    if (hasMapinfo)
        MAPINFO_Free(&mapinfo);
    return true;
}

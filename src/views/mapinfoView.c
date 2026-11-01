#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "core/file.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/map/mapinfo.h"
#include "formats/script/chrt.h"
#include "views/mt5Render.h"
#include "views/freeCamera.h"
#include "views/listView.h"
#include "views/scene3D.h"
#include "views/mapinfoView.h"

#define MAPINFOVIEW_LABEL_SIZE   160
#define MAPINFOVIEW_LIST_WIDTH   420.0f
#define MAPINFOVIEW_FOVY         45.0f
#define MAPINFOVIEW_CAMERA_FOVY  60.0f   // Of the cameras without a field of view curve
#define MAPINFOVIEW_ASPECT       (4.0f / 3.0f) // Of the screen of the game, for the cameras of the map
#define MAPINFOVIEW_HEIGHT       1.7f    // Of the characters, drawn as cylinders
#define MAPINFOVIEW_RADIUS       0.25f
#define MAPINFOVIEW_CIRCLE_SIDES 24

typedef enum {
    ITEM_INFO,
    ITEM_CAMERA,
    ITEM_LIGHT,
    ITEM_CHARACTER,
    ITEM_DOOR,
} ItemKind;

typedef struct {
    ItemKind    kind;
    uint32_t    index;  // In the cameras, lights, characters or doors
} Item;

typedef enum {
    LAYER_MAP,
    LAYER_DOORS,
    LAYER_GROUPS,           // One per COLS group, MAPINFO_GROUP_COUNT
    LAYER_LIGHTS = LAYER_GROUPS + MAPINFO_GROUP_COUNT,
    LAYER_CAMERAS,
    LAYER_CHARACTERS,
    LAYER_LABELS,
    LAYER_COUNT,
} Layer;

static const char* g_layerNames[LAYER_COUNT] = {
    "Map", "Doors", "COLI", "HGHT", "EVNT", "UNDU", "SOND", "PROP", "Lights", "Cameras", "Characters", "Labels",
};

static const Color g_groupColors[MAPINFO_GROUP_COUNT] = {
    { 235, 80, 70, 255 }, { 230, 210, 80, 255 }, { 190, 110, 240, 255 },
    { 80, 210, 220, 255 }, { 110, 210, 110, 255 }, { 245, 150, 60, 255 },
};

static const Color g_cameraColor = { 220, 220, 220, 255 };
static const Color g_selectedColor = { 255, 230, 60, 255 };
static const Color g_characterColor = { 90, 170, 255, 255 };

typedef struct {
    bool                open;
    char                filename[256];
    MAPINFO_File        file;
    CHRT_Script         script;
    uint32_t            characterCount;
    CHRT_Character*     characters;
    MT5Render           render;
    uint32_t            modelCount;
    MT5Render_Model*    models;         // The models of the map, then the models of the doors
    uint32_t            mapModelCount;
    uint32_t            instanceCount;
    MT5Render_Instance* instances;      // Each model of the map (mapModelCount), then each door with its model
    uint32_t            loadedDoors;    // Doors with their model
    const char**        doorErrors;     // Why the model of each door couldn't be loaded, NULL if it was
    uint32_t            itemCount;
    Item*               items;
    char                (*labels)[MAPINFOVIEW_LABEL_SIZE];
    char**              labelPointers;
    bool*               failedItems;    // Doors without their model
    int                 listScroll;
    int                 listFocus;
    int                 selected;       // Item, -1 if none
    bool                mapCamera;      // Show the map from the selected camera
    float               time;           // Seconds, for the animated cameras
    FreeCamera         camera;
    bool                fit;            // The camera is fitted to the map in the next draw, when the size of the view is known
    Vector3             min, max;       // Box around the map
    Vector3             center;
    float               radius;
} MAPINFOView_State;

// Kept when another map is opened. The collisions, the lights and the cameras are hidden at first
typedef struct {
    bool                layers[LAYER_COUNT];
    bool                fly;
} MAPINFOView_Options;

static MAPINFOView_State g_view;
static MAPINFOView_Options g_options = {
    .layers = { [LAYER_MAP] = true, [LAYER_DOORS] = true, [LAYER_CHARACTERS] = true, [LAYER_LABELS] = true },
};

static Vector3 LightPosition(const MAPINFO_Light* light) {
    return (Vector3) { light->position[0], light->position[1], light->position[2] };
}

static float CameraDuration(const MAPINFO_Camera* camera) {
    float duration = 0.0f;
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++) {
        if (camera->curves[c].count > 0)
            duration = fmaxf(duration, camera->curves[c].times[camera->curves[c].count - 1]);
    }
    return duration;
}

static uint32_t CameraKeys(const MAPINFO_Camera* camera) {
    uint32_t keys = 0;
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        keys = camera->curves[c].count > keys ? camera->curves[c].count : keys;
    return keys;
}

// The camera at the current time: the animated cameras are played in a loop
static Camera3D MapCamera(const MAPINFO_Camera* source) {
    float duration = CameraDuration(source);
    float time = duration > 0.0f ? fmodf(g_view.time, duration) : 0.0f;
    return Scene3D_CurveCamera(source->curves, time, MAPINFOVIEW_CAMERA_FOVY);
}

static const MAPINFO_Camera* SelectedCamera(void) {
    if (g_view.selected < 0 || g_view.items[g_view.selected].kind != ITEM_CAMERA)
        return NULL;
    return &g_view.file.cameras[g_view.items[g_view.selected].index];
}

// The characters of the CHRD chunk
static void LoadCharacters(void) {
    const MAPINFO_File* file = &g_view.file;
    if (file->characters == NULL || !CHRT_LoadFromMemory(file->characters, file->charactersSize, g_view.filename, &g_view.script))
        return;
    const CHRT_Script* script = &g_view.script;
    g_view.characters = calloc(script->commandCount + 1, sizeof(CHRT_Character));
    if (g_view.characters == NULL)
        return;
    for (uint32_t c = 0; c < script->commandCount; c++) {
        if (script->commands[c].parent == -1 && CHRT_IsCommand(&script->commands[c], "Character"))
            CHRT_ReadCharacter(script, c, &g_view.characters[g_view.characterCount++]);
    }
}

static Vector3 CharacterPosition(const CHRT_Character* character) {
    return (Vector3) { character->position[0], character->position[1], character->position[2] };
}

// Models of the map: MAP.mt5, MAP01.mt5... in the folder, in its subfolders (mpk00, map0...) and in its PKF/PKS files
static bool IsMapModel(const char* path) {
    const char* name = GetFileName(path);
    if (toupper((unsigned char)name[0]) != 'M' || toupper((unsigned char)name[1]) != 'A' || toupper((unsigned char)name[2]) != 'P')
        return false;
    for (name += 3; isdigit((unsigned char)*name); name++)
        ;
    return *name == '.';
}

// A model or CHRT file of the map: on disk, or in a PKF/PKS file of the map that isn't extracted, loaded in memory
typedef struct {
    const char*             path;   // NULL if the file is in memory
    const char*             name;
    const IPAC_File*        packed;
} MapFile;

// The external textures of the packed files are searched in the files of all the PKF/PKS files (e.g. the MAP.mt5 of
// mpk00.pks uses the textures of mpk00.pkf)
static bool LoadMapModel(const MapFile* file, const IPAC_FileList* packed, MT5Render_Model* model) {
    return file->packed != NULL ? MT5Render_LoadPackedModel(file->packed, packed, model) : MT5Render_LoadModel(file->path, model);
}

static bool LoadMapScript(const MapFile* file, CHRT_Script* script) {
    return file->packed != NULL ? CHRT_LoadFromMemory(file->packed->data, file->packed->size, file->name, script) : CHRT_Load(file->path, script);
}

// Model file (MT5 or CHRM) of a model name, without the $ and @ prefixes and the extension of the names of the CHRT
// scripts (e.g. "$DR01_009.MT5"). Returns -1 if there is none
static int ModelFile(const MapFile* files, uint32_t count, const char* name) {
    char base[64];
    while (*name == '$' || *name == '@')
        name++;
    snprintf(base, sizeof(base), "%s", name);
    char* dot = strchr(base, '.');
    if (dot != NULL)
        *dot = '\0';
    for (uint32_t i = 0; i < count; i++) {
        if (!IsFileExtension(files[i].name, ".chrt") && CHRT_EqualsIgnoreCase(GetFileNameWithoutExt(files[i].name), base))
            return (int)i;
    }
    return -1;
}

// Model file of the model of a door: the file with its name, or the file of the DefImage with its name in the CHRT
// scripts of the map (the CHRD chunk and the CHRT files, e.g. mpk00/CHARA.chrt). Returns -1 if there is none
static int DoorModelFile(const MapFile* files, uint32_t count, const CHRT_Script* scripts, uint32_t scriptCount, const char* name) {
    int file = ModelFile(files, count, name);
    for (uint32_t i = 0; file < 0 && i < scriptCount; i++) {
        const char* image = CHRT_ImageFile(&scripts[i], name);
        if (image != NULL)
            file = ModelFile(files, count, image);
    }
    return file;
}

// The files of the PKF and PKS files of the folder that aren't extracted (no folder named as the file next to it)
static void LoadPacks(const char* folder, IPAC_FileList* packed) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".pkf;.pks", false);
    for (unsigned int i = 0; i < files.count; i++) {
        char extracted[1024];
        File_GetOutputFolder(files.paths[i], "", extracted, sizeof(extracted));
        if (DirectoryExists(extracted))
            continue;
        if (IsFileExtension(files.paths[i], ".pkf"))
            PKF_LoadFiles(files.paths[i], packed);
        else
            PKS_LoadFiles(files.paths[i], packed);
    }
    UnloadDirectoryFiles(files);
}

static void AddInstance(uint32_t model, Matrix transform) {
    MT5Render_Model* instanced = &g_view.models[model];
    g_view.instances[g_view.instanceCount++] = (MT5Render_Instance) { instanced, transform,
        Vector3Transform(instanced->mesh.center, transform), 0 };
}

// The models of the map, and the models of the doors at their placements, from the MT5 and CHRM files of the folder and
// of one level of subfolders (the extracted PKF files of the map), and of the PKF and PKS files of the folder that
// aren't extracted. Each file is loaded once
static void LoadModels(const char* folder) {
    const MAPINFO_File* file = &g_view.file;
    FilePathList paths = LoadDirectoryFilesEx(folder, ".mt5;.chrm;.chrt", true);
    IPAC_FileList packed = { 0 };
    LoadPacks(folder, &packed);
    uint32_t capacity = paths.count + packed.count;

    MapFile* files = calloc(capacity + 1, sizeof(MapFile));
    uint32_t count = 0;
    int* fileModels = malloc((capacity + 1) * sizeof(int)); // Model of each file, -1 if not loaded
    CHRT_Script* scripts = calloc(capacity + 2, sizeof(CHRT_Script)); // The CHRD chunk and the CHRT files
    uint32_t scriptCount = 0;
    g_view.models = calloc(capacity + 1, sizeof(MT5Render_Model));
    g_view.instances = calloc(capacity + file->doorCount + 1, sizeof(MT5Render_Instance));
    g_view.doorErrors = calloc(file->doorCount + 1, sizeof(const char*));
    if (files != NULL && fileModels != NULL && scripts != NULL && g_view.models != NULL &&
        g_view.instances != NULL && g_view.doorErrors != NULL) {
        for (unsigned int i = 0; i < paths.count; i++) {
            const char* relative = paths.paths[i] + strlen(folder);
            int separators = 0;
            for (const char* p = relative + 1; *p != '\0'; p++)
                separators += *p == '/' || *p == '\\';
            if (separators <= 1)
                files[count++] = (MapFile) { paths.paths[i], GetFileName(paths.paths[i]), NULL };
        }
        // The files of the folder with the name of a packed file are used instead of it (e.g. map.mt5 is the MAP.mt5 of
        // mpk00.pks with its textures)
        uint32_t diskCount = count;
        for (uint32_t i = 0; i < packed.count; i++) {
            char name[IPAC_FILENAME_SIZE];
            snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(packed.files[i].filename));
            bool onDisk = false;
            for (uint32_t d = 0; d < diskCount && !onDisk; d++)
                onDisk = CHRT_EqualsIgnoreCase(GetFileNameWithoutExt(files[d].name), name);
            if (!onDisk && IsFileExtension(packed.files[i].filename, ".mt5;.chrm;.chrt"))
                files[count++] = (MapFile) { NULL, packed.files[i].filename, &packed.files[i] };
        }

        if (g_view.script.commandCount > 0)
            scripts[scriptCount++] = g_view.script;
        for (uint32_t i = 0; i < count; i++) {
            fileModels[i] = -1;
            if (file->doorCount > 0 && IsFileExtension(files[i].name, ".chrt") && LoadMapScript(&files[i], &scripts[scriptCount]))
                scriptCount++;
            else if (IsFileExtension(files[i].name, ".mt5") && IsMapModel(files[i].name) &&
                LoadMapModel(&files[i], &packed, &g_view.models[g_view.modelCount])) {
                fileModels[i] = (int)g_view.modelCount;
                AddInstance(g_view.modelCount++, MatrixIdentity());
            }
        }
        g_view.mapModelCount = g_view.modelCount;

        for (uint32_t i = 0; i < file->doorCount; i++) {
            const MAPINFO_Door* door = &file->doors[i];
            int f = DoorModelFile(files, count, scripts, scriptCount, door->model);
            if (f < 0) {
                g_view.doorErrors[i] = "no model file";
                continue;
            }
            if (fileModels[f] < 0 && LoadMapModel(&files[f], &packed, &g_view.models[g_view.modelCount]))
                fileModels[f] = (int)g_view.modelCount++;
            if (fileModels[f] < 0) {
                g_view.doorErrors[i] = "invalid model file";
                continue;
            }
            // As the nodes of the models: scale, rotation and translation
            Matrix transform = MatrixMultiply(MatrixScale(door->scale[0], door->scale[1], door->scale[2]), MatrixRotateY(door->angle * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixTranslate(door->position[0], door->position[1], door->position[2]));
            AddInstance((uint32_t)fileModels[f], transform);
            g_view.loadedDoors++;
        }
    }
    // The script of the CHRD chunk is kept
    for (uint32_t i = g_view.script.commandCount > 0 ? 1 : 0; scripts != NULL && i < scriptCount; i++)
        CHRT_Free(&scripts[i]);
    free(scripts);
    free(files);
    free(fileModels);
    IPAC_FreeFiles(&packed);
    UnloadDirectoryFiles(paths);
}

// Point of a shape in the scene: the shapes have x and z coordinates
static Vector3 ShapePoint(float x, float y, float z) {
    return (Vector3) { x, y, -z };
}

static void AddPoint(Vector3 point, Vector3* min, Vector3* max) {
    *min = Vector3Min(*min, point);
    *max = Vector3Max(*max, point);
}

// Box around the models of the map, or around the cameras, lights and shapes if there is no model. The characters are
// not included: they can be far from the map
static void ComputeBounds(void) {
    const MAPINFO_File* file = &g_view.file;
    Vector3 min = { INFINITY, INFINITY, INFINITY }, max = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t i = 0; i < g_view.mapModelCount; i++) {
        const MT5Mesh* mesh = &g_view.models[i].mesh;
        Vector3 extent = { mesh->radius, mesh->radius, mesh->radius };
        AddPoint(Vector3Subtract(mesh->center, extent), &min, &max);
        AddPoint(Vector3Add(mesh->center, extent), &min, &max);
    }
    if (g_view.mapModelCount == 0) {
        for (uint32_t i = 0; i < file->cameraCount; i++)
            AddPoint(MapCamera(&file->cameras[i]).position, &min, &max);
        for (uint32_t i = 0; i < file->lightCount; i++)
            AddPoint(LightPosition(&file->lights[i]), &min, &max);
        for (uint32_t i = 0; i < file->shapeCount; i++) {
            // The points of the shape, or its center (circles: radius, x, z; rectangles: x, z, half axes)
            const MAPINFO_Shape* shape = &file->shapes[i];
            uint32_t first = shape->type == MAPINFO_SHAPE_CIRCLE ? 1 : 0;
            uint32_t end = shape->type == MAPINFO_SHAPE_CIRCLE ? 3 : shape->type == MAPINFO_SHAPE_RECTANGLE ? 2 : shape->valueCount;
            for (uint32_t v = first; v < end && v + 1 < shape->valueCount; v += 2)
                AddPoint(ShapePoint(shape->values[v], 0.0f, shape->values[v + 1]), &min, &max);
        }
    }
    if (min.x > max.x) {
        min = (Vector3) { -1.0f, 0.0f, -1.0f };
        max = (Vector3) { 1.0f, 2.0f, 1.0f };
    }
    g_view.min = min;
    g_view.max = max;
    g_view.center = Vector3Scale(Vector3Add(min, max), 0.5f);
    g_view.radius = fmaxf(Vector3Distance(min, max) * 0.5f, 2.0f);
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, g_view.center, (Vector3) { 0.0f, 0.0f, 1.0f }, g_view.radius, MAPINFOVIEW_FOVY);
    g_view.camera.pitch = 0.8f;
    g_view.fit = true;
}

// Fits the camera to the models of the map and of the doors, or to the corners of the bounds if there is no model
static void FitCamera(Rectangle area) {
    g_view.fit = false;
    uint32_t count;
    Vector3* points = MT5Render_InstanceVertices(g_view.instances, g_view.instanceCount, 8, &count);
    if (points == NULL)
        return;
    if (count == 0) {
        for (int i = 0; i < 8; i++)
            points[count++] = (Vector3) { (i & 1) ? g_view.max.x : g_view.min.x, (i & 2) ? g_view.max.y : g_view.min.y,
                (i & 4) ? g_view.max.z : g_view.min.z };
    }
    FreeCamera_Fit(&g_view.camera, points, count, area.width / area.height);
    free(points);
}

// The failed items are marked in the list
static void AddItem(ItemKind kind, uint32_t index, const char* text, bool failed) {
    g_view.items[g_view.itemCount] = (Item) { kind, index };
    snprintf(g_view.labels[g_view.itemCount], MAPINFOVIEW_LABEL_SIZE, "%s%s", failed ? LISTVIEW_FAILED : "", text);
    g_view.labelPointers[g_view.itemCount] = g_view.labels[g_view.itemCount];
    g_view.failedItems[g_view.itemCount] = failed;
    g_view.itemCount++;
}

static const char* ScnText(uint32_t version) {
    return version == 0x20000 ? "SH-4 code" : version == 0x100 ? "bytecode" : "unknown version";
}

// Information lines, then the cameras, the lights, the characters and the doors
static bool CreateItems(void) {
    const MAPINFO_File* file = &g_view.file;
    uint32_t count = 16 + file->bankCount + file->cameraCount + file->lightCount + g_view.characterCount + file->doorCount;
    g_view.items = malloc(count * sizeof(Item));
    g_view.labels = malloc(count * sizeof(*g_view.labels));
    g_view.labelPointers = malloc(count * sizeof(*g_view.labelPointers));
    g_view.failedItems = malloc(count * sizeof(bool));
    if (g_view.items == NULL || g_view.labels == NULL || g_view.labelPointers == NULL || g_view.failedItems == NULL)
        return false;

    const MAPINFO_Chunk* scn = MAPINFO_FindChunk(file, "SCN3");
    if (scn != NULL)
        AddItem(ITEM_INFO, 0, TextFormat("SCN3: %s (0x%X), 0x%X bytes", ScnText(file->scnVersion), file->scnVersion, scn->size), false);
    uint32_t groupCounts[MAPINFO_GROUP_COUNT] = { 0 };
    for (uint32_t i = 0; i < file->shapeCount; i++)
        groupCounts[file->shapes[i].group]++;
    if (file->shapeCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("COLS: %u COLI, %u HGHT, %u EVNT, %u UNDU, %u SOND, %u PROP", groupCounts[0],
            groupCounts[1], groupCounts[2], groupCounts[3], groupCounts[4], groupCounts[5]), false);
    if (file->fog.present)
        AddItem(ITEM_INFO, 0, TextFormat("Fog: %g to %g, density %g, color #%06X", file->fog.nearDistance, file->fog.farDistance,
            file->fog.density, file->fog.color & 0xFFFFFF), false);
    if (file->cutsceneCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("SCEX: %u cutscenes", file->cutsceneCount), false);
    for (uint32_t i = 0; i < file->bankCount; i++) {
        if (strcmp(file->banks[i].name, "FREE") != 0)
            AddItem(ITEM_INFO, 0, TextFormat("Sound bank %u: %s", file->banks[i].slot, file->banks[i].name), false);
    }
    const char* values[] = { "MAPR", "MAPT", "DIRT", "SCRL" };
    for (int i = 0; i < 4; i++) {
        uint32_t value;
        if (MAPINFO_ChunkValue(file, values[i], &value))
            AddItem(ITEM_INFO, 0, TextFormat("%s: %d", values[i], (int32_t)value), false);
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

    for (uint32_t i = 0; i < file->cameraCount; i++) {
        const MAPINFO_Camera* camera = &file->cameras[i];
        uint32_t keys = CameraKeys(camera);
        Camera3D view = MapCamera(camera);
        AddItem(ITEM_CAMERA, i, keys > 1 ?
            TextFormat("Camera %s  %u keys, %.2f s  flags 0x%X", camera->id, keys, CameraDuration(camera), camera->flags) :
            TextFormat("Camera %s  (%.2f, %.2f, %.2f)  flags 0x%X", camera->id, view.position.x, view.position.y, view.position.z, camera->flags), false);
    }
    for (uint32_t i = 0; i < file->lightCount; i++) {
        const MAPINFO_Light* light = &file->lights[i];
        AddItem(ITEM_LIGHT, i, TextFormat("Light %u.%u  (%.2f, %.2f, %.2f)  color (%.2f, %.2f, %.2f)", light->group, light->id,
            light->position[0], light->position[1], light->position[2], light->color[0], light->color[1], light->color[2]), false);
    }
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRT_Character* character = &g_view.characters[i];
        AddItem(ITEM_CHARACTER, i, character->positioned ?
            TextFormat("Character %s  (%.2f, %.2f, %.2f)%s", character->id, character->position[0], character->position[1],
                character->position[2], character->sleep ? "  sleep" : "") :
            TextFormat("Character %s  no position%s", character->id, character->sleep ? "  sleep" : ""), false);
    }
    for (uint32_t i = 0; i < file->doorCount; i++) {
        const MAPINFO_Door* door = &file->doors[i];
        const char* error = g_view.doorErrors != NULL ? g_view.doorErrors[i] : NULL;
        AddItem(ITEM_DOOR, i, TextFormat("%s %s  (%.2f, %.2f, %.2f)  %.1f deg%s%s", door->type == MAPINFO_DOOR_DOOR ? "Door" : "Model",
            door->model, door->position[0], door->position[1], door->position[2], door->angle, error != NULL ? "  " : "",
            error != NULL ? error : ""), error != NULL);
    }
    return true;
}

void MAPINFOView_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    for (uint32_t i = 0; i < g_view.modelCount; i++)
        MT5Render_FreeModel(&g_view.models[i]);
    free(g_view.models);
    free(g_view.instances);
    free((void*)g_view.doorErrors);
    MT5Render_Free(&g_view.render);
    free(g_view.characters);
    free(g_view.items);
    free(g_view.labels);
    free(g_view.labelPointers);
    free(g_view.failedItems);
    CHRT_Free(&g_view.script);
    MAPINFO_Free(&g_view.file);
    memset(&g_view, 0, sizeof(g_view));
}

bool MAPINFOView_Open(const char* filepath) {
    MAPINFO_File file;
    if (!MAPINFO_Load(filepath, &file))
        return false;

    MAPINFOView_Close();
    g_view.file = file;
    snprintf(g_view.filename, sizeof(g_view.filename), "%s", filepath);
    LoadCharacters();
    MT5Render_Init(&g_view.render);
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    LoadModels(folder);
    // After the models, for the doors whose model couldn't be loaded
    if (!CreateItems()) {
        fputs("[MAPINFOView_Open]: Memory error\n", stderr);
        MAPINFOView_Close();
        return false;
    }
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.selected = -1;
    g_view.listFocus = -1;

    printf("[MAPINFOView_Open]: %u chunks, %u cameras, %u lights, %u shapes, %u characters, %u map models, %u of %u doors\n",
        file.chunkCount, file.cameraCount, file.lightCount, file.shapeCount, g_view.characterCount, g_view.mapModelCount,
        g_view.loadedDoors, file.doorCount);
    g_view.open = true;
    return true;
}

bool MAPINFOView_IsOpen(void) {
    return g_view.open;
}

// A camera of the list shows the map from it; a light or a character is looked at with the free camera
static void Select(int item) {
    g_view.selected = item;
    if (item < 0)
        return;
    const Item* selected = &g_view.items[item];
    if (selected->kind == ITEM_CAMERA) {
        g_view.mapCamera = true;
        g_view.time = 0.0f;
        return;
    }
    Vector3 target;
    if (selected->kind == ITEM_LIGHT)
        target = LightPosition(&g_view.file.lights[selected->index]);
    else if (selected->kind == ITEM_CHARACTER && g_view.characters[selected->index].positioned)
        target = Vector3Add(CharacterPosition(&g_view.characters[selected->index]), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.5f, 0.0f });
    else if (selected->kind == ITEM_DOOR) {
        const float* position = g_view.file.doors[selected->index].position;
        target = (Vector3) { position[0], position[1] + 1.0f, position[2] };
    }
    else
        return;
    g_view.mapCamera = false;
    g_view.camera.target = target;
    g_view.camera.distance = 6.0f;
}

static void UpdateInput(Rectangle scene) {
    if (IsKeyPressed(KEY_V) && SelectedCamera() != NULL)
        g_view.mapCamera = !g_view.mapCamera;
    if (IsKeyPressed(KEY_F)) {
        // Flies from the position of the camera of the map, or of the free camera, looking around it
        const MAPINFO_Camera* source = g_view.mapCamera ? SelectedCamera() : NULL;
        if (source != NULL)
            FreeCamera_LookFrom(&g_view.camera, MapCamera(source));
        FreeCamera_SetFly(&g_view.camera, g_view.mapCamera || !g_view.camera.fly);
        g_view.mapCamera = false;
    }
    if (IsKeyPressed(KEY_R)) {
        ResetCamera();
        g_view.mapCamera = false;
    }
    if (g_view.fit)
        FitCamera(scene);
    if (!g_view.mapCamera)
        FreeCamera_Update(&g_view.camera, scene, 0.1f);
    g_view.time += Scene3D_FrameTime();
}

// The instances of the visible layers: the models of the map are before the doors
static void DrawModels(Camera3D camera) {
    uint32_t first = g_options.layers[LAYER_MAP] ? 0 : g_view.mapModelCount;
    uint32_t end = g_options.layers[LAYER_DOORS] ? g_view.instanceCount : g_view.mapModelCount;
    if (first < end)
        MT5Render_DrawInstances(&g_view.render, g_view.instances + first, end - first, camera);
}

// Height of the point of a shape: the height of HGHT, the height of each corner of UNDU, 0 for the rest
static float ShapeHeight(const MAPINFO_Shape* shape, uint32_t point) {
    if (shape->group == MAPINFO_GROUP_HGHT)
        return shape->height;
    if (shape->group == MAPINFO_GROUP_UNDU)
        return shape->heights[point < 4 ? point : 0];
    return 0.0f;
}

static void DrawShape(const MAPINFO_Shape* shape, Color color) {
    const float* v = shape->values;
    Vector3 points[4];
    switch (shape->type) {
    case MAPINFO_SHAPE_POINT: {
        float y = ShapeHeight(shape, 0);
        DrawLine3D(ShapePoint(v[0] - 0.2f, y, v[1]), ShapePoint(v[0] + 0.2f, y, v[1]), color);
        DrawLine3D(ShapePoint(v[0], y, v[1] - 0.2f), ShapePoint(v[0], y, v[1] + 0.2f), color);
        break;
    }
    case MAPINFO_SHAPE_SEGMENT:
        DrawLine3D(ShapePoint(v[0], ShapeHeight(shape, 0), v[1]), ShapePoint(v[2], ShapeHeight(shape, 1), v[3]), color);
        break;
    case MAPINFO_SHAPE_CIRCLE: {
        float y = ShapeHeight(shape, 0);
        for (int i = 0; i < MAPINFOVIEW_CIRCLE_SIDES; i++) {
            float a0 = i * 2.0f * PI / MAPINFOVIEW_CIRCLE_SIDES, a1 = (i + 1) * 2.0f * PI / MAPINFOVIEW_CIRCLE_SIDES;
            DrawLine3D(ShapePoint(v[1] + v[0] * cosf(a0), y, v[2] + v[0] * sinf(a0)),
                ShapePoint(v[1] + v[0] * cosf(a1), y, v[2] + v[0] * sinf(a1)), color);
        }
        break;
    }
    case MAPINFO_SHAPE_RECTANGLE: {
        // Center and two half axes
        const float signs[4][2] = { { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } };
        for (int i = 0; i < 4; i++)
            points[i] = ShapePoint(v[0] + signs[i][0] * v[2] + signs[i][1] * v[4], ShapeHeight(shape, i),
                v[1] + signs[i][0] * v[3] + signs[i][1] * v[5]);
        for (int i = 0; i < 4; i++)
            DrawLine3D(points[i], points[(i + 1) % 4], color);
        break;
    }
    case MAPINFO_SHAPE_TRIANGLE:
    case MAPINFO_SHAPE_POLYLINE:
    case MAPINFO_SHAPE_POLYGON: {
        uint32_t count = shape->valueCount / 2;
        bool closed = shape->type != MAPINFO_SHAPE_POLYLINE;
        for (uint32_t i = 0; i + 1 < count + (closed ? 1 : 0); i++) {
            uint32_t j = (i + 1) % count;
            DrawLine3D(ShapePoint(v[i * 2], ShapeHeight(shape, i), v[i * 2 + 1]),
                ShapePoint(v[j * 2], ShapeHeight(shape, j), v[j * 2 + 1]), color);
        }
        break;
    }
    default:
        break;
    }
}

static void DrawCameraGizmo(Camera3D camera, Color color) {
    Scene3D_DrawCameraGizmo(camera, 0.6f, MAPINFOVIEW_ASPECT, false, color);
}

// The layers over the map, always visible
static void DrawOverlays(void) {
    const MAPINFO_File* file = &g_view.file;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (uint32_t i = 0; i < file->shapeCount; i++) {
        const MAPINFO_Shape* shape = &file->shapes[i];
        if (g_options.layers[LAYER_GROUPS + shape->group])
            DrawShape(shape, g_groupColors[shape->group]);
    }
    // Not seen from the camera of the map: they would cover the view
    const MAPINFO_Camera* selectedCamera = SelectedCamera();
    if (!g_view.mapCamera && (g_options.layers[LAYER_CAMERAS] || selectedCamera != NULL)) {
        for (uint32_t i = 0; i < file->cameraCount; i++) {
            const MAPINFO_Camera* camera = &file->cameras[i];
            if (camera == selectedCamera) {
                Camera3D view = MapCamera(camera);
                DrawCameraGizmo(view, g_selectedColor);
                DrawLine3D(view.position, view.target, Fade(g_selectedColor, 0.5f));
            }
            else if (g_options.layers[LAYER_CAMERAS])
                DrawCameraGizmo(MapCamera(camera), Fade(g_cameraColor, 0.6f));
        }
    }
    if (g_options.layers[LAYER_LIGHTS]) {
        for (uint32_t i = 0; i < file->lightCount; i++) {
            const MAPINFO_Light* light = &file->lights[i];
            Vector3 position = LightPosition(light);
            Color color = ColorFromNormalized((Vector4) { light->color[0], light->color[1], light->color[2], 1.0f });
            bool selected = g_view.selected >= 0 && g_view.items[g_view.selected].kind == ITEM_LIGHT && g_view.items[g_view.selected].index == i;
            DrawSphere(position, selected ? 0.15f : 0.08f, color);
            Vector3 direction = { light->direction[0], light->direction[1], light->direction[2] };
            DrawLine3D(position, Vector3Add(position, Vector3Scale(direction, 0.5f)), selected ? g_selectedColor : color);
        }
    }
    if (g_options.layers[LAYER_CHARACTERS]) {
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            const CHRT_Character* character = &g_view.characters[i];
            if (!character->positioned)
                continue;
            bool selected = g_view.selected >= 0 && g_view.items[g_view.selected].kind == ITEM_CHARACTER && g_view.items[g_view.selected].index == i;
            Color color = selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor;
            DrawCylinderWires(CharacterPosition(character), MAPINFOVIEW_RADIUS, MAPINFOVIEW_RADIUS, MAPINFOVIEW_HEIGHT, 8, color);
            // Direction it faces: angle 0 faces -Z, as the bodies of the models
            float yaw = character->angle[1] * DEG2RAD;
            Vector3 eyes = Vector3Add(CharacterPosition(character), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.9f, 0.0f });
            DrawLine3D(eyes, Vector3Add(eyes, (Vector3) { -sinf(yaw) * 0.5f, 0.0f, -cosf(yaw) * 0.5f }), color);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

static void DrawLabel(Camera3D camera, Rectangle area, Font font, float fontSize, Vector3 position, const char* text, Color color) {
    Vector2 point;
    if (Scene3D_ScreenPoint(camera, area, position, &point))
        Scene3D_DrawLabel(point, text, font, fontSize, color);
}

static void DrawLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    const MAPINFO_File* file = &g_view.file;
    if (g_options.layers[LAYER_CHARACTERS]) {
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            const CHRT_Character* character = &g_view.characters[i];
            if (character->positioned)
                DrawLabel(camera, area, font, fontSize, Vector3Add(CharacterPosition(character), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT + 0.1f, 0.0f }),
                    character->id, character->sleep ? GRAY : g_characterColor);
        }
    }
    if (g_options.layers[LAYER_CAMERAS] && !g_view.mapCamera) {
        for (uint32_t i = 0; i < file->cameraCount; i++)
            DrawLabel(camera, area, font, fontSize, MapCamera(&file->cameras[i]).position, file->cameras[i].id,
                &file->cameras[i] == SelectedCamera() ? g_selectedColor : g_cameraColor);
    }
}

static void DrawScene(Rectangle area, Font font, float fontSize) {
    const MAPINFO_Camera* source = g_view.mapCamera ? SelectedCamera() : NULL;
    Camera3D camera;
    Rectangle view = area;
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    if (source != NULL) {
        camera = MapCamera(source);
        view = Scene3D_Letterbox(area, MAPINFOVIEW_ASPECT);
        FreeCamera_BeginCamera3D(camera, view, 0.05, g_view.radius * 4.0 + 100.0);
    }
    else
        camera = FreeCamera_Begin3D(&g_view.camera, area);

    DrawModels(camera);
    DrawOverlays();
    FreeCamera_End3D();
    if (g_options.layers[LAYER_LABELS])
        DrawLabels(camera, view, font, fontSize);
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// Toggles of the layers. The style of the application draws the pressed controls like the normal ones: highlight them
static void DrawLayerToggles(Rectangle area) {
    int pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
    float width = fminf(100.0f, (area.width - (LAYER_COUNT - 1) * 4.0f) / LAYER_COUNT);
    for (int i = 0; i < LAYER_COUNT; i++)
        GuiToggle((Rectangle) { area.x + i * (width + 4.0f), area.y, width, area.height }, g_layerNames[i], &g_options.layers[i]);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, pressedColor);
}

void MAPINFOView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float lineHeight = fontSize * 1.2f;
    const MAPINFO_File* file = &g_view.file;
    const MAPINFO_Camera* camera = SelectedCamera();
    const char* lines[3] = {
        g_view.filename,
        TextFormat("Map info, %u chunks: %u cameras, %u lights, %u collision and area shapes, %u characters, %u map models, %u doors%s",
            file->chunkCount, file->cameraCount, file->lightCount, file->shapeCount, g_view.characterCount, g_view.mapModelCount,
            file->doorCount, g_view.loadedDoors < file->doorCount ?
                TextFormat(" (%u without their model)", file->doorCount - g_view.loadedDoors) : ""),
        g_view.mapCamera && camera != NULL ?
            TextFormat("Camera %s   V: free camera   F: fly   R: reset the free camera   Click on the list: select a camera, light, character or door", camera->id) :
            TextFormat("%s   R: reset   V: camera of the map   Click on the list: select a camera, light, character or door",
                FreeCamera_Help(&g_view.camera)),
    };

    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin * 2.0f + 3 * lineHeight;
    Rectangle list = { area.x + margin, top, MAPINFOVIEW_LIST_WIDTH, area.height - margin * 3.0f - 3 * lineHeight };
    float panelX = list.x + list.width + margin;
    float panelWidth = area.width - list.width - margin * 3.0f;
    Rectangle controls = { panelX, area.y + area.height - margin - controlsHeight, panelWidth, controlsHeight };
    Rectangle scene = { panelX, top, panelWidth, controls.y - margin - top };
    if (list.height <= 0.0f || panelWidth <= 0.0f || scene.height <= 0.0f)
        return;
    UpdateInput(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    for (int i = 0; i < 3; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + margin, area.y + margin + i * lineHeight }, fontSize, 1.0f, LIGHTGRAY);

    int active = g_view.selected;
    int alignment = GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT);
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    ListView_Draw(list, g_view.labelPointers, g_view.failedItems, (int)g_view.itemCount, &g_view.listScroll, &active, &g_view.listFocus);
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, alignment);
    if (active != g_view.selected)
        Select(active);

    DrawScene(scene, font, fontSize);
    DrawLayerToggles(controls);
}

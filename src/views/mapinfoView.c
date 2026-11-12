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
#include "app/gameDB.h"
#include "formats/map/mapinfo.h"
#include "formats/script/chrt.h"
#include "render/mt5Render.h"
#include "render/freeCamera.h"
#include "render/scene3D.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/authView.h"
#include "views/mapFiles.h"
#include "views/mapProgram.h"
#include "views/mapinfoView.h"
#include "views/programView.h"

#define MAPINFOVIEW_LABEL_SIZE   160
#define MAPINFOVIEW_LIST_WIDTH   420.0f
#define MAPINFOVIEW_FOVY         45.0f
#define MAPINFOVIEW_CAMERA_FOVY  60.0f   // Of the cameras without a field of view curve
#define MAPINFOVIEW_ASPECT       (4.0f / 3.0f) // Of the screen of the game, for the cameras of the map
#define MAPINFOVIEW_HEIGHT       1.7f    // Of the characters, drawn as cylinders
#define MAPINFOVIEW_RADIUS       0.25f
#define MAPINFOVIEW_CIRCLE_SIDES 24
#define MAPINFOVIEW_PACKAGE      "mpk00" // Base package of the map (PKF/PKS files or their extracted folder)
#define MAPINFOVIEW_SCENE_MODELS "model/chara" // Models of the characters in the folder of the scene, for the maps without them

// Kinds of the items of the tree, in this order
typedef enum {
    ITEM_INFO,
    ITEM_MAP_MODEL,
    ITEM_DOOR,
    ITEM_CAMERA,
    ITEM_LIGHT,
    ITEM_SHAPE,
    ITEM_CHARACTER,
    ITEM_CUTSCENE,
    ITEM_SEQUENCE,      // A cutscene that the program loads in the table of cutscenes
    ITEM_KIND_COUNT,
} ItemKind;

typedef struct {
    ItemKind    kind;
    uint32_t    index;  // In the models of the map, doors, cameras, lights, shapes or characters
} Item;

// Groups of the tree, one per kind of item, shown or hidden by their eye (not the information)
typedef enum {
    LAYER_INFO,
    LAYER_MAP,
    LAYER_DOORS,
    LAYER_CAMERAS,
    LAYER_LIGHTS,
    LAYER_SHAPES,
    LAYER_CHARACTERS,
    LAYER_CUTSCENES,        // Not drawn: a cutscene is played by the view of the cutscenes
    LAYER_GROUPS,           // Subgroups of the shapes, one per COLS group, MAPINFO_GROUP_COUNT
    LAYER_COUNT = LAYER_GROUPS + MAPINFO_GROUP_COUNT,
} Layer;

static const char* g_layerNames[LAYER_COUNT] = {
    "Information", "Map", "Doors", "Cameras", "Lights", "Shapes", "Characters", "Cutscenes",
    // The COLS groups, by the meaning of their IDs: COLIsion, HeiGHT, EVeNT, UNDUlation, SOuND, PROPerty
    "Collisions", "Floor heights", "Events", "Slopes", "Sounds", "Properties",
};

static const char* g_shapeTypes[] = { "point", "segment", "polyline", "circle", "triangle", "rectangle", "polygon" };

static const Color g_groupColors[MAPINFO_GROUP_COUNT] = {
    { 235, 80, 70, 255 }, { 230, 210, 80, 255 }, { 190, 110, 240, 255 },
    { 80, 210, 220, 255 }, { 110, 210, 110, 255 }, { 245, 150, 60, 255 },
};

static const Color g_cameraColor = { 220, 220, 220, 255 };
static const Color g_selectedColor = { 255, 230, 60, 255 };
static const Color g_characterColor = { 90, 170, 255, 255 };

// A CHRT script of a package of the map, with the characters of a situation of the map (e.g. the furniture of the Hazuki
// house in common01 of scene/01/jomo, the night in yoru of scene/01/d000). A package with a character ID of a previous
// one is a variant of it (e.g. dcb0 to dcb4 of scene/01/dcbn): the program loads one of them
typedef struct {
    uint32_t            script;         // In g_view.scripts
    char                name[IPAC_FILENAME_SIZE]; // Of its PKF/PKS files or of their extracted folder
    uint32_t            characterCount;
    int                 variantOf;      // Previous package with one of its character IDs, -1 if none
    bool                shown;          // Its characters are shown; at first, those of the packages that aren't variants
    bool                expanded;       // In the tree; at first, the shown ones
} MapPackage;

// A character of the CHRD chunk or of a CHRT script of a package of the map. The displayed ones (positioned in the scene,
// not an arm of Ryo, with an image) that are in the map are drawn with their model, sleeping or not, if they are shown
typedef struct {
    CHRT_Character      info;           // Its image points into the buffer of its script. Its position and angle can be
                                        // edited
    CHRT_Character      original;       // As read from the script, to restore the edits
    uint32_t            script;         // In g_view.scripts
    int                 package;        // In g_view.packages, -1 for the CHRD chunk
    int                 model;          // In g_view.models, -1 if it isn't drawn with a model
    bool                shown;          // The CHRD chunk or a shown package, and no character shown before has its ID. Drawn
                                        // if it is visible in the tree too
    const char*         error;          // Why the model of a displayed character couldn't be loaded, NULL otherwise
} MapCharacter;

#define MAPINFOVIEW_CUTSCENE_IDS 5 // Characters of a cutscene named in the tree

// A cutscene of the SCEX chunk (a TRCK chunk), played by the view of the cutscenes
typedef struct {
    bool                valid;          // An AUTH cutscene, not an empty chunk
    uint32_t            duration;       // Frames
    uint32_t            characterCount;
    uint32_t            commandCount;
    uint32_t            motions;        // Commands of each kind
    uint32_t            voices;
    uint32_t            sounds;
    char                ids[MAPINFOVIEW_LABEL_SIZE]; // Of the characters, separated by spaces
} MapCutscene;

typedef struct {
    bool                open;
    char                filename[1024]; // Path of the open file
    MapCutscene*        cutscenes;      // file.cutsceneCount of them
    MAPINFO_File        file;
    SD_Data             program;        // What the program of the map does with it (mapProgram.h), empty if none
    uint32_t            sequenceCount;
    uint32_t*           sequences;      // The cutscenes that the program loads in the table of cutscenes: their first
                                        // reference (SD_SEQUENCE) in program, one per slot, file and package
    uint32_t            scriptCount;
    CHRT_Script*        scripts;        // The CHRD chunk, then the CHRT files of the map
    uint32_t            packageCount;
    MapPackage*         packages;       // The base package first, then those with more characters
    uint32_t            characterCount;
    MapCharacter*       characters;     // Of the CHRD chunk, then of each package
    uint32_t            modelledCharacters; // Shown with their model
    MT5Render           render;
    uint32_t            modelCount;
    MT5Render_Model*    models;         // The models of the map, then the models of the doors and of the characters
    uint32_t            mapModelCount;
    char                (*mapModelNames)[MAPINFOVIEW_LABEL_SIZE]; // File and package of each model of the map
    uint32_t            instanceCount;
    MT5Render_Instance* instances;      // Each model of the map (mapModelCount), then each door with its model
                                        // (loadedDoors), then the shown characters with their model (from
                                        // characterInstances)
    uint32_t            loadedDoors;    // Doors with their model
    uint32_t            characterInstances;
    MT5Render_Instance* visibleInstances; // The instances drawn, filled before drawing them
    int32_t*            doorInstances;  // Instance of each door, -1 if its model couldn't be loaded
    const char**        doorErrors;     // Why the model of each door couldn't be loaded, NULL if it was
    MAPINFO_Door*       originalDoors;  // The doors and the lights as read from the file, to restore the edits
    MAPINFO_Light*      originalLights;
    uint32_t            itemCount;
    Item*               items;          // Of each kind in ItemKind order, and in the order of their arrays
    uint32_t            firstItems[ITEM_KIND_COUNT]; // First item of each kind
    char                (*labels)[MAPINFOVIEW_LABEL_SIZE];
    bool*               failedItems;    // Doors and characters without their model
    bool*               visibleItems;   // Their eye in the tree
    Tree                tree;
    Properties          properties;     // Of the selected item
    bool                mapExpanded;    // The characters of the CHRD chunk in the tree
    int                 selected;       // Item, -1 if none
    bool                mapCamera;      // Show the map from the selected camera
    float               time;           // Seconds, for the animated cameras
    float               start[AUTH_CAMERA_CURVES]; // Active camera when the selected one started, for the transitions
                                                   // from the current camera (AUTH_CameraCurve order)
    FreeCamera         camera;
    bool                fit;            // The camera is fitted to the map in the next draw, when the size of the view is known
    Vector3             min, max;       // Box around the map
    Vector3             center;
    float               radius;
} MAPINFOView_State;

// Kept when another map is opened. The groups of shapes, the lights and the cameras are hidden at first
typedef struct {
    bool                layers[LAYER_COUNT];    // Visible
    bool                expanded[LAYER_COUNT];  // In the tree
    bool                labels;
    bool                fly;
} MAPINFOView_Options;

static MAPINFOView_State g_view;
static MAPINFOView_Options g_options = {
    .layers = { [LAYER_MAP] = true, [LAYER_DOORS] = true, [LAYER_SHAPES] = true, [LAYER_CHARACTERS] = true },
    .expanded = { [LAYER_INFO] = true, [LAYER_CAMERAS] = true, [LAYER_CHARACTERS] = true, [LAYER_CUTSCENES] = true },
    .labels = true,
};

// The eye of the item in the tree. All of them are visible before the tree is created
static bool IsItemVisible(ItemKind kind, uint32_t index) {
    return g_view.visibleItems == NULL || g_view.visibleItems[g_view.firstItems[kind] + index];
}

static bool IsSelected(ItemKind kind, uint32_t index) {
    return g_view.selected >= 0 && g_view.items[g_view.selected].kind == kind && g_view.items[g_view.selected].index == index;
}

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

static const MAPINFO_Camera* SelectedCamera(void) {
    if (g_view.selected < 0 || g_view.items[g_view.selected].kind != ITEM_CAMERA)
        return NULL;
    return &g_view.file.cameras[g_view.items[g_view.selected].index];
}

// Values of the curves of a camera at a time. The transitions from the current camera go from g_view.start to their
// first keys after time 0 (the keys at time 0 are a placeholder); the curves without such a key keep the start value.
// The relative ones are relative to a character that isn't known, so their position and target are moved from the start
// by their values, without rotation: not where the game shows them, but always along the same path
static void CameraValues(const MAPINFO_Camera* camera, float time, float values[AUTH_CAMERA_CURVES]) {
    bool relative = (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) && (camera->flags & MAPINFO_CAMERA_RELATIVE);
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++) {
        const AUTH_Curve* curve = &camera->curves[c];
        float fallback = c == AUTH_CAMERA_FOV ? MAPINFOVIEW_CAMERA_FOVY : 0.0f;
        float origin = relative && c <= AUTH_CAMERA_TARGET_Z ? g_view.start[c] : 0.0f;
        if (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) {
            uint32_t key = 0;
            while (key < curve->count && curve->times[key] <= 0.0f)
                key++;
            if (key == curve->count) {
                values[c] = g_view.start[c];
                continue;
            }
            if (time < curve->times[key]) {
                values[c] = Lerp(g_view.start[c], origin + curve->values[key], time / curve->times[key]);
                continue;
            }
        }
        values[c] = origin + AUTH_CurveValue(curve, time, fallback);
    }
}

// The values of the camera at the current time: the animated cameras are played in a loop. The transitions from the
// current camera that aren't selected are at their end
static void MapCameraValues(const MAPINFO_Camera* source, float values[AUTH_CAMERA_CURVES]) {
    float duration = CameraDuration(source);
    float time = duration > 0.0f ? fmodf(g_view.time, duration) : 0.0f;
    if ((source->flags & MAPINFO_CAMERA_FROM_CURRENT) && source != SelectedCamera())
        time = duration;
    CameraValues(source, time, values);
}

static Camera3D MapCamera(const MAPINFO_Camera* source) {
    float values[AUTH_CAMERA_CURVES];
    MapCameraValues(source, values);
    static const float zero = 0.0f;
    AUTH_Curve curves[AUTH_CAMERA_CURVES];
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        curves[c] = (AUTH_Curve) { 1, &zero, &values[c], &zero };
    return Scene3D_CurveCamera(curves, 0.0f, MAPINFOVIEW_CAMERA_FOVY);
}

// The active camera becomes the start of the transitions from the current camera: the camera of the map, or the free one
static void SetStart(void) {
    const MAPINFO_Camera* camera = g_view.mapCamera ? SelectedCamera() : NULL;
    float values[AUTH_CAMERA_CURVES];
    if (camera != NULL)
        MapCameraValues(camera, values);
    else {
        Camera3D view = FreeCamera_Camera3D(&g_view.camera);
        float free[AUTH_CAMERA_CURVES] = { view.position.x, view.position.y, view.position.z, view.target.x, view.target.y,
            view.target.z, 0.0f, view.fovy };
        memcpy(values, free, sizeof(values));
    }
    memcpy(g_view.start, values, sizeof(values));
}

static bool IsCharacterCommand(const CHRT_Script* script, uint32_t command) {
    return script->commands[command].parent == -1 && CHRT_IsCommand(&script->commands[command], "Character");
}

static uint32_t CountCharacters(const CHRT_Script* script) {
    uint32_t count = 0;
    for (uint32_t c = 0; c < script->commandCount; c++)
        count += IsCharacterCommand(script, c);
    return count;
}

// Appends the characters of a script of g_view.scripts, of a package of g_view.packages (-1 for the CHRD chunk)
static void AddCharacters(uint32_t script, int package) {
    const CHRT_Script* source = &g_view.scripts[script];
    MapCharacter* characters = realloc(g_view.characters, (g_view.characterCount + source->commandCount + 1) * sizeof(MapCharacter));
    if (characters == NULL)
        return;
    g_view.characters = characters;
    for (uint32_t c = 0; c < source->commandCount; c++) {
        if (!IsCharacterCommand(source, c))
            continue;
        MapCharacter* character = &characters[g_view.characterCount++];
        memset(character, 0, sizeof(*character));
        CHRT_ReadCharacter(source, c, &character->info);
        character->original = character->info;
        character->script = script;
        character->package = package;
        character->model = -1;
    }
}

static const char* PackageName(int package) {
    return package >= 0 ? g_view.packages[package].name : NULL;
}

static Vector3 CharacterPosition(const CHRT_Character* character) {
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
    const float* angle = character->angle;
    Matrix matrix = MatrixTranslate(0.0f, character->human ? -MT5Render_Bottom(model) : 0.0f, 0.0f);
    matrix = MatrixMultiply(matrix, MatrixRotateX(angle[0] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateY(angle[1] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateZ(angle[2] * DEG2RAD));
    return MatrixMultiply(matrix, MatrixTranslate(character->position[0], character->position[1], character->position[2]));
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

// A model name of the CHRT scripts without the $ and @ prefixes and the extension (e.g. "DR01_009" for "$DR01_009.MT5")
static void ModelBaseName(const char* name, char* base, size_t size) {
    while (*name == '$' || *name == '@')
        name++;
    snprintf(base, size, "%s", name);
    char* dot = strchr(base, '.');
    if (dot != NULL)
        *dot = '\0';
}

// Model file (MT5 or CHRM) of a model name of the CHRT scripts, preferably of a package (the variants can have models
// with the same name), of any package if it is NULL. Returns -1 if there is none
static int ModelFile(const MapFile* files, uint32_t count, const char* name, const char* package) {
    char base[64];
    ModelBaseName(name, base, sizeof(base));
    int found = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (IsFileExtension(files[i].name, ".chrt") || !CHRT_EqualsIgnoreCase(GetFileNameWithoutExt(files[i].name), base))
            continue;
        if (package == NULL || CHRT_EqualsIgnoreCase(files[i].package, package))
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
    for (uint32_t i = 0; file < 0 && i < g_view.scriptCount; i++) {
        const char* image = CHRT_ImageFile(&g_view.scripts[i], name);
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
    const char* image = CHRT_ImageFile(&g_view.scripts[character->script], name);
    for (uint32_t i = 0; image == NULL && i < g_view.scriptCount; i++)
        image = CHRT_ImageFile(&g_view.scripts[i], name);
    return image != NULL ? image : name;
}

// Model of a file, loaded the first time. Returns -1 if it can't be loaded
static int FileModel(const MapFile* files, int f, int* fileModels, const IPAC_FileList* packed) {
    if (fileModels[f] < 0 && LoadMapModel(&files[f], packed, &g_view.models[g_view.modelCount]))
        fileModels[f] = (int)g_view.modelCount++;
    return fileModels[f];
}

static void AddInstance(uint32_t model, Matrix transform) {
    MT5Render_Model* instanced = &g_view.models[model];
    g_view.instances[g_view.instanceCount++] = (MT5Render_Instance) { instanced, transform,
        Vector3Transform(instanced->mesh.center, transform), 0 };
}

// In the bounding sphere of a model of the map, or true if there is none. Some characters wait outside the map at
// positions below CHRT_OFF_SCENE, e.g. (300, 0, 0)
static bool InMapModels(Vector3 position) {
    for (uint32_t i = 0; i < g_view.mapModelCount; i++) {
        const MT5Mesh* mesh = &g_view.models[i].mesh;
        if (Vector3Distance(position, mesh->center) <= mesh->radius)
            return true;
    }
    return g_view.mapModelCount == 0;
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
    ModelBaseName(name, base, sizeof(base));
    for (uint32_t i = 0; i < *sceneModelCount; i++) {
        if (CHRT_EqualsIgnoreCase(sceneModels[i].name, base))
            return sceneModels[i].model;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s.mt5", GetPrevDirectoryPath(folder), MAPINFOVIEW_SCENE_MODELS, TextToLower(base));
    SceneModel* added = &sceneModels[(*sceneModelCount)++];
    snprintf(added->name, sizeof(added->name), "%s", base);
    added->model = FileExists(path) && MT5Render_LoadModel(path, &g_view.models[g_view.modelCount]) ? (int)g_view.modelCount++ : -1;
    return added->model;
}

// A character is shown if its package is (the CHRD chunk always is) and no character shown before has its ID: the
// first shown package with an ID wins. The instances of the characters are those of the shown ones with their model
static void ShowCharacters(void) {
    g_view.instanceCount = g_view.characterInstances;
    g_view.modelledCharacters = 0;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        MapCharacter* character = &g_view.characters[i];
        character->shown = character->package < 0 || g_view.packages[character->package].shown;
        for (uint32_t c = 0; c < i && character->shown; c++) {
            const MapCharacter* previous = &g_view.characters[c];
            character->shown = !previous->shown || previous->script == character->script || strcmp(previous->info.id, character->info.id) != 0;
        }
        if (character->shown && character->model >= 0 && IsItemVisible(ITEM_CHARACTER, i)) {
            AddInstance((uint32_t)character->model, CharacterTransform(&character->info, &g_view.models[character->model]));
            g_view.modelledCharacters++;
        }
    }
}

// As the nodes of the models: scale, rotation and translation
static Matrix DoorTransform(const MAPINFO_Door* door) {
    Matrix transform = MatrixMultiply(MatrixScale(door->scale[0], door->scale[1], door->scale[2]), MatrixRotateY(door->angle * DEG2RAD));
    return MatrixMultiply(transform, MatrixTranslate(door->position[0], door->position[1], door->position[2]));
}

// The instances of the models of the map, of the doors at their placements and of the shown characters in the map. The
// characters whose model isn't in the map are drawn with the model of the folder of the scene
static void PlaceModels(const char* folder, const MapFile* files, uint32_t count, int* fileModels, const IPAC_FileList* packed) {
    const MAPINFO_File* file = &g_view.file;
    for (uint32_t i = 0; i < count; i++) {
        if (IsMapModel(&files[i]) && FileModel(files, (int)i, fileModels, packed) >= 0) {
            snprintf(g_view.mapModelNames[g_view.instanceCount], MAPINFOVIEW_LABEL_SIZE, "%s%s%s", files[i].name,
                files[i].package[0] != '\0' ? "  " : "", files[i].package);
            AddInstance((uint32_t)fileModels[i], MatrixIdentity());
        }
    }
    g_view.mapModelCount = g_view.modelCount;

    for (uint32_t i = 0; i < file->doorCount; i++) {
        const MAPINFO_Door* door = &file->doors[i];
        g_view.doorInstances[i] = -1;
        int f = DoorModelFile(files, count, door->model);
        if (f < 0) {
            g_view.doorErrors[i] = "no model file";
            continue;
        }
        if (FileModel(files, f, fileModels, packed) < 0) {
            g_view.doorErrors[i] = "invalid model file";
            continue;
        }
        g_view.doorInstances[i] = (int32_t)g_view.instanceCount;
        AddInstance((uint32_t)fileModels[f], DoorTransform(door));
        g_view.loadedDoors++;
    }

    g_view.characterInstances = g_view.instanceCount;
    SceneModel* sceneModels = calloc(g_view.characterCount + 1, sizeof(SceneModel));
    uint32_t sceneModelCount = 0;
    for (uint32_t i = 0; i < g_view.characterCount && sceneModels != NULL; i++) {
        MapCharacter* character = &g_view.characters[i];
        if (!IsDisplayed(&character->info) || !InMapModels(CharacterPosition(&character->info)))
            continue;
        const char* name = CharacterModelName(character);
        int f = ModelFile(files, count, name, PackageName(character->package));
        int model = f >= 0 ? FileModel(files, f, fileModels, packed) : FolderSceneModel(folder, name, sceneModels, &sceneModelCount);
        if (model < 0)
            character->error = f < 0 ? "no model file" : "invalid model file";
        character->model = model;
    }
    free(sceneModels);
    ShowCharacters();
}

// The base package first, then those with more characters
static int ComparePackages(const void* a, const void* b) {
    const MapPackage* first = a;
    const MapPackage* second = b;
    bool firstBase = CHRT_EqualsIgnoreCase(first->name, MAPINFOVIEW_PACKAGE), secondBase = CHRT_EqualsIgnoreCase(second->name, MAPINFOVIEW_PACKAGE);
    if (firstBase != secondBase)
        return firstBase ? -1 : 1;
    if (first->characterCount != second->characterCount)
        return first->characterCount > second->characterCount ? -1 : 1;
    return strcmp(first->name, second->name);
}

static bool ShareCharacter(int package, int other) {
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        if (g_view.characters[i].package != package)
            continue;
        for (uint32_t j = 0; j < g_view.characterCount; j++) {
            if (g_view.characters[j].package == other && strcmp(g_view.characters[i].info.id, g_view.characters[j].info.id) == 0)
                return true;
        }
    }
    return false;
}

// The scripts of the map: the CHRD chunk, then the CHRT files of the packages (CHARA.chrt), with their characters
static void LoadScripts(const MapFile* files, uint32_t count) {
    const MAPINFO_File* file = &g_view.file;
    if (file->characters != NULL &&
        CHRT_LoadFromMemory(file->characters, file->charactersSize, g_view.filename, &g_view.scripts[g_view.scriptCount]))
        AddCharacters(g_view.scriptCount++, -1);
    g_view.packages = calloc(count + 1, sizeof(MapPackage));
    if (g_view.packages == NULL)
        return;
    for (uint32_t i = 0; i < count; i++) {
        if (!IsFileExtension(files[i].name, ".chrt") || !LoadMapScript(&files[i], &g_view.scripts[g_view.scriptCount]))
            continue;
        MapPackage* package = &g_view.packages[g_view.packageCount++];
        package->script = g_view.scriptCount++;
        snprintf(package->name, sizeof(package->name), "%s", files[i].package[0] != '\0' ? files[i].package : GetFileNameWithoutExt(files[i].name));
        package->characterCount = CountCharacters(&g_view.scripts[package->script]);
    }
    qsort(g_view.packages, g_view.packageCount, sizeof(MapPackage), ComparePackages);
    for (uint32_t p = 0; p < g_view.packageCount; p++)
        AddCharacters(g_view.packages[p].script, (int)p);
    for (uint32_t p = 0; p < g_view.packageCount; p++) {
        MapPackage* package = &g_view.packages[p];
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
static void LoadModels(const char* folder) {
    const MAPINFO_File* file = &g_view.file;
    MapFiles mapFiles;
    if (!MapFiles_Load(folder, ".mt5;.chrm;.chrt", &mapFiles))
        return;
    const MapFile* files = mapFiles.files;
    uint32_t count = mapFiles.count;
    int* fileModels = malloc((count + 1) * sizeof(int)); // Model of each file, -1 if not loaded
    g_view.scripts = calloc(count + 2, sizeof(CHRT_Script));
    g_view.doorErrors = calloc(file->doorCount + 1, sizeof(const char*));
    g_view.doorInstances = calloc(file->doorCount + 1, sizeof(int32_t));
    g_view.mapModelNames = calloc(count + 1, sizeof(*g_view.mapModelNames));
    if (fileModels != NULL && g_view.scripts != NULL && g_view.doorErrors != NULL && g_view.doorInstances != NULL &&
        g_view.mapModelNames != NULL) {
        for (uint32_t i = 0; i < count; i++)
            fileModels[i] = -1;

        LoadScripts(files, count);
        // The models of the files, and of the folder of the scene for the characters
        g_view.models = calloc(count + g_view.characterCount + 1, sizeof(MT5Render_Model));
        g_view.instances = calloc(count + file->doorCount + g_view.characterCount + 1, sizeof(MT5Render_Instance));
        g_view.visibleInstances = calloc(count + file->doorCount + g_view.characterCount + 1, sizeof(MT5Render_Instance));
        if (g_view.models != NULL && g_view.instances != NULL && g_view.visibleInstances != NULL)
            PlaceModels(folder, files, count, fileModels, &mapFiles.packed);
    }
    free(fileModels);
    MapFiles_Free(&mapFiles);
}

// Point of a shape in the scene: the shapes have x and z coordinates
static Vector3 ShapePoint(float x, float y, float z) {
    return (Vector3) { x, y, -z };
}

// Height of the point of a shape: the height of HGHT, the height of each corner of UNDU, 0 for the rest
static float ShapeHeight(const MAPINFO_Shape* shape, uint32_t point) {
    if (shape->group == MAPINFO_GROUP_HGHT)
        return shape->height;
    if (shape->group == MAPINFO_GROUP_UNDU)
        return shape->heights[point < 4 ? point : 0];
    return 0.0f;
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

// Fits the camera to the models of the map and of the doors, or to the corners of the bounds if there is no model. The
// characters are not included, as in the bounds
static void FitCamera(Rectangle area) {
    g_view.fit = false;
    uint32_t count;
    Vector3* points = MT5Render_InstanceVertices(g_view.instances, g_view.characterInstances, 8, &count);
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

static void AddItem(ItemKind kind, uint32_t index, const char* text, bool failed) {
    uint32_t item = g_view.itemCount++;
    g_view.items[item] = (Item) { kind, index };
    snprintf(g_view.labels[item], MAPINFOVIEW_LABEL_SIZE, "%s", text);
    g_view.failedItems[item] = failed;
    g_view.visibleItems[item] = true;
}

// The references of the program of the map (g_view.program) of a kind, with a number (any if negative) and a name (any
// if NULL), the names compared ignoring the case
static bool ProgramMatches(const SD_Reference* reference, SD_Kind kind, int32_t number, const char* name) {
    return reference->kind == kind && (number < 0 || reference->number == number) &&
        (name == NULL || CHRT_EqualsIgnoreCase(reference->name, name));
}

static uint32_t ProgramCount(SD_Kind kind, int32_t number, const char* name) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.program.count; i++)
        count += ProgramMatches(&g_view.program.references[i], kind, number, name);
    return count;
}

static bool SameReference(const SD_Reference* a, const SD_Reference* b) {
    return a->kind == b->kind && a->number == b->number && CHRT_EqualsIgnoreCase(a->name, b->name) &&
        CHRT_EqualsIgnoreCase(a->folder, b->folder);
}

// The references of a kind, each number, name and folder counted once
static uint32_t ProgramDistinct(SD_Kind kind) {
    uint32_t count = 0;
    const SD_Reference* references = g_view.program.references;
    for (uint32_t i = 0; i < g_view.program.count; i++) {
        bool first = references[i].kind == kind;
        for (uint32_t j = 0; j < i && first; j++)
            first = !SameReference(&references[j], &references[i]);
        count += first;
    }
    return count;
}

// "no", or the number of calls of the program and their addresses (up to MAPINFOVIEW_PROGRAM_CALLS of them)
#define MAPINFOVIEW_PROGRAM_CALLS 6
static const char* ProgramCalls(SD_Kind kind, int32_t number, const char* name) {
    static char text[MAPINFOVIEW_LABEL_SIZE];
    uint32_t count = ProgramCount(kind, number, name), shown = 0;
    if (count == 0)
        return "no";
    int length = snprintf(text, sizeof(text), "%u call%s at", count, count == 1 ? "" : "s");
    for (uint32_t i = 0; i < g_view.program.count && length < (int)sizeof(text); i++) {
        const SD_Reference* reference = &g_view.program.references[i];
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

// The distinct cutscenes that the program loads in the table of cutscenes. Returns false on memory error
static bool FindSequences(void) {
    g_view.sequences = calloc(g_view.program.count + 1, sizeof(uint32_t));
    if (g_view.sequences == NULL)
        return false;
    const SD_Reference* references = g_view.program.references;
    for (uint32_t i = 0; i < g_view.program.count; i++) {
        bool found = references[i].kind != SD_SEQUENCE;
        for (uint32_t j = 0; j < g_view.sequenceCount && !found; j++)
            found = SameReference(&references[g_view.sequences[j]], &references[i]);
        if (!found)
            g_view.sequences[g_view.sequenceCount++] = i;
    }
    return true;
}

static const SD_Reference* Sequence(uint32_t index) {
    return &g_view.program.references[g_view.sequences[index]];
}

// Its slot, its file and its package (or folder)
static const char* SequenceText(uint32_t index) {
    const SD_Reference* sequence = Sequence(index);
    return TextFormat("%3d  %s of %s", sequence->number, sequence->name, sequence->folder);
}

// Name of the subgroup of the characters of a package in the tree
static const char* PackageText(const MapPackage* package) {
    const char* loaded = ProgramCount(SD_PACKAGE, -1, package->name) > 0 ? ", loaded by the program" : "";
    return package->variantOf >= 0 ? TextFormat("%s, variant of %s%s", package->name, g_view.packages[package->variantOf].name,
        loaded) : TextFormat("%s%s", package->name, loaded);
}

// The center of the points of a shape (see DrawShape)
static Vector3 ShapeCenter(const MAPINFO_Shape* shape) {
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

static const char* ShapeType(const MAPINFO_Shape* shape) {
    return shape->type < sizeof(g_shapeTypes) / sizeof(g_shapeTypes[0]) ? g_shapeTypes[shape->type] : "unknown";
}

static const char* ScnText(uint32_t version) {
    return version == 0x20000 ? "SH-4 code" : version == 0x100 ? "bytecode" : "unknown version";
}

static void AddInfoItems(void) {
    const MAPINFO_File* file = &g_view.file;
    const GameDB_Map* map = GameDB_MapOfFile(g_view.filename);
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
        cutscenes += g_view.cutscenes[i].valid;
    if (file->cutsceneCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("SCEX: %u cutscenes, %u empty", cutscenes, file->cutsceneCount - cutscenes), false);
    if (g_view.program.count > 0) {
        AddItem(ITEM_INFO, 0, TextFormat("Program: loads %u packages and %u cutscenes, plays %u cutscenes",
            ProgramDistinct(SD_PACKAGE) + ProgramDistinct(SD_AFS_PACKAGE), ProgramDistinct(SD_SEQUENCE),
            ProgramDistinct(SD_CUTSCENE)), false);
        AddItem(ITEM_INFO, 0, TextFormat("Program: wakes %u characters, puts %u to sleep", ProgramDistinct(SD_WAKE),
            ProgramDistinct(SD_SLEEP)), false);
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
static bool ReadCutscenes(void) {
    const MAPINFO_File* file = &g_view.file;
    g_view.cutscenes = calloc(file->cutsceneCount + 1, sizeof(MapCutscene));
    if (g_view.cutscenes == NULL)
        return false;
    for (uint32_t i = 0; i < file->cutsceneCount; i++) {
        MapCutscene* cutscene = &g_view.cutscenes[i];
        AUTH_Sequence sequence;
        // The empty chunks are only a header
        if (file->cutscenes[i].size <= 8 || !AUTH_LoadFromMemory(file->data + file->cutscenes[i].offset, file->cutscenes[i].size,
            TextFormat("%s #%u", g_view.filename, i), &sequence))
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

static const char* FormatTime(float seconds) {
    int centiseconds = (int)(seconds * 100.0f);
    return TextFormat("%d:%02d.%02d", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

// The names of the characters of a cutscene (their ID for those without a name), separated by commas, up to max of
// them (followed by "..." if it has more)
static const char* CutsceneNames(const MapCutscene* cutscene, int max) {
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
    const MapCutscene* cutscene = &g_view.cutscenes[index];
    if (!cutscene->valid)
        return TextFormat("%3u  empty", index);
    return TextFormat("%3u  %s%s", index, CutsceneNames(cutscene, MAPINFOVIEW_CUTSCENE_IDS),
        ProgramCount(SD_CUTSCENE, (int32_t)index, NULL) > 0 ? "  (played)" : "");
}

// The items of each kind, in ItemKind order
static bool CreateItems(void) {
    const MAPINFO_File* file = &g_view.file;
    uint32_t count = 16 + file->bankCount + g_view.mapModelCount + file->doorCount + file->cameraCount + file->lightCount +
        file->shapeCount + g_view.characterCount + file->cutsceneCount + g_view.sequenceCount;
    g_view.items = malloc(count * sizeof(Item));
    g_view.labels = malloc(count * sizeof(*g_view.labels));
    g_view.failedItems = malloc(count * sizeof(bool));
    bool* visibleItems = malloc(count * sizeof(bool));
    if (g_view.items == NULL || g_view.labels == NULL || g_view.failedItems == NULL || visibleItems == NULL) {
        free(visibleItems);
        return false;
    }
    // Set at the end: all the items are visible before
    g_view.visibleItems = visibleItems;

    g_view.firstItems[ITEM_INFO] = g_view.itemCount;
    AddInfoItems();
    g_view.firstItems[ITEM_MAP_MODEL] = g_view.itemCount;
    for (uint32_t i = 0; i < g_view.mapModelCount; i++)
        AddItem(ITEM_MAP_MODEL, i, g_view.mapModelNames[i], false);
    g_view.firstItems[ITEM_DOOR] = g_view.itemCount;
    for (uint32_t i = 0; i < file->doorCount; i++) {
        const MAPINFO_Door* door = &file->doors[i];
        const char* error = g_view.doorErrors != NULL ? g_view.doorErrors[i] : NULL;
        AddItem(ITEM_DOOR, i, door->model, error != NULL);
    }
    g_view.firstItems[ITEM_CAMERA] = g_view.itemCount;
    for (uint32_t i = 0; i < file->cameraCount; i++) {
        AddItem(ITEM_CAMERA, i, file->cameras[i].id, false);
    }
    g_view.firstItems[ITEM_LIGHT] = g_view.itemCount;
    for (uint32_t i = 0; i < file->lightCount; i++) {
        AddItem(ITEM_LIGHT, i, TextFormat("%u.%u", file->lights[i].group, file->lights[i].id), false);
    }
    g_view.firstItems[ITEM_SHAPE] = g_view.itemCount;
    for (uint32_t i = 0; i < file->shapeCount; i++)
        AddItem(ITEM_SHAPE, i, TextFormat("%u  %s", i, ShapeType(&file->shapes[i])), false);
    g_view.firstItems[ITEM_CHARACTER] = g_view.itemCount;
    for (uint32_t i = 0; i < g_view.characterCount; i++)
        AddItem(ITEM_CHARACTER, i, GameDB_CharacterLabel(g_view.characters[i].info.id), g_view.characters[i].error != NULL);
    g_view.firstItems[ITEM_CUTSCENE] = g_view.itemCount;
    for (uint32_t i = 0; i < file->cutsceneCount; i++)
        AddItem(ITEM_CUTSCENE, i, CutsceneText(i), false);
    g_view.firstItems[ITEM_SEQUENCE] = g_view.itemCount;
    for (uint32_t i = 0; i < g_view.sequenceCount; i++)
        AddItem(ITEM_SEQUENCE, i, SequenceText(i), false);
    return true;
}

void MAPINFOView_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    for (uint32_t i = 0; i < g_view.modelCount; i++)
        MT5Render_FreeModel(&g_view.models[i]);
    free(g_view.models);
    free(g_view.instances);
    free(g_view.visibleInstances);
    free(g_view.doorInstances);
    free(g_view.mapModelNames);
    free((void*)g_view.doorErrors);
    free(g_view.originalDoors);
    free(g_view.originalLights);
    MT5Render_Free(&g_view.render);
    free(g_view.characters);
    free(g_view.packages);
    free(g_view.items);
    free(g_view.labels);
    free(g_view.failedItems);
    free(g_view.visibleItems);
    for (uint32_t i = 0; i < g_view.scriptCount; i++)
        CHRT_Free(&g_view.scripts[i]);
    free(g_view.scripts);
    free(g_view.cutscenes);
    SD_FreeData(&g_view.program);
    free(g_view.sequences);
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
    MT5Render_Init(&g_view.render);
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    LoadModels(folder);
    g_view.originalDoors = malloc((file.doorCount + 1) * sizeof(MAPINFO_Door));
    g_view.originalLights = malloc((file.lightCount + 1) * sizeof(MAPINFO_Light));
    if (g_view.originalDoors != NULL && g_view.originalLights != NULL) {
        memcpy(g_view.originalDoors, file.doors, file.doorCount * sizeof(MAPINFO_Door));
        memcpy(g_view.originalLights, file.lights, file.lightCount * sizeof(MAPINFO_Light));
    }
    // What the program does with the map, if it has one
    if (!MapProgram_Read(filepath, &g_view.file, &g_view.program))
        SD_FreeData(&g_view.program);
    // After the models, for the doors whose model couldn't be loaded
    if (!FindSequences() || g_view.originalDoors == NULL || g_view.originalLights == NULL || !ReadCutscenes() || !CreateItems()) {
        fputs("[MAPINFOView_Open]: Memory error\n", stderr);
        MAPINFOView_Close();
        return false;
    }
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.selected = -1;
    g_view.mapExpanded = true;

    printf("[MAPINFOView_Open]: %u chunks, %u cameras, %u lights, %u shapes, %u characters (%u shown with their model), %u map models, %u of %u doors\n",
        file.chunkCount, file.cameraCount, file.lightCount, file.shapeCount, g_view.characterCount, g_view.modelledCharacters,
        g_view.mapModelCount, g_view.loadedDoors, file.doorCount);
    g_view.open = true;
    return true;
}

bool MAPINFOView_IsOpen(void) {
    return g_view.open;
}

// The edits of the properties, in memory: the doors, the lights and the characters differ from the file
static bool IsItemEdited(const Item* item) {
    switch (item->kind) {
    case ITEM_DOOR:
        return memcmp(&g_view.file.doors[item->index], &g_view.originalDoors[item->index], sizeof(MAPINFO_Door)) != 0;
    case ITEM_LIGHT:
        return memcmp(&g_view.file.lights[item->index], &g_view.originalLights[item->index], sizeof(MAPINFO_Light)) != 0;
    case ITEM_CHARACTER: {
        const MapCharacter* character = &g_view.characters[item->index];
        return memcmp(character->info.position, character->original.position, sizeof(character->info.position)) != 0 ||
            memcmp(character->info.angle, character->original.angle, sizeof(character->info.angle)) != 0;
    }
    default:
        return false;
    }
}

static bool IsAnyEdited(void) {
    for (uint32_t i = 0; i < g_view.itemCount; i++) {
        if (IsItemEdited(&g_view.items[i]))
            return true;
    }
    return false;
}

// The instance of a door, after its placement is edited
static void UpdateDoor(uint32_t door) {
    if (g_view.doorInstances[door] < 0)
        return;
    MT5Render_Instance* instance = &g_view.instances[g_view.doorInstances[door]];
    instance->transform = DoorTransform(&g_view.file.doors[door]);
    instance->center = Vector3Transform(instance->model->mesh.center, instance->transform);
}

// The values of the file
static void RestoreAll(void) {
    memcpy(g_view.file.doors, g_view.originalDoors, g_view.file.doorCount * sizeof(MAPINFO_Door));
    memcpy(g_view.file.lights, g_view.originalLights, g_view.file.lightCount * sizeof(MAPINFO_Light));
    for (uint32_t i = 0; i < g_view.file.doorCount; i++)
        UpdateDoor(i);
    for (uint32_t i = 0; i < g_view.characterCount; i++)
        g_view.characters[i].info = g_view.characters[i].original;
    ShowCharacters();
}

// A camera of the tree shows the map from it; the other items are looked at with the free camera
static void Select(int item) {
    SetStart();
    g_view.selected = item;
    g_view.mapCamera = false;
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
    else if (selected->kind == ITEM_CHARACTER && g_view.characters[selected->index].info.positioned)
        target = Vector3Add(CharacterPosition(&g_view.characters[selected->index].info), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.5f, 0.0f });
    else if (selected->kind == ITEM_DOOR) {
        const float* position = g_view.file.doors[selected->index].position;
        target = (Vector3) { position[0], position[1] + 1.0f, position[2] };
    }
    else if (selected->kind == ITEM_SHAPE)
        target = ShapeCenter(&g_view.file.shapes[selected->index]);
    else if (selected->kind == ITEM_MAP_MODEL) {
        // The whole model
        const MT5Mesh* mesh = &g_view.models[selected->index].mesh;
        g_view.camera.target = mesh->center;
        g_view.camera.distance = mesh->radius / sinf(MAPINFOVIEW_FOVY * 0.5f * DEG2RAD);
        return;
    }
    else
        return;
    g_view.camera.target = target;
    g_view.camera.distance = 6.0f;
}

// The selected cutscene, if it isn't an empty chunk. -1 if none
static int SelectedCutscene(void) {
    if (g_view.selected < 0 || g_view.items[g_view.selected].kind != ITEM_CUTSCENE)
        return -1;
    uint32_t index = g_view.items[g_view.selected].index;
    return g_view.cutscenes[index].valid ? (int)index : -1;
}

// The selected cutscene loaded by the program, NULL if none
static const SD_Reference* SelectedSequence(void) {
    if (g_view.selected < 0 || g_view.items[g_view.selected].kind != ITEM_SEQUENCE)
        return NULL;
    return Sequence(g_view.items[g_view.selected].index);
}

void MAPINFOView_Toolbar(void) {
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the free camera", KEY_R)) {
        ResetCamera();
        g_view.mapCamera = false;
    }
    bool fly = !g_view.mapCamera && g_view.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the free camera, from the camera of the map if it is shown", KEY_F, &fly)) {
        // Flies from the position of the camera of the map, or of the free camera, looking around it
        if (fly && g_view.mapCamera && SelectedCamera() != NULL)
            FreeCamera_LookFrom(&g_view.camera, MapCamera(SelectedCamera()));
        FreeCamera_SetFly(&g_view.camera, fly);
        g_view.mapCamera = false;
    }
    if (SelectedCamera() == NULL)
        GuiDisable();
    int mode = g_view.mapCamera ? 1 : 0;
    if (Toolbar_Choice("Free camera;Map camera", "Show the map from the free camera or from the selected camera", KEY_C, &mode)) {
        // A transition from the current camera starts again from the free camera
        if (mode == 1 && (SelectedCamera()->flags & MAPINFO_CAMERA_FROM_CURRENT)) {
            SetStart();
            g_view.time = 0.0f;
        }
        g_view.mapCamera = mode == 1;
    }
    GuiEnable();
    Toolbar_Separator();
    Toolbar_Toggle(ICON_TEXT_A, "Labels", "Show the names of the characters and the IDs of the cameras", KEY_L, &g_options.labels);
    Toolbar_Separator();
    if (!IsAnyEdited())
        GuiDisable();
    if (Toolbar_Button(ICON_UNDO, "Restore", "Restore the values of the file of the edited items", 0))
        RestoreAll();
    GuiEnable();
    // The view of the cutscenes is shown over this one, which stays open to go back to it
    Toolbar_Separator();
    int cutscene = SelectedCutscene();
    const SD_Reference* sequence = SelectedSequence();
    if (cutscene < 0 && sequence == NULL)
        GuiDisable();
    if (Toolbar_Button(ICON_PLAYER_PLAY, "Play", "Play the selected cutscene", KEY_ENTER)) {
        if (sequence != NULL && !AUTHView_OpenMapSequence(g_view.filename, sequence))
            printf("[MAPINFOView]: Failed to load the cutscene %s of %s\n", sequence->name, sequence->folder);
        else if (sequence == NULL && !AUTHView_OpenMapCutscene(g_view.filename, (uint32_t)cutscene))
            printf("[MAPINFOView]: Failed to load the cutscene %d\n", cutscene);
    }
    GuiEnable();
    // The view of the program of the map too
    if (Toolbar_Button(ICON_CPU, "Program", "Show the program of the map (SCN3 chunk)", KEY_P) &&
        !ProgramView_OpenMap(g_view.filename))
        printf("[MAPINFOView]: Failed to load the program of the map\n");
}

static void UpdateInput(Rectangle scene) {
    if (g_view.fit)
        FitCamera(scene);
    if (!g_view.mapCamera)
        FreeCamera_Update(&g_view.camera, scene, 0.1f);
    g_view.time += Scene3D_FrameTime();
}

// The visible instances: the models of the map, the doors and the characters (only the visible ones have an instance),
// without the characters for which hidden returns true if it isn't NULL
static void DrawModels(Camera3D camera, bool (*hidden)(const char* id)) {
    if (g_view.visibleInstances == NULL)
        return;
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.mapModelCount && g_options.layers[LAYER_MAP]; i++) {
        if (IsItemVisible(ITEM_MAP_MODEL, i))
            g_view.visibleInstances[count++] = g_view.instances[i];
    }
    for (uint32_t i = 0; i < g_view.file.doorCount && g_options.layers[LAYER_DOORS]; i++) {
        if (g_view.doorInstances[i] >= 0 && IsItemVisible(ITEM_DOOR, i))
            g_view.visibleInstances[count++] = g_view.instances[g_view.doorInstances[i]];
    }
    // In the order of ShowCharacters, which gives an instance to the shown characters with a model
    uint32_t instance = g_view.characterInstances;
    for (uint32_t i = 0; i < g_view.characterCount && instance < g_view.instanceCount && g_options.layers[LAYER_CHARACTERS]; i++) {
        const MapCharacter* character = &g_view.characters[i];
        if (!character->shown || character->model < 0 || !IsItemVisible(ITEM_CHARACTER, i))
            continue;
        if (hidden == NULL || !hidden(character->info.id))
            g_view.visibleInstances[count++] = g_view.instances[instance];
        instance++;
    }
    if (count > 0)
        MT5Render_DrawInstances(&g_view.render, g_view.visibleInstances, count, camera);
}

const char* MAPINFOView_Path(void) {
    return g_view.open ? g_view.filename : NULL;
}

void MAPINFOView_DrawMap(Camera3D camera, bool (*hidden)(const char* id)) {
    if (g_view.open)
        DrawModels(camera, hidden);
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
    // The selected one too, over the others
    for (uint32_t i = 0; i < file->shapeCount; i++) {
        const MAPINFO_Shape* shape = &file->shapes[i];
        if (g_options.layers[LAYER_SHAPES] && g_options.layers[LAYER_GROUPS + shape->group] && IsItemVisible(ITEM_SHAPE, i) &&
            !IsSelected(ITEM_SHAPE, i))
            DrawShape(shape, g_groupColors[shape->group]);
    }
    if (g_view.selected >= 0 && g_view.items[g_view.selected].kind == ITEM_SHAPE)
        DrawShape(&file->shapes[g_view.items[g_view.selected].index], g_selectedColor);
    // Not seen from the camera of the map: they would cover the view. The selected one is drawn even if it is hidden
    const MAPINFO_Camera* selectedCamera = SelectedCamera();
    if (!g_view.mapCamera) {
        for (uint32_t i = 0; i < file->cameraCount; i++) {
            const MAPINFO_Camera* camera = &file->cameras[i];
            if (camera == selectedCamera) {
                Camera3D view = MapCamera(camera);
                DrawCameraGizmo(view, g_selectedColor);
                DrawLine3D(view.position, view.target, Fade(g_selectedColor, 0.5f));
            }
            else if (g_options.layers[LAYER_CAMERAS] && IsItemVisible(ITEM_CAMERA, i))
                DrawCameraGizmo(MapCamera(camera), Fade(g_cameraColor, 0.6f));
        }
    }
    for (uint32_t i = 0; i < file->lightCount; i++) {
        bool selected = IsSelected(ITEM_LIGHT, i);
        if (selected || (g_options.layers[LAYER_LIGHTS] && IsItemVisible(ITEM_LIGHT, i))) {
            const MAPINFO_Light* light = &file->lights[i];
            Vector3 position = LightPosition(light);
            Color color = ColorFromNormalized((Vector4) { light->color[0], light->color[1], light->color[2], 1.0f });
            DrawSphere(position, selected ? 0.15f : 0.08f, color);
            Vector3 direction = { light->direction[0], light->direction[1], light->direction[2] };
            DrawLine3D(position, Vector3Add(position, Vector3Scale(direction, 0.5f)), selected ? g_selectedColor : color);
        }
    }
    // The shown characters without their model are drawn as cylinders, and the selected one too
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRT_Character* character = &g_view.characters[i].info;
        bool selected = IsSelected(ITEM_CHARACTER, i);
        bool visible = g_options.layers[LAYER_CHARACTERS] && g_view.characters[i].shown && IsItemVisible(ITEM_CHARACTER, i);
        if (!character->positioned || (!selected && (!visible || g_view.characters[i].model >= 0)))
            continue;
        Color color = selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor;
        DrawCylinderWires(CharacterPosition(character), MAPINFOVIEW_RADIUS, MAPINFOVIEW_RADIUS, MAPINFOVIEW_HEIGHT, 8, color);
        // Direction it faces: angle 0 faces -Z, as the bodies of the models
        float yaw = character->angle[1] * DEG2RAD;
        Vector3 eyes = Vector3Add(CharacterPosition(character), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.9f, 0.0f });
        DrawLine3D(eyes, Vector3Add(eyes, (Vector3) { -sinf(yaw) * 0.5f, 0.0f, -cosf(yaw) * 0.5f }), color);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

static void DrawLabel(Camera3D camera, Rectangle area, Font font, float fontSize, Vector3 position, const char* text, Color color) {
    Vector2 point;
    if (Scene3D_ScreenPoint(camera, area, position, &point))
        Scene3D_DrawLabel(point, text, font, fontSize, color);
}

// Names of the visible characters (their ID if they have none) and IDs of the cameras, and of the selected ones
static void DrawLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    const MAPINFO_File* file = &g_view.file;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRT_Character* character = &g_view.characters[i].info;
        bool selected = IsSelected(ITEM_CHARACTER, i);
        bool visible = g_options.layers[LAYER_CHARACTERS] && g_view.characters[i].shown && IsItemVisible(ITEM_CHARACTER, i);
        if (character->positioned && (visible || selected))
            DrawLabel(camera, area, font, fontSize, Vector3Add(CharacterPosition(character), (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT + 0.1f, 0.0f }),
                GameDB_CharacterDisplay(character->id), selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor);
    }
    for (uint32_t i = 0; i < file->cameraCount && !g_view.mapCamera; i++) {
        bool selected = &file->cameras[i] == SelectedCamera();
        if (selected || (g_options.layers[LAYER_CAMERAS] && IsItemVisible(ITEM_CAMERA, i)))
            DrawLabel(camera, area, font, fontSize, MapCamera(&file->cameras[i]).position, file->cameras[i].id,
                selected ? g_selectedColor : g_cameraColor);
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

    DrawModels(camera, NULL);
    DrawOverlays();
    FreeCamera_End3D();
    if (g_options.labels)
        DrawLabels(camera, view, font, fontSize);
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// An item of the tree, with its eye or not. A click sets *clicked. Returns true when its eye is clicked
static bool TreeItem(uint32_t item, bool eye, bool dimmed, int* clicked) {
    bool* visible = eye ? &g_view.visibleItems[item] : NULL;
    bool previous = visible != NULL && *visible;
    int flags = ((int)item == g_view.selected ? TREE_SELECTED : 0) | (g_view.failedItems[item] ? TREE_FAILED : 0) | (dimmed ? TREE_DIMMED : 0) |
        (IsItemEdited(&g_view.items[item]) ? TREE_EDITED : 0);
    if (Tree_Item(&g_view.tree, g_view.labels[item], flags, visible))
        *clicked = (int)item;
    return visible != NULL && *visible != previous;
}

// The items of a kind in a group, whose eye is the layer if they have one
static void TreeLayer(Layer layer, ItemKind kind, uint32_t count, bool eyes, int* clicked) {
    Tree_BeginGroup(&g_view.tree, g_layerNames[layer], &g_options.expanded[layer], eyes ? &g_options.layers[layer] : NULL);
    for (uint32_t i = 0; i < count; i++)
        TreeItem(g_view.firstItems[kind] + i, eyes, false, clicked);
    Tree_EndGroup(&g_view.tree);
}

// A group per kind of item. The shapes are in a subgroup per COLS group, and the characters in a subgroup per package,
// whose eye shows or hides the package. A click on an item selects it, or unselects it if it is selected
static void DrawTree(Rectangle bounds) {
    const MAPINFO_File* file = &g_view.file;
    Tree* tree = &g_view.tree;
    int clicked = -1;
    Tree_Begin(tree, bounds);
    TreeLayer(LAYER_INFO, ITEM_INFO, g_view.firstItems[ITEM_MAP_MODEL] - g_view.firstItems[ITEM_INFO], false, &clicked);
    TreeLayer(LAYER_MAP, ITEM_MAP_MODEL, g_view.mapModelCount, true, &clicked);
    TreeLayer(LAYER_DOORS, ITEM_DOOR, file->doorCount, true, &clicked);
    TreeLayer(LAYER_CAMERAS, ITEM_CAMERA, file->cameraCount, true, &clicked);
    TreeLayer(LAYER_LIGHTS, ITEM_LIGHT, file->lightCount, true, &clicked);

    Tree_BeginGroup(tree, g_layerNames[LAYER_SHAPES], &g_options.expanded[LAYER_SHAPES], &g_options.layers[LAYER_SHAPES]);
    for (int g = 0; g < MAPINFO_GROUP_COUNT; g++) {
        Tree_BeginGroup(tree, g_layerNames[LAYER_GROUPS + g], &g_options.expanded[LAYER_GROUPS + g], &g_options.layers[LAYER_GROUPS + g]);
        for (uint32_t i = 0; i < file->shapeCount; i++) {
            if ((int)file->shapes[i].group == g)
                TreeItem(g_view.firstItems[ITEM_SHAPE] + i, true, false, &clicked);
        }
        Tree_EndGroup(tree);
    }
    Tree_EndGroup(tree);

    // The characters hidden by a character with their ID in a previous package are dimmed
    bool changed = false;
    Tree_BeginGroup(tree, g_layerNames[LAYER_CHARACTERS], &g_options.expanded[LAYER_CHARACTERS], &g_options.layers[LAYER_CHARACTERS]);
    for (int p = -1; p < (int)g_view.packageCount; p++) {
        MapPackage* package = p >= 0 ? &g_view.packages[p] : NULL;
        if (package == NULL && (g_view.characterCount == 0 || g_view.characters[0].package >= 0))
            continue;
        Tree_BeginGroup(tree, package != NULL ? PackageText(package) : "CHRD chunk", package != NULL ? &package->expanded : &g_view.mapExpanded,
            package != NULL ? &package->shown : NULL);
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            if (g_view.characters[i].package == p)
                changed |= TreeItem(g_view.firstItems[ITEM_CHARACTER] + i, true, !g_view.characters[i].shown, &clicked);
        }
        changed |= Tree_EndGroup(tree);
    }
    Tree_EndGroup(tree);
    // The empty chunks are dimmed
    Tree_BeginGroup(tree, g_layerNames[LAYER_CUTSCENES], &g_options.expanded[LAYER_CUTSCENES], NULL);
    for (uint32_t i = 0; i < file->cutsceneCount; i++)
        TreeItem(g_view.firstItems[ITEM_CUTSCENE] + i, false, !g_view.cutscenes[i].valid, &clicked);
    for (uint32_t i = 0; i < g_view.sequenceCount; i++)
        TreeItem(g_view.firstItems[ITEM_SEQUENCE] + i, false, false, &clicked);
    Tree_EndGroup(tree);
    Tree_End(tree);

    if (changed)
        ShowCharacters();
    if (clicked >= 0)
        Select(clicked == g_view.selected ? -1 : clicked);
}

static const char* g_cameraCurves[AUTH_CAMERA_CURVES] = { "X", "Y", "Z", "Target X", "Target Y", "Target Z", "Roll", "Field of view" };

// The chunks of the file, when nothing is selected
static void FileProperties(Properties* properties) {
    const MAPINFO_File* file = &g_view.file;
    const GameDB_Map* map = GameDB_MapOfFile(g_view.filename);
    Properties_Note(properties, "Select an item of the tree");
    if (map != NULL) {
        Properties_Text(properties, "Map", map->name);
        if (map->area[0] != '\0')
            Properties_Text(properties, "Area", map->area);
    }
    Properties_Section(properties, TextFormat("%u chunks", file->chunkCount));
    for (uint32_t i = 0; i < file->chunkCount; i++)
        Properties_Text(properties, file->chunks[i].id, TextFormat("0x%X bytes at 0x%X", file->chunks[i].size, file->chunks[i].offset));
}

static void CameraProperties(Properties* properties, uint32_t index) {
    const MAPINFO_Camera* camera = &g_view.file.cameras[index];
    float values[AUTH_CAMERA_CURVES];
    MapCameraValues(camera, values);
    Properties_Text(properties, "ID", camera->id);
    Properties_Hex(properties, "Flags", camera->flags);
    Properties_Bool(properties, "From the current camera", (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) != 0);
    Properties_Bool(properties, "Relative", (camera->flags & MAPINFO_CAMERA_RELATIVE) != 0);
    Properties_Int(properties, "Keys", CameraKeys(camera));
    Properties_Text(properties, "Duration", TextFormat("%.2f s", CameraDuration(camera)));
    Properties_Section(properties, "Now");
    Properties_Vector(properties, "Position", &values[AUTH_CAMERA_X], 3);
    Properties_Vector(properties, "Target", &values[AUTH_CAMERA_TARGET_X], 3);
    Properties_Float(properties, "Roll", values[AUTH_CAMERA_ROLL]);
    Properties_Float(properties, "Field of view", values[AUTH_CAMERA_FOV]);
    Properties_Section(properties, "Keys of the curves");
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        Properties_Int(properties, g_cameraCurves[c], camera->curves[c].count);
}

// Its color, intensity, position, direction and angle can be edited
static void LightProperties(Properties* properties, uint32_t index) {
    MAPINFO_Light* light = &g_view.file.lights[index];
    const MAPINFO_Light* original = &g_view.originalLights[index];
    Properties_Int(properties, "Group", light->group);
    Properties_Int(properties, "ID", light->id);
    Properties_Hex(properties, "Flags", light->flags);
    Properties_Int(properties, "Unknown", light->unknown);
    Properties_EditVector(properties, "Color", light->color, original->color, 3, 0.005f);
    Properties_EditFloat(properties, "Intensity", &light->intensity, &original->intensity, 0.01f);
    Properties_EditVector(properties, "Position", light->position, original->position, 3, 0.01f);
    Properties_EditVector(properties, "Direction", light->direction, original->direction, 3, 0.01f);
    Properties_EditFloat(properties, "Angle", &light->angle, &original->angle, 0.5f);
}

// The fields of the prefix that the group of the shape has, then its points
static void ShapeProperties(Properties* properties, uint32_t index) {
    const MAPINFO_Shape* shape = &g_view.file.shapes[index];
    Properties_Text(properties, "Group", TextFormat("%s (%s)", g_layerNames[LAYER_GROUPS + shape->group],
        MAPINFO_GroupName(shape->group)));
    Properties_Text(properties, "Type", ShapeType(shape));
    Properties_Int(properties, "Block", shape->block);
    if (shape->group == MAPINFO_GROUP_COLI)
        Properties_Int(properties, "Collision type", shape->value);
    else if (shape->group == MAPINFO_GROUP_EVNT || shape->group == MAPINFO_GROUP_SOND)
        Properties_Int(properties, "Value", shape->value);
    else if (shape->group == MAPINFO_GROUP_PROP) {
        Properties_Text(properties, "Property", shape->property);
        Properties_Hex(properties, "First word", shape->value);
    }
    if (shape->group == MAPINFO_GROUP_HGHT)
        Properties_Float(properties, "Height", shape->height);
    if (shape->group == MAPINFO_GROUP_UNDU)
        Properties_Vector(properties, "Heights", shape->heights, 4);
    Vector3 center = ShapeCenter(shape);
    Properties_Vector(properties, "Center", (const float[]) { center.x, center.y, center.z }, 3);
    Properties_Section(properties, TextFormat("%u values", shape->valueCount));
    // Circles: radius, x, z; rectangles: x, z of the center and the half axes; the others: x, z of each point
    for (uint32_t v = 0; v < shape->valueCount; v++)
        Properties_Float(properties, TextFormat("%u", v), shape->values[v]);
}

// Its placement can be edited
static void DoorProperties(Properties* properties, uint32_t index) {
    MAPINFO_Door* door = &g_view.file.doors[index];
    const MAPINFO_Door* original = &g_view.originalDoors[index];
    const char* error = g_view.doorErrors != NULL ? g_view.doorErrors[index] : NULL;
    Properties_Text(properties, "Model", door->model);
    Properties_Text(properties, "Type", door->type == MAPINFO_DOOR_DOOR ? "door" : door->type == MAPINFO_DOOR_MODEL ? "model" :
        TextFormat("%u", door->type));
    bool changed = Properties_EditVector(properties, "Position", door->position, original->position, 3, 0.01f);
    changed |= Properties_EditFloat(properties, "Angle", &door->angle, &original->angle, 0.5f);
    changed |= Properties_EditVector(properties, "Scale", door->scale, original->scale, 3, 0.01f);
    Properties_Text(properties, "Model file", error != NULL ? error : "loaded");
    if (changed)
        UpdateDoor(index);
}

static void ModelProperties(Properties* properties, const MT5Render_Model* model) {
    Properties_Int(properties, "Nodes", model->nodeCount);
    Properties_Int(properties, "Triangles", model->mesh.triangleCount);
    if (model->mesh.skippedTriangles > 0)
        Properties_Int(properties, "Skipped triangles", model->mesh.skippedTriangles);
    Properties_Text(properties, "Textures", TextFormat("%u/%u", model->loadedTextures, model->textureCount));
    Properties_Int(properties, "Animation frames", model->mesh.frameCount);
    Properties_Vector(properties, "Center", (const float[]) { model->mesh.center.x, model->mesh.center.y, model->mesh.center.z }, 3);
    Properties_Float(properties, "Radius", model->mesh.radius);
}

// Its position, if it is in the scene, and its angle can be edited
static void CharacterProperties(Properties* properties, uint32_t index) {
    MapCharacter* character = &g_view.characters[index];
    CHRT_Character* info = &character->info;
    const char* package = PackageName(character->package);
    Properties_Text(properties, "ID", info->id);
    GameDB_CharacterProperties(properties, info->id);
    Properties_Text(properties, "Package", package != NULL ? package : "CHRD chunk");
    Properties_Text(properties, "Image", info->image != NULL ? info->image : "none");
    bool changed = false;
    if (info->positioned)
        changed = Properties_EditVector(properties, "Position", info->position, character->original.position, 3, 0.01f);
    else
        Properties_Text(properties, "Position", info->hasPosition ? "off the scene" : "none");
    changed |= Properties_EditVector(properties, "Angle", info->angle, character->original.angle, 3, 0.5f);
    if (changed)
        ShowCharacters();
    Properties_Bool(properties, "Sleep", info->sleep);
    Properties_Bool(properties, "Human", info->human);
    Properties_Bool(properties, "Arm of Ryo", info->arm);
    if (g_view.program.count > 0) {
        Properties_Text(properties, "Woken by the program", ProgramCalls(SD_WAKE, -1, info->id));
        Properties_Text(properties, "Put to sleep by it", ProgramCalls(SD_SLEEP, -1, info->id));
        Properties_Text(properties, "Used by it", ProgramCalls(SD_CHARACTER, -1, info->id));
    }
    // A shown package can have it hidden by a character with its ID of a previous package
    Properties_Text(properties, "Shown", character->shown ? "yes" : character->package >= 0 && !g_view.packages[character->package].shown ?
        "no, its package is hidden" : "no, another package has its ID");
    if (character->model >= 0) {
        Properties_Section(properties, "Model");
        ModelProperties(properties, &g_view.models[character->model]);
    }
    else
        Properties_Text(properties, "Model", character->error != NULL ? character->error : "none");
}

static void CutsceneProperties(Properties* properties, uint32_t index) {
    const MapCutscene* cutscene = &g_view.cutscenes[index];
    const MAPINFO_Cutscene* chunk = &g_view.file.cutscenes[index];
    Properties_Text(properties, "Chunk", TextFormat("0x%X bytes at 0x%X", chunk->size, chunk->offset));
    Properties_Text(properties, "Played by the program", ProgramCalls(SD_CUTSCENE, (int32_t)index, NULL));
    if (!cutscene->valid) {
        Properties_Note(properties, chunk->size <= 8 ? "Empty TRCK chunk" : "Invalid TRCK chunk");
        return;
    }
    Properties_Text(properties, "Duration", TextFormat("%s (%u frames)", FormatTime(cutscene->duration / AUTH_FRAME_RATE), cutscene->duration));
    Properties_Int(properties, "Characters", cutscene->characterCount);
    Properties_Text(properties, "IDs", cutscene->ids);
    Properties_Text(properties, "Names", CutsceneNames(cutscene, INT32_MAX));
    Properties_Int(properties, "Commands", cutscene->commandCount);
    Properties_Int(properties, "Motions", cutscene->motions);
    Properties_Int(properties, "Voices", cutscene->voices);
    Properties_Int(properties, "Sounds", cutscene->sounds);
    Properties_Note(properties, "Play it with the Play button of the toolbar (Enter)");
}

// A cutscene loaded by the program in a slot of the table of cutscenes: its AUTH file is loaded when it is played
static void SequenceProperties(Properties* properties, uint32_t index) {
    const SD_Reference* sequence = Sequence(index);
    Properties_Int(properties, "Slot", sequence->number);
    Properties_Text(properties, "File", sequence->name);
    Properties_Text(properties, strchr(sequence->folder, '/') != NULL ? "Folder" : "Package", sequence->folder);
    uint32_t loads = 0;
    for (uint32_t i = 0; i < g_view.program.count; i++)
        loads += SameReference(&g_view.program.references[i], sequence);
    Properties_Int(properties, "Loaded by calls", loads);
    Properties_Text(properties, "Played by the program", ProgramCalls(SD_CUTSCENE, sequence->number, NULL));
    if ((uint32_t)sequence->number < g_view.file.cutsceneCount)
        Properties_Note(properties, "The slot of a TRCK chunk of the map: the program replaces its cutscene");
    Properties_Note(properties, "Play it with the Play button of the toolbar (Enter)");
}

// Of the selected item, or the chunks of the file
static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_view.properties;
    if (g_view.selected < 0) {
        Properties_Begin(properties, bounds, GetFileName(g_view.filename));
        FileProperties(properties);
        Properties_End(properties);
        return;
    }
    const Item* item = &g_view.items[g_view.selected];
    const MAPINFO_File* file = &g_view.file;
    switch (item->kind) {
    case ITEM_CAMERA:
        Properties_Begin(properties, bounds, TextFormat("Camera %s", file->cameras[item->index].id));
        CameraProperties(properties, item->index);
        break;
    case ITEM_LIGHT:
        Properties_Begin(properties, bounds, TextFormat("Light %u.%u", file->lights[item->index].group, file->lights[item->index].id));
        LightProperties(properties, item->index);
        break;
    case ITEM_SHAPE:
        Properties_Begin(properties, bounds, TextFormat("Shape %u", item->index));
        ShapeProperties(properties, item->index);
        break;
    case ITEM_DOOR:
        Properties_Begin(properties, bounds, TextFormat("Door %u", item->index));
        DoorProperties(properties, item->index);
        break;
    case ITEM_MAP_MODEL:
        Properties_Begin(properties, bounds, TextFormat("Model %s", g_view.mapModelNames[item->index]));
        ModelProperties(properties, &g_view.models[item->index]);
        break;
    case ITEM_CHARACTER:
        Properties_Begin(properties, bounds, TextFormat("Character %s", GameDB_CharacterDisplay(g_view.characters[item->index].info.id)));
        CharacterProperties(properties, item->index);
        break;
    case ITEM_CUTSCENE:
        Properties_Begin(properties, bounds, TextFormat("Cutscene %u", item->index));
        CutsceneProperties(properties, item->index);
        break;
    case ITEM_SEQUENCE:
        Properties_Begin(properties, bounds, TextFormat("Cutscene %d, loaded by the program", Sequence(item->index)->number));
        SequenceProperties(properties, item->index);
        break;
    default:
        Properties_Begin(properties, bounds, "Information");
        Properties_Note(properties, g_view.labels[g_view.selected]);
        break;
    }
    Properties_End(properties);
}

void MAPINFOView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float margin = 10.0f;
    float top = area.y + margin;
    Rectangle column = { area.x + margin, top, MAPINFOVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle scene = { column.x + column.width + margin, top, area.width - column.width - margin * 3.0f, column.height };
    Rectangle tree, properties;
    Properties_Split(column, &tree, &properties);
    if (tree.height <= 0.0f || scene.width <= 0.0f)
        return;
    UpdateInput(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    DrawTree(tree);
    DrawProperties(properties);
    DrawScene(scene, font, fontSize);
}

void MAPINFOView_Status(ViewStatus* status) {
    const MAPINFO_File* file = &g_view.file;
    const MAPINFO_Camera* camera = SelectedCamera();
    snprintf(status->file, sizeof(status->file), "%s", g_view.filename);
    int length = snprintf(status->info, sizeof(status->info),
        "%u cameras, %u lights, %u shapes, %u characters (%u with a model), %u models, %u doors", file->cameraCount,
        file->lightCount, file->shapeCount, g_view.characterCount, g_view.modelledCharacters, g_view.mapModelCount, file->doorCount);
    if (g_view.loadedDoors < file->doorCount)
        snprintf(status->info + length, sizeof(status->info) - length, " (%u without a model)", file->doorCount - g_view.loadedDoors);
    if (g_view.mapCamera && camera != NULL)
        snprintf(status->hint, sizeof(status->hint), "Seen from the camera %s", camera->id);
    else
        snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_view.camera));
}

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
#include "formats/map/mapinfo.h"
#include "formats/text/chrt.h"
#include "views/mt5Render.h"
#include "views/orbitCamera.h"
#include "views/mapinfoView.h"

#define MAPINFOVIEW_LABEL_SIZE   160
#define MAPINFOVIEW_LIST_WIDTH   420.0f
#define MAPINFOVIEW_FOVY         45.0f
#define MAPINFOVIEW_CAMERA_FOVY  60.0f   // Of the cameras without a field of view curve
#define MAPINFOVIEW_ASPECT       (4.0f / 3.0f) // Of the screen of the game, for the cameras of the map
#define MAPINFOVIEW_OFF_SCENE    999.0f  // Coordinate of the positions of the characters off the scene
#define MAPINFOVIEW_HEIGHT       1.7f    // Of the characters, drawn as cylinders
#define MAPINFOVIEW_RADIUS       0.25f
#define MAPINFOVIEW_CIRCLE_SIDES 24

typedef enum {
    ITEM_INFO,
    ITEM_CAMERA,
    ITEM_LIGHT,
    ITEM_CHARACTER,
} ItemKind;

typedef struct {
    ItemKind    kind;
    uint32_t    index;  // In the cameras, lights or characters
} Item;

typedef struct {
    char        id[5];
    bool        positioned; // Has a Position in the scene
    bool        sleep;      // SLEEP flag
    Vector3     position;
    float       angle;      // Around the vertical axis, degrees
} Character;

typedef enum {
    LAYER_MAP,
    LAYER_GROUPS,           // One per COLS group, MAPINFO_GROUP_COUNT
    LAYER_LIGHTS = LAYER_GROUPS + MAPINFO_GROUP_COUNT,
    LAYER_CAMERAS,
    LAYER_CHARACTERS,
    LAYER_LABELS,
    LAYER_COUNT,
} Layer;

static const char* g_layerNames[LAYER_COUNT] = {
    "Map", "COLI", "HGHT", "EVNT", "UNDU", "SOND", "PROP", "Lights", "Cameras", "Characters", "Labels",
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
    Character*          characters;
    MT5Render           render;
    uint32_t            modelCount;
    MT5Render_Model*    models;
    uint32_t            itemCount;
    Item*               items;
    char                (*labels)[MAPINFOVIEW_LABEL_SIZE];
    const char**        labelPointers;
    int                 listScroll;
    int                 listFocus;
    int                 selected;       // Item, -1 if none
    bool                mapCamera;      // Show the map from the selected camera
    float               time;           // Seconds, for the animated cameras
    OrbitCamera         camera;
    Vector3             center;         // Of the map
    float               radius;
    bool                layers[LAYER_COUNT];
} MAPINFOView_State;

static MAPINFOView_State g_view;

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
    const AUTH_Curve* curves = source->curves;
    Vector3 position = { AUTH_CurveValue(&curves[AUTH_CAMERA_X], time, 0.0f), AUTH_CurveValue(&curves[AUTH_CAMERA_Y], time, 0.0f),
        AUTH_CurveValue(&curves[AUTH_CAMERA_Z], time, 0.0f) };
    Vector3 target = { AUTH_CurveValue(&curves[AUTH_CAMERA_TARGET_X], time, 0.0f),
        AUTH_CurveValue(&curves[AUTH_CAMERA_TARGET_Y], time, 0.0f), AUTH_CurveValue(&curves[AUTH_CAMERA_TARGET_Z], time, 0.0f) };
    if (Vector3Equals(position, target))
        target.z -= 1.0f;
    Vector3 forward = Vector3Normalize(Vector3Subtract(target, position));
    float roll = AUTH_CurveValue(&curves[AUTH_CAMERA_ROLL], time, 0.0f);
    Vector3 up = Vector3RotateByAxisAngle((Vector3) { 0.0f, 1.0f, 0.0f }, forward, roll * DEG2RAD);
    float fovy = AUTH_CurveValue(&curves[AUTH_CAMERA_FOV], time, MAPINFOVIEW_CAMERA_FOVY);
    return (Camera3D) { position, target, up, fovy > 1.0f && fovy < 179.0f ? fovy : MAPINFOVIEW_CAMERA_FOVY, CAMERA_PERSPECTIVE };
}

static const MAPINFO_Camera* SelectedCamera(void) {
    if (g_view.selected < 0 || g_view.items[g_view.selected].kind != ITEM_CAMERA)
        return NULL;
    return &g_view.file.cameras[g_view.items[g_view.selected].index];
}

// Reads the Position and Angle commands of a block. Returns true if it has a Position in the scene (see CHRTView)
static bool ReadPlacement(uint32_t block, Character* character) {
    const CHRT_Script* script = &g_view.script;
    bool hasPosition = false;
    for (uint32_t c = block + 1; c < script->commandCount && script->commands[c].depth > script->commands[block].depth; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != (int32_t)block || command->argCount == 0 || command->args[0].type != CHRT_ARG_FLOAT)
            continue;
        if (CHRT_IsCommand(command, "Position") && command->argCount == 3) {
            Vector3 position = { command->args[0].f, command->args[1].f, command->args[2].f };
            if (fabsf(position.x) < MAPINFOVIEW_OFF_SCENE && fabsf(position.y) < MAPINFOVIEW_OFF_SCENE &&
                fabsf(position.z) < MAPINFOVIEW_OFF_SCENE) {
                character->position = position;
                hasPosition = true;
            }
        }
        else if (CHRT_IsCommand(command, "Angle"))
            character->angle = command->args[command->argCount == 3 ? 1 : 0].f;
    }
    return hasPosition;
}

// The characters of the CHRD chunk, at their last Position in the scene, or that of their first Entry
static void LoadCharacters(void) {
    const MAPINFO_File* file = &g_view.file;
    if (file->characters == NULL || !CHRT_LoadFromMemory(file->characters, file->charactersSize, g_view.filename, &g_view.script))
        return;
    const CHRT_Script* script = &g_view.script;
    g_view.characters = calloc(script->commandCount + 1, sizeof(Character));
    if (g_view.characters == NULL)
        return;
    for (uint32_t c = 0; c < script->commandCount; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != -1 || !CHRT_IsCommand(command, "Character"))
            continue;
        Character* character = &g_view.characters[g_view.characterCount++];
        snprintf(character->id, sizeof(character->id), "%s", command->argCount > 0 && command->args[0].type == CHRT_ARG_LITERAL ? command->args[0].literal : "?");
        character->positioned = ReadPlacement(c, character);
        for (uint32_t i = c + 1; i < script->commandCount && script->commands[i].depth > command->depth; i++) {
            const CHRT_Command* child = &script->commands[i];
            if (child->parent != (int32_t)c)
                continue;
            if (!character->positioned && CHRT_IsCommand(child, "Entry"))
                character->positioned = ReadPlacement(i, character);
            if (CHRT_IsCommand(child, "Flags")) {
                for (uint32_t a = 0; a < child->argCount; a++)
                    character->sleep |= child->args[a].type == CHRT_ARG_STRING && CHRT_EqualsIgnoreCase(child->args[a].string, "SLEEP");
            }
        }
    }
}

// Models of the map: MAP.mt5, MAP01.mt5... in the folder and in its subfolders (mpk00, map0...)
static bool IsMapModel(const char* path) {
    const char* name = GetFileName(path);
    if (toupper((unsigned char)name[0]) != 'M' || toupper((unsigned char)name[1]) != 'A' || toupper((unsigned char)name[2]) != 'P')
        return false;
    for (name += 3; isdigit((unsigned char)*name); name++)
        ;
    return *name == '.';
}

static void LoadMapModels(const char* folder) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".mt5", true);
    g_view.models = calloc(files.count + 1, sizeof(MT5Render_Model));
    if (g_view.models != NULL) {
        for (unsigned int i = 0; i < files.count; i++) {
            // Only one level of subfolders: the extracted PKF files of the map
            const char* relative = files.paths[i] + strlen(folder);
            int separators = 0;
            for (const char* p = relative + 1; *p != '\0'; p++)
                separators += *p == '/' || *p == '\\';
            if (separators <= 1 && IsMapModel(files.paths[i]) && MT5Render_LoadModel(files.paths[i], &g_view.models[g_view.modelCount]))
                g_view.modelCount++;
        }
    }
    UnloadDirectoryFiles(files);
}

// Point of a shape in the scene: the shapes have x and z coordinates
static Vector3 ShapePoint(float x, float y, float z) {
    return (Vector3) { x, y, -z };
}

static void AddPoint(Vector3 point, Vector3* min, Vector3* max) {
    *min = Vector3Min(*min, point);
    *max = Vector3Max(*max, point);
}

// Box around the models of the map, or around the cameras, lights, characters and shapes if there is no model
static void ComputeBounds(void) {
    const MAPINFO_File* file = &g_view.file;
    Vector3 min = { INFINITY, INFINITY, INFINITY }, max = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        const MT5Mesh* mesh = &g_view.models[i].mesh;
        Vector3 extent = { mesh->radius, mesh->radius, mesh->radius };
        AddPoint(Vector3Subtract(mesh->center, extent), &min, &max);
        AddPoint(Vector3Add(mesh->center, extent), &min, &max);
    }
    if (g_view.modelCount == 0) {
        for (uint32_t i = 0; i < file->cameraCount; i++)
            AddPoint(MapCamera(&file->cameras[i]).position, &min, &max);
        for (uint32_t i = 0; i < file->lightCount; i++)
            AddPoint(LightPosition(&file->lights[i]), &min, &max);
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            if (g_view.characters[i].positioned)
                AddPoint(g_view.characters[i].position, &min, &max);
        }
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
    g_view.center = Vector3Scale(Vector3Add(min, max), 0.5f);
    g_view.radius = fmaxf(Vector3Distance(min, max) * 0.5f, 2.0f);
}

static void ResetCamera(void) {
    OrbitCamera_Reset(&g_view.camera, g_view.center, (Vector3) { 0.0f, 0.0f, 1.0f }, g_view.radius, MAPINFOVIEW_FOVY);
    g_view.camera.pitch = 0.8f;
}

static void AddItem(ItemKind kind, uint32_t index, const char* text) {
    g_view.items[g_view.itemCount] = (Item) { kind, index };
    snprintf(g_view.labels[g_view.itemCount], MAPINFOVIEW_LABEL_SIZE, "%s", text);
    g_view.labelPointers[g_view.itemCount] = g_view.labels[g_view.itemCount];
    g_view.itemCount++;
}

static const char* ScnText(uint32_t version) {
    return version == 0x20000 ? "SH-4 code" : version == 0x100 ? "bytecode" : "unknown version";
}

// Information lines, then the cameras, the lights and the characters
static bool CreateItems(void) {
    const MAPINFO_File* file = &g_view.file;
    uint32_t count = 16 + file->bankCount + file->cameraCount + file->lightCount + g_view.characterCount;
    g_view.items = malloc(count * sizeof(Item));
    g_view.labels = malloc(count * sizeof(*g_view.labels));
    g_view.labelPointers = malloc(count * sizeof(*g_view.labelPointers));
    if (g_view.items == NULL || g_view.labels == NULL || g_view.labelPointers == NULL)
        return false;

    const MAPINFO_Chunk* scn = MAPINFO_FindChunk(file, "SCN3");
    if (scn != NULL)
        AddItem(ITEM_INFO, 0, TextFormat("SCN3: %s (0x%X), 0x%X bytes", ScnText(file->scnVersion), file->scnVersion, scn->size));
    uint32_t groupCounts[MAPINFO_GROUP_COUNT] = { 0 };
    for (uint32_t i = 0; i < file->shapeCount; i++)
        groupCounts[file->shapes[i].group]++;
    if (file->shapeCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("COLS: %u COLI, %u HGHT, %u EVNT, %u UNDU, %u SOND, %u PROP", groupCounts[0],
            groupCounts[1], groupCounts[2], groupCounts[3], groupCounts[4], groupCounts[5]));
    if (file->fog.present)
        AddItem(ITEM_INFO, 0, TextFormat("Fog: %g to %g, density %g, color #%06X", file->fog.nearDistance, file->fog.farDistance,
            file->fog.density, file->fog.color & 0xFFFFFF));
    if (file->cutsceneCount > 0)
        AddItem(ITEM_INFO, 0, TextFormat("SCEX: %u cutscenes", file->cutsceneCount));
    for (uint32_t i = 0; i < file->bankCount; i++) {
        if (strcmp(file->banks[i].name, "FREE") != 0)
            AddItem(ITEM_INFO, 0, TextFormat("Sound bank %u: %s", file->banks[i].slot, file->banks[i].name));
    }
    const char* values[] = { "MAPR", "MAPT", "DIRT", "SCRL" };
    for (int i = 0; i < 4; i++) {
        uint32_t value;
        if (MAPINFO_ChunkValue(file, values[i], &value))
            AddItem(ITEM_INFO, 0, TextFormat("%s: %d", values[i], (int32_t)value));
    }
    const char* floats[] = { "SCOF", "BLKS" };
    for (int i = 0; i < 2; i++) {
        uint32_t value;
        float f;
        if (MAPINFO_ChunkValue(file, floats[i], &value)) {
            memcpy(&f, &value, sizeof(f));
            AddItem(ITEM_INFO, 0, TextFormat("%s: %g", floats[i], f));
        }
    }

    for (uint32_t i = 0; i < file->cameraCount; i++) {
        const MAPINFO_Camera* camera = &file->cameras[i];
        uint32_t keys = CameraKeys(camera);
        Camera3D view = MapCamera(camera);
        AddItem(ITEM_CAMERA, i, keys > 1 ?
            TextFormat("Camera %s  %u keys, %.2f s  flags 0x%X", camera->id, keys, CameraDuration(camera), camera->flags) :
            TextFormat("Camera %s  (%.2f, %.2f, %.2f)  flags 0x%X", camera->id, view.position.x, view.position.y, view.position.z, camera->flags));
    }
    for (uint32_t i = 0; i < file->lightCount; i++) {
        const MAPINFO_Light* light = &file->lights[i];
        AddItem(ITEM_LIGHT, i, TextFormat("Light %u.%u  (%.2f, %.2f, %.2f)  color (%.2f, %.2f, %.2f)", light->group, light->id,
            light->position[0], light->position[1], light->position[2], light->color[0], light->color[1], light->color[2]));
    }
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const Character* character = &g_view.characters[i];
        AddItem(ITEM_CHARACTER, i, character->positioned ?
            TextFormat("Character %s  (%.2f, %.2f, %.2f)%s", character->id, character->position.x, character->position.y,
                character->position.z, character->sleep ? "  sleep" : "") :
            TextFormat("Character %s  no position%s", character->id, character->sleep ? "  sleep" : ""));
    }
    return true;
}

void MAPINFOView_Close(void) {
    for (uint32_t i = 0; i < g_view.modelCount; i++)
        MT5Render_FreeModel(&g_view.models[i]);
    free(g_view.models);
    if (g_view.open)
        MT5Render_Free(&g_view.render);
    free(g_view.characters);
    free(g_view.items);
    free(g_view.labels);
    free(g_view.labelPointers);
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
    if (!CreateItems()) {
        fputs("[MAPINFOView_Open]: Memory error\n", stderr);
        MAPINFOView_Close();
        return false;
    }
    MT5Render_Init(&g_view.render);
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    LoadMapModels(folder);
    ComputeBounds();
    ResetCamera();
    g_view.selected = -1;
    g_view.listFocus = -1;
    for (int i = 0; i < LAYER_COUNT; i++)
        g_view.layers[i] = true;
    for (int g = MAPINFO_GROUP_HGHT; g < MAPINFO_GROUP_COUNT; g++)
        g_view.layers[LAYER_GROUPS + g] = false;

    printf("[MAPINFOView_Open]: %u chunks, %u cameras, %u lights, %u shapes, %u characters, %u map models\n", file.chunkCount,
        file.cameraCount, file.lightCount, file.shapeCount, g_view.characterCount, g_view.modelCount);
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
        target = Vector3Add(g_view.characters[selected->index].position, (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.5f, 0.0f });
    else
        return;
    g_view.mapCamera = false;
    g_view.camera.target = target;
    g_view.camera.distance = 6.0f;
}

static void UpdateInput(Rectangle scene) {
    if (IsKeyPressed(KEY_V) && SelectedCamera() != NULL)
        g_view.mapCamera = !g_view.mapCamera;
    if (IsKeyPressed(KEY_R)) {
        ResetCamera();
        g_view.mapCamera = false;
    }
    if (!g_view.mapCamera)
        OrbitCamera_Update(&g_view.camera, scene, 0.1f);
    g_view.time += fminf(GetFrameTime(), 0.25f);
}

static Camera3D g_sortCamera;

static int CompareDistance(const void* a, const void* b) {
    float distanceA = Vector3DistanceSqr(g_view.models[*(const uint32_t*)a].mesh.center, g_sortCamera.position);
    float distanceB = Vector3DistanceSqr(g_view.models[*(const uint32_t*)b].mesh.center, g_sortCamera.position);
    return distanceA < distanceB ? 1 : distanceA > distanceB ? -1 : 0; // Farthest first
}

// The opaque part of all the models, then the translucent part from the farthest model to the nearest one
static void DrawMap(Camera3D camera) {
    uint32_t* order = malloc((g_view.modelCount + 1) * sizeof(uint32_t));
    if (order == NULL)
        return;
    MT5Render_SetCamera(&g_view.render, camera);
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        MT5Render_DrawOpaque(&g_view.render, &g_view.models[i], MatrixIdentity(), 0);
        order[i] = i;
    }
    g_sortCamera = camera;
    qsort(order, g_view.modelCount, sizeof(uint32_t), CompareDistance);
    for (uint32_t i = 0; i < g_view.modelCount; i++)
        MT5Render_DrawTranslucent(&g_view.render, &g_view.models[order[i]], MatrixIdentity(), 0, camera);
    free(order);
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
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float depth = 0.6f;
    float halfHeight = depth * tanf(camera.fovy * 0.5f * DEG2RAD);
    float halfWidth = halfHeight * MAPINFOVIEW_ASPECT;
    Vector3 center = Vector3Add(camera.position, Vector3Scale(forward, depth));
    Vector3 corners[4];
    for (int i = 0; i < 4; i++) {
        float sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
        float sy = i < 2 ? 1.0f : -1.0f;
        corners[i] = Vector3Add(center, Vector3Add(Vector3Scale(right, sx * halfWidth), Vector3Scale(up, sy * halfHeight)));
    }
    for (int i = 0; i < 4; i++) {
        DrawLine3D(camera.position, corners[i], color);
        DrawLine3D(corners[i], corners[(i + 1) % 4], color);
    }
}

// The layers over the map, always visible
static void DrawOverlays(void) {
    const MAPINFO_File* file = &g_view.file;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (uint32_t i = 0; i < file->shapeCount; i++) {
        const MAPINFO_Shape* shape = &file->shapes[i];
        if (g_view.layers[LAYER_GROUPS + shape->group])
            DrawShape(shape, g_groupColors[shape->group]);
    }
    // Not seen from the camera of the map: they would cover the view
    const MAPINFO_Camera* selectedCamera = SelectedCamera();
    if (!g_view.mapCamera && (g_view.layers[LAYER_CAMERAS] || selectedCamera != NULL)) {
        for (uint32_t i = 0; i < file->cameraCount; i++) {
            const MAPINFO_Camera* camera = &file->cameras[i];
            if (camera == selectedCamera) {
                Camera3D view = MapCamera(camera);
                DrawCameraGizmo(view, g_selectedColor);
                DrawLine3D(view.position, view.target, Fade(g_selectedColor, 0.5f));
            }
            else if (g_view.layers[LAYER_CAMERAS])
                DrawCameraGizmo(MapCamera(camera), Fade(g_cameraColor, 0.6f));
        }
    }
    if (g_view.layers[LAYER_LIGHTS]) {
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
    if (g_view.layers[LAYER_CHARACTERS]) {
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            const Character* character = &g_view.characters[i];
            if (!character->positioned)
                continue;
            bool selected = g_view.selected >= 0 && g_view.items[g_view.selected].kind == ITEM_CHARACTER && g_view.items[g_view.selected].index == i;
            Color color = selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor;
            DrawCylinderWires(character->position, MAPINFOVIEW_RADIUS, MAPINFOVIEW_RADIUS, MAPINFOVIEW_HEIGHT, 8, color);
            // Direction it faces: angle 0 faces +Z
            float yaw = character->angle * DEG2RAD;
            Vector3 eyes = Vector3Add(character->position, (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT * 0.9f, 0.0f });
            DrawLine3D(eyes, Vector3Add(eyes, (Vector3) { sinf(yaw) * 0.5f, 0.0f, cosf(yaw) * 0.5f }), color);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

static void DrawLabel(Camera3D camera, Rectangle area, Font font, float fontSize, Vector3 position, const char* text, Color color) {
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    if (Vector3DotProduct(Vector3Subtract(position, camera.position), forward) <= 0.0f)
        return;
    Vector2 point = GetWorldToScreenEx(position, camera, (int)area.width, (int)area.height);
    point = (Vector2) { area.x + point.x, area.y + point.y };
    if (!CheckCollisionPointRec(point, area))
        return;
    Vector2 size = MeasureTextEx(font, text, fontSize, 1.0f);
    Rectangle box = { point.x - size.x * 0.5f - 3.0f, point.y - fontSize - 1.0f, size.x + 6.0f, fontSize + 2.0f };
    DrawRectangleRec(box, Fade(BLACK, 0.6f));
    DrawTextEx(font, text, (Vector2) { box.x + 3.0f, box.y + 1.0f }, fontSize, 1.0f, color);
}

static void DrawLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    const MAPINFO_File* file = &g_view.file;
    if (g_view.layers[LAYER_CHARACTERS]) {
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            const Character* character = &g_view.characters[i];
            if (character->positioned)
                DrawLabel(camera, area, font, fontSize, Vector3Add(character->position, (Vector3) { 0.0f, MAPINFOVIEW_HEIGHT + 0.1f, 0.0f }),
                    character->id, character->sleep ? GRAY : g_characterColor);
        }
    }
    if (g_view.layers[LAYER_CAMERAS] && !g_view.mapCamera) {
        for (uint32_t i = 0; i < file->cameraCount; i++)
            DrawLabel(camera, area, font, fontSize, MapCamera(&file->cameras[i]).position, file->cameras[i].id,
                &file->cameras[i] == SelectedCamera() ? g_selectedColor : g_cameraColor);
    }
}

static void DrawScene(Rectangle area, Font font, float fontSize) {
    const MAPINFO_Camera* source = g_view.mapCamera ? SelectedCamera() : NULL;
    Camera3D camera;
    Rectangle view = area;
    double farPlane = g_view.camera.distance + g_view.radius * 4.0;
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    if (source != NULL) {
        // The screen of the game, letterboxed
        camera = MapCamera(source);
        view.width = fminf(area.width, area.height * MAPINFOVIEW_ASPECT);
        view.height = view.width / MAPINFOVIEW_ASPECT;
        view.x = roundf(area.x + (area.width - view.width) * 0.5f);
        view.y = roundf(area.y + (area.height - view.height) * 0.5f);
        DrawRectangleRec(area, BLACK);
        DrawRectangleRec(view, (Color) { 45, 45, 48, 255 });
        BeginMode3D(camera);
        rlViewport((int)view.x, GetRenderHeight() - (int)(view.y + view.height), (int)view.width, (int)view.height);
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        double nearPlane = 0.05;
        double top = nearPlane * tan(camera.fovy * 0.5 * DEG2RAD);
        double right = top * view.width / view.height;
        rlFrustum(-right, right, -top, top, nearPlane, g_view.radius * 4.0 + 100.0);
        rlMatrixMode(RL_MODELVIEW);
    }
    else
        camera = OrbitCamera_Begin3D(&g_view.camera, area, g_view.camera.distance * 0.01, farPlane);

    if (g_view.layers[LAYER_MAP])
        DrawMap(camera);
    DrawOverlays();
    OrbitCamera_End3D();
    if (g_view.layers[LAYER_LABELS])
        DrawLabels(camera, view, font, fontSize);
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// Toggles of the layers. The style of the application draws the pressed controls like the normal ones: highlight them
static void DrawLayerToggles(Rectangle area) {
    int pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
    float width = fminf(100.0f, (area.width - (LAYER_COUNT - 1) * 4.0f) / LAYER_COUNT);
    for (int i = 0; i < LAYER_COUNT; i++)
        GuiToggle((Rectangle) { area.x + i * (width + 4.0f), area.y, width, area.height }, g_layerNames[i], &g_view.layers[i]);
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
        TextFormat("Map info, %u chunks: %u cameras, %u lights, %u collision and area shapes, %u characters, %u map models",
            file->chunkCount, file->cameraCount, file->lightCount, file->shapeCount, g_view.characterCount, g_view.modelCount),
        g_view.mapCamera && camera != NULL ?
            TextFormat("Camera %s   V: free camera   R: reset the free camera   Click on the list: select a camera, light or character", camera->id) :
            "Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset   V: camera of the map   Click on the list: select a camera, light or character",
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
    GuiListViewEx(list, g_view.labelPointers, (int)g_view.itemCount, &g_view.listScroll, &active, &g_view.listFocus);
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, alignment);
    if (active != g_view.selected)
        Select(active);

    DrawScene(scene, font, fontSize);
    DrawLayerToggles(controls);
}

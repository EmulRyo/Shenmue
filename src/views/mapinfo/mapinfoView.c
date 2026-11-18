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
#include "core/log.h"
#include "app/gameDB.h"
#include "core/text.h"
#include "formats/map/mapinfo.h"
#include "formats/map/weather.h"
#include "formats/script/chrt.h"
#include "scenario/scnDisplay.h"
#include "render/bounds.h"
#include "render/mt5Render.h"
#include "render/freeCamera.h"
#include "render/scene3D.h"
#include "render/skyRender.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/auth/authView.h"
#include "game/mapDate.h"
#include "game/mapFiles.h"
#include "game/mapProgram.h"
#include "views/mapinfo/mapinfoView.h"
#include "views/mapinfo/mapinfoViewInternal.h"
#include "views/program/programView.h"

#define MAPINFOVIEW_LIST_WIDTH   420.0f
#define MAPINFOVIEW_CAMERA_FOVY  60.0f   // Of the cameras without a field of view curve
#define MAPINFOVIEW_CIRCLE_SIDES 24

const char* g_mapLayerNames[LAYER_COUNT] = {
    "Information", "Map", "Doors", "Cameras", "Lights", "Shapes", "Characters", "Cutscenes", "Flags of the game",
    // The COLS groups, by the meaning of their IDs: COLIsion, HeiGHT, EVeNT, UNDUlation, SOuND, PROPerty
    "Collisions", "Floor heights", "Events", "Slopes", "Sounds", "Properties",
};

static const Color g_groupColors[MAPINFO_GROUP_COUNT] = {
    { 235, 80, 70, 255 }, { 230, 210, 80, 255 }, { 190, 110, 240, 255 },
    { 80, 210, 220, 255 }, { 110, 210, 110, 255 }, { 245, 150, 60, 255 },
};

static const Color g_cameraColor = { 220, 220, 220, 255 };
static const Color g_selectedColor = { 255, 230, 60, 255 };
static const Color g_characterColor = { 90, 170, 255, 255 };

MAPINFOView_State g_map;
static SkyRender g_skyRender;   // The sky drawn, kept from a map to the next (its shader is created at the first draw)
static bool g_skyRenderInit;
MAPINFOView_Options g_mapOptions = {
    .layers = { [LAYER_MAP] = true, [LAYER_DOORS] = true, [LAYER_SHAPES] = true, [LAYER_CHARACTERS] = true },
    .expanded = { [LAYER_INFO] = true, [LAYER_CAMERAS] = true, [LAYER_CHARACTERS] = true, [LAYER_CUTSCENES] = true },
    .labels = true,
    .lights = true,
    .date = true,
    .sky = true,
};

// The eye of the item in the tree. All of them are visible before the tree is created
bool MAPINFOView_IsItemVisible(ItemKind kind, uint32_t index) {
    return g_map.visibleItems == NULL || g_map.visibleItems[g_map.firstItems[kind] + index];
}

static bool IsSelected(ItemKind kind, uint32_t index) {
    return g_map.selected >= 0 && g_map.items[g_map.selected].kind == kind && g_map.items[g_map.selected].index == index;
}

static Vector3 LightPosition(const MAPINFO_Light* light) {
    return (Vector3) { light->position[0], light->position[1], light->position[2] };
}

float MAPINFOView_CameraDuration(const MAPINFO_Camera* camera) {
    float duration = 0.0f;
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++) {
        if (camera->curves[c].count > 0)
            duration = fmaxf(duration, camera->curves[c].times[camera->curves[c].count - 1]);
    }
    return duration;
}

uint32_t MAPINFOView_CameraKeys(const MAPINFO_Camera* camera) {
    uint32_t keys = 0;
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        keys = camera->curves[c].count > keys ? camera->curves[c].count : keys;
    return keys;
}

static const MAPINFO_Camera* SelectedCamera(void) {
    if (g_map.selected < 0 || g_map.items[g_map.selected].kind != ITEM_CAMERA)
        return NULL;
    return &g_map.file.cameras[g_map.items[g_map.selected].index];
}

// Values of the curves of a camera at a time. The transitions from the current camera go from g_map.start to their
// first keys after time 0 (the keys at time 0 are a placeholder); the curves without such a key keep the start value.
// The relative ones are relative to a character that isn't known, so their position and target are moved from the start
// by their values, without rotation: not where the game shows them, but always along the same path
static void CameraValues(const MAPINFO_Camera* camera, float time, float values[AUTH_CAMERA_CURVES]) {
    bool relative = (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) && (camera->flags & MAPINFO_CAMERA_RELATIVE);
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++) {
        const AUTH_Curve* curve = &camera->curves[c];
        float fallback = c == AUTH_CAMERA_FOV ? MAPINFOVIEW_CAMERA_FOVY : 0.0f;
        float origin = relative && c <= AUTH_CAMERA_TARGET_Z ? g_map.start[c] : 0.0f;
        if (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) {
            uint32_t key = 0;
            while (key < curve->count && curve->times[key] <= 0.0f)
                key++;
            if (key == curve->count) {
                values[c] = g_map.start[c];
                continue;
            }
            if (time < curve->times[key]) {
                values[c] = Lerp(g_map.start[c], origin + curve->values[key], time / curve->times[key]);
                continue;
            }
        }
        values[c] = origin + AUTH_CurveValue(curve, time, fallback);
    }
}

// The values of the camera at the current time: the animated cameras are played in a loop. The transitions from the
// current camera that aren't selected are at their end
void MAPINFOView_MapCameraValues(const MAPINFO_Camera* source, float values[AUTH_CAMERA_CURVES]) {
    float duration = MAPINFOView_CameraDuration(source);
    float time = duration > 0.0f ? fmodf(g_map.time, duration) : 0.0f;
    if ((source->flags & MAPINFO_CAMERA_FROM_CURRENT) && source != SelectedCamera())
        time = duration;
    CameraValues(source, time, values);
}

static Camera3D MapCamera(const MAPINFO_Camera* source) {
    float values[AUTH_CAMERA_CURVES];
    MAPINFOView_MapCameraValues(source, values);
    static const float zero = 0.0f;
    AUTH_Curve curves[AUTH_CAMERA_CURVES];
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        curves[c] = (AUTH_Curve) { 1, &zero, &values[c], &zero };
    return Scene3D_CurveCamera(curves, 0.0f, MAPINFOVIEW_CAMERA_FOVY);
}

// The active camera becomes the start of the transitions from the current camera: the camera of the map, or the free one
static void SetStart(void) {
    const MAPINFO_Camera* camera = g_map.mapCamera ? SelectedCamera() : NULL;
    float values[AUTH_CAMERA_CURVES];
    if (camera != NULL)
        MAPINFOView_MapCameraValues(camera, values);
    else {
        Camera3D view = FreeCamera_Camera3D(&g_map.camera);
        float free[AUTH_CAMERA_CURVES] = { view.position.x, view.position.y, view.position.z, view.target.x, view.target.y,
            view.target.z, 0.0f, view.fovy };
        memcpy(values, free, sizeof(values));
    }
    memcpy(g_map.start, values, sizeof(values));
}

// Box around the models of the map, or around the cameras, lights and shapes if there is no model. The characters are
// not included: they can be far from the map
static void ComputeBounds(void) {
    const MAPINFO_File* file = &g_map.file;
    Bounds bounds = Bounds_Empty();
    for (uint32_t i = 0; i < g_map.mapModelCount; i++)
        Bounds_AddSphere(&bounds, g_map.models[i].mesh.center, g_map.models[i].mesh.radius);
    if (g_map.mapModelCount == 0) {
        for (uint32_t i = 0; i < file->cameraCount; i++)
            Bounds_AddPoint(&bounds, MapCamera(&file->cameras[i]).position);
        for (uint32_t i = 0; i < file->lightCount; i++)
            Bounds_AddPoint(&bounds, LightPosition(&file->lights[i]));
        for (uint32_t i = 0; i < file->shapeCount; i++) {
            // The points of the shape, or its center (circles: radius, x, z; rectangles: x, z, half axes)
            const MAPINFO_Shape* shape = &file->shapes[i];
            uint32_t first = shape->type == MAPINFO_SHAPE_CIRCLE ? 1 : 0;
            uint32_t end = shape->type == MAPINFO_SHAPE_CIRCLE ? 3 : shape->type == MAPINFO_SHAPE_RECTANGLE ? 2 : shape->valueCount;
            for (uint32_t v = first; v < end && v + 1 < shape->valueCount; v += 2)
                Bounds_AddPoint(&bounds, ShapePoint(shape->values[v], 0.0f, shape->values[v + 1]));
        }
    }
    if (Bounds_IsEmpty(&bounds))
        bounds = (Bounds) { { -1.0f, 0.0f, -1.0f }, { 1.0f, 2.0f, 1.0f } };
    g_map.bounds = bounds;
    g_map.center = Bounds_Center(&bounds);
    g_map.radius = Bounds_Radius(&bounds, 2.0f);
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_map.camera, g_map.center, (Vector3) { 0.0f, 0.0f, 1.0f }, g_map.radius, SCENE3D_FOVY);
    g_map.camera.pitch = 0.8f;
    g_map.fit = true;
}

// Fits the camera to the models of the map and of the doors, or to the corners of the bounds if there is no model. The
// characters are not included, as in the bounds
static void FitCamera(Rectangle area) {
    g_map.fit = false;
    uint32_t count;
    Vector3* points = MT5Render_InstanceVertices(g_map.instances, g_map.characterInstances, 8, &count);
    if (points == NULL)
        return;
    if (count == 0) {
        for (int i = 0; i < 8; i++)
            points[count++] = (Vector3) { (i & 1) ? g_map.bounds.max.x : g_map.bounds.min.x,
                (i & 2) ? g_map.bounds.max.y : g_map.bounds.min.y, (i & 4) ? g_map.bounds.max.z : g_map.bounds.min.z };
    }
    FreeCamera_Fit(&g_map.camera, points, count, area.width / area.height);
    free(points);
}

void MAPINFOView_Close(void) {
    if (g_map.open)
        g_mapOptions.fly = g_map.camera.fly;
    for (uint32_t i = 0; i < g_map.modelCount; i++)
        MT5Render_FreeModel(&g_map.models[i]);
    free(g_map.models);
    free(g_map.instances);
    free(g_map.visibleInstances);
    free(g_map.renderLights);
    free(g_map.doorInstances);
    free(g_map.mapModelNames);
    free(g_map.mapModelSlots);
    free(g_map.mapModelPackages);
    free(g_map.mapModelVariants);
    free(g_map.mapModelUnloaded);
    free((void*)g_map.doorErrors);
    free(g_map.originalDoors);
    free(g_map.originalLights);
    MT5Render_Free(&g_map.render);
    free(g_map.characters);
    free(g_map.packages);
    free(g_map.items);
    free(g_map.labels);
    free(g_map.failedItems);
    free(g_map.visibleItems);
    for (uint32_t i = 0; i < g_map.scriptCount; i++)
        CHRT_Free(&g_map.scripts[i]);
    free(g_map.scripts);
    free(g_map.cutscenes);
    SD_FreeData(&g_map.program);
    free(g_map.sequences);
    free(g_map.sequenceFound);
    free(g_map.flags);
    MapDate_Close();
    MAPINFO_Free(&g_map.file);
    MAPINFO_Free(&g_map.doorMap);
    memset(&g_map, 0, sizeof(g_map));
}

bool MAPINFOView_Open(const char* filepath) {
    MAPINFO_File file;
    if (!MAPINFO_Load(filepath, &file))
        return false;

    MAPINFOView_Close();
    g_map.file = file;
    snprintf(g_map.filename, sizeof(g_map.filename), "%s", filepath);
    MT5Render_Init(&g_map.render);
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    MAPINFOModels_Load(folder);
    // After the models: a map without door table can take the doors of another one
    g_map.originalDoors = malloc((g_map.file.doorCount + 1) * sizeof(MAPINFO_Door));
    g_map.originalLights = malloc((file.lightCount + 1) * sizeof(MAPINFO_Light));
    if (g_map.originalDoors != NULL && g_map.originalLights != NULL) {
        memcpy(g_map.originalDoors, g_map.file.doors, g_map.file.doorCount * sizeof(MAPINFO_Door));
        memcpy(g_map.originalLights, file.lights, file.lightCount * sizeof(MAPINFO_Light));
    }
    // What the program does with the map, if it has one
    if (!MapProgram_Read(filepath, &g_map.file, &g_map.program))
        SD_FreeData(&g_map.program);
    SCNSky_Find(&g_map.file, &g_map.program, &g_map.sky);
    // The models of the map at the date of the game, if its program can be emulated
    g_map.hasDate = MapDate_Open(filepath, &g_map.file);
    MAPINFODate_FindVariants();
    MAPINFODate_Init();
    // After the models, for the doors whose model couldn't be loaded
    if (!MAPINFOItems_FindSequences() || !MAPINFOItems_FindFlags() || g_map.originalDoors == NULL || g_map.originalLights == NULL || !MAPINFOItems_ReadCutscenes() || !MAPINFOItems_Create()) {
        Log_Error("[MAPINFOView_Open]: Memory error\n");
        MAPINFOView_Close();
        return false;
    }
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_map.camera, g_mapOptions.fly);
    g_map.selected = -1;
    g_map.mapExpanded = true;

    Log_Info("[MAPINFOView_Open]: %u chunks, %u cameras, %u lights, %u shapes, %u characters (%u shown with their model), %u map models, %u of %u doors\n",
        file.chunkCount, file.cameraCount, file.lightCount, file.shapeCount, g_map.characterCount, g_map.modelledCharacters,
        g_map.mapModelCount, g_map.loadedDoors, g_map.file.doorCount);
    g_map.open = true;
    return true;
}

bool MAPINFOView_IsOpen(void) {
    return g_map.open;
}

// The edits of the properties, in memory: the doors, the lights and the characters differ from the file
static bool IsItemEdited(const Item* item) {
    switch (item->kind) {
    case ITEM_DOOR:
        return memcmp(&g_map.file.doors[item->index], &g_map.originalDoors[item->index], sizeof(MAPINFO_Door)) != 0;
    case ITEM_LIGHT:
        return memcmp(&g_map.file.lights[item->index], &g_map.originalLights[item->index], sizeof(MAPINFO_Light)) != 0;
    case ITEM_CHARACTER: {
        const MapCharacter* character = &g_map.characters[item->index];
        return memcmp(character->info.position, character->original.position, sizeof(character->info.position)) != 0 ||
            memcmp(character->info.angle, character->original.angle, sizeof(character->info.angle)) != 0;
    }
    default:
        return false;
    }
}

static bool IsAnyEdited(void) {
    for (uint32_t i = 0; i < g_map.itemCount; i++) {
        if (IsItemEdited(&g_map.items[i]))
            return true;
    }
    return false;
}

// The instance of a door, after its placement is edited
void MAPINFOView_UpdateDoor(uint32_t door) {
    if (g_map.doorInstances[door] < 0)
        return;
    MT5Render_Instance* instance = &g_map.instances[g_map.doorInstances[door]];
    instance->transform = MAPINFOModels_DoorTransform(&g_map.file.doors[door]);
    instance->center = Vector3Transform(instance->model->mesh.center, instance->transform);
}

// The values of the file
static void RestoreAll(void) {
    memcpy(g_map.file.doors, g_map.originalDoors, g_map.file.doorCount * sizeof(MAPINFO_Door));
    memcpy(g_map.file.lights, g_map.originalLights, g_map.file.lightCount * sizeof(MAPINFO_Light));
    for (uint32_t i = 0; i < g_map.file.doorCount; i++)
        MAPINFOView_UpdateDoor(i);
    for (uint32_t i = 0; i < g_map.characterCount; i++)
        g_map.characters[i].info = g_map.characters[i].original;
    MAPINFOModels_ShowCharacters();
}

// A camera of the tree shows the map from it; the other items are looked at with the free camera
static void Select(int item) {
    SetStart();
    g_map.selected = item;
    g_map.mapCamera = false;
    if (item < 0)
        return;
    const Item* selected = &g_map.items[item];
    if (selected->kind == ITEM_CAMERA) {
        g_map.mapCamera = true;
        g_map.time = 0.0f;
        return;
    }
    Vector3 target;
    if (selected->kind == ITEM_LIGHT)
        target = LightPosition(&g_map.file.lights[selected->index]);
    else if (selected->kind == ITEM_CHARACTER && g_map.characters[selected->index].info.positioned)
        target = Vector3Add(MAPINFOModels_CharacterPosition(&g_map.characters[selected->index].info), (Vector3) { 0.0f, SCENE3D_CHARACTER_HEIGHT * 0.5f, 0.0f });
    else if (selected->kind == ITEM_DOOR) {
        const float* position = g_map.file.doors[selected->index].position;
        target = (Vector3) { position[0], position[1] + 1.0f, position[2] };
    }
    else if (selected->kind == ITEM_SHAPE)
        target = MAPINFOItems_ShapeCenter(&g_map.file.shapes[selected->index]);
    else if (selected->kind == ITEM_MAP_MODEL) {
        // The whole model
        const MT5Mesh* mesh = &g_map.models[selected->index].mesh;
        g_map.camera.target = mesh->center;
        g_map.camera.distance = mesh->radius / sinf(SCENE3D_FOVY * 0.5f * DEG2RAD);
        return;
    }
    else
        return;
    g_map.camera.target = target;
    g_map.camera.distance = 6.0f;
}

// The selected cutscene, if it isn't an empty chunk. -1 if none
static int SelectedCutscene(void) {
    if (g_map.selected < 0 || g_map.items[g_map.selected].kind != ITEM_CUTSCENE)
        return -1;
    uint32_t index = g_map.items[g_map.selected].index;
    return g_map.cutscenes[index].valid ? (int)index : -1;
}

// The selected cutscene loaded by the program, NULL if none or if its AUTH file isn't found
static const SD_Reference* SelectedSequence(void) {
    if (g_map.selected < 0 || g_map.items[g_map.selected].kind != ITEM_SEQUENCE ||
        !g_map.sequenceFound[g_map.items[g_map.selected].index])
        return NULL;
    return MAPINFOItems_Sequence(g_map.items[g_map.selected].index);
}

void MAPINFOView_Toolbar(void) {
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the free camera", KEY_R)) {
        ResetCamera();
        g_map.mapCamera = false;
    }
    bool fly = !g_map.mapCamera && g_map.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the free camera, from the camera of the map if it is shown", KEY_F, &fly)) {
        // Flies from the position of the camera of the map, or of the free camera, looking around it
        if (fly && g_map.mapCamera && SelectedCamera() != NULL)
            FreeCamera_LookFrom(&g_map.camera, MapCamera(SelectedCamera()));
        FreeCamera_SetFly(&g_map.camera, fly);
        g_map.mapCamera = false;
    }
    if (SelectedCamera() == NULL)
        GuiDisable();
    int mode = g_map.mapCamera ? 1 : 0;
    if (Toolbar_Choice("Free camera;Map camera", "Show the map from the free camera or from the selected camera", KEY_C, &mode)) {
        // A transition from the current camera starts again from the free camera
        if (mode == 1 && (SelectedCamera()->flags & MAPINFO_CAMERA_FROM_CURRENT)) {
            SetStart();
            g_map.time = 0.0f;
        }
        g_map.mapCamera = mode == 1;
    }
    GuiEnable();
    Toolbar_Separator();
    Toolbar_Toggle(ICON_TEXT_A, "Labels", "Show the names of the characters and the IDs of the cameras", KEY_L, &g_mapOptions.labels);
    Toolbar_Toggle(ICON_STAR, "Lights", "Light the characters with the lights of the map (the group 0 of the LGHT chunk: its "
        "ambient color and its point lights), else from the camera. The exteriors are always lit from the camera: the game "
        "also lights them by the hour and the weather", KEY_I, &g_mapOptions.lights);
    MAPINFO_Fog fog;
    if (g_map.sky.source == SCNSKY_NONE && !MAPINFODate_Fog(&fog))
        GuiDisable();
    Toolbar_Toggle(ICON_FILETYPE_IMAGE, "Sky", "Draw the sky of the map behind it (SCROLLnn.SPR of the sprite folder): the one of its "
        "SCRL chunk or of its program, or the one that the game chooses by the hour and the weather of the date. With it, the "
        "fog and the light that the game sets by the hour and the weather in the maps whose program lets it, or the fog of the "
        "set of the FOG chunk that the program applies", KEY_Y,
        &g_mapOptions.sky);
    GuiEnable();
    Toolbar_Separator();
    // The date: the program of the map shows its models then
    if (!g_map.hasDate)
        GuiDisable();
    Toolbar_Toggle(ICON_CLOCK, "Date", "Show the models of the map that its program shows at the date and the hour of the game "
        "(the events of the day of WEATHER.BIN: snow on the ground, Christmas, New Year)", KEY_T, &g_mapOptions.date);
    if (!g_mapOptions.date)
        GuiDisable();
    if (Toolbar_Button(ICON_ARROW_LEFT, NULL, "Previous day", KEY_COMMA | TOOLBAR_REPEAT))
        MAPINFODate_Move(-1);
    if (Toolbar_Button(ICON_PLAYER_NEXT, MAPINFODate_Text(&g_mapOptions.gameDate), "Next day with other events (snow on the ground, Christmas, "
        "New Year)", KEY_N))
        MAPINFODate_NextEvents();
    if (Toolbar_Button(ICON_ARROW_RIGHT, NULL, "Next day", KEY_PERIOD | TOOLBAR_REPEAT))
        MAPINFODate_Move(1);
    if (Toolbar_Button(ICON_ALARM, TextFormat("%d:%02d", g_mapOptions.gameDate.hour, g_mapOptions.gameDate.minute),
        TextFormat("Next hour of the date: %s", MAPINFODate_Hours()), KEY_H))
        MAPINFODate_NextHour();
    GuiEnable();
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
        char motionBank[1024];
        bool hasBank = sequence != NULL && MapProgram_SequenceMotionBank(g_map.filename, &g_map.program, sequence, motionBank,
            sizeof(motionBank));
        if (sequence != NULL && !AUTHView_OpenMapSequence(g_map.filename, sequence, hasBank ? motionBank : NULL))
            Log_Error("[MAPINFOView]: Failed to load the cutscene %s of %s\n", sequence->name, sequence->folder);
        else if (sequence == NULL && !AUTHView_OpenMapCutscene(g_map.filename, (uint32_t)cutscene))
            Log_Error("[MAPINFOView]: Failed to load the cutscene %d\n", cutscene);
    }
    GuiEnable();
    // The view of the program of the map too
    if (Toolbar_Button(ICON_CPU, "Program", "Show the program of the map (SCN3 chunk)", KEY_P) &&
        !ProgramView_OpenMap(g_map.filename))
        Log_Error("[MAPINFOView]: Failed to load the program of the map\n");
}

static void UpdateInput(Rectangle scene) {
    if (g_map.fit)
        FitCamera(scene);
    if (!g_map.mapCamera)
        FreeCamera_Update(&g_map.camera, scene, 0.1f);
    g_map.time += Scene3D_FrameTime();
}

// The visible instances: the models of the map, the doors and the characters (only the visible ones have an instance),
// with the changes of another view if it isn't NULL
static void DrawModels(Camera3D camera, const MAPINFOView_Changes* changes) {
    if (g_map.visibleInstances == NULL)
        return;
    // The fog of the weather module or of the FOG chunk, and the light of the weather module, with the sky
    SCNSky_Air air = { 0 };
    bool hasAir = g_mapOptions.sky && MAPINFODate_Air(&air);
    MAPINFO_Fog source = { 0 };
    MT5Render_Fog fog = { 0 };
    if (g_mapOptions.sky && MAPINFODate_Fog(&source))
        fog = (MT5Render_Fog) { true, source.nearDistance, source.farDistance, source.density, { ((source.color >> 16) & 0xFF) / 255.0f,
            ((source.color >> 8) & 0xFF) / 255.0f, (source.color & 0xFF) / 255.0f } };
    MT5Render_SetFog(&g_map.render, &fog);
    // The characters lit by the lights of the other view, by the weather module (its ambient light and the intensity of
    // its light 0, shown with the direction and the color of its tables, which the game doesn't set), or by the map.
    // The models of the map and the doors are lit by the lights of the interiors too, else from the camera: only their
    // triangles without vertex colors, as in the game (e.g. most of the room of scene/02/toki), and without the light
    // that the game gives to each person
    const MT5Render_Lighting* lighting = NULL;
    const MT5Render_Lighting* mapLighting = NULL;
    if (changes != NULL) {
        lighting = changes->lighting;
        mapLighting = lighting;
    }
    else if (g_mapOptions.lights && hasAir && (g_map.sky.air & SCNSKY_AIR_LIGHT)) {
        MT5Render_Lighting* airLighting = &g_map.airLighting;
        Vector3 direction = Vector3Normalize((Vector3) { air.direction[0], air.direction[1], -air.direction[2] });
        *airLighting = (MT5Render_Lighting) { { air.ambient, air.ambient, air.ambient }, 0, NULL, { direction.x, direction.y, direction.z },
            { air.light[0] * air.intensity, air.light[1] * air.intensity, air.light[2] * air.intensity } };
        lighting = airLighting;
    }
    else if (g_mapOptions.lights && g_map.renderLights != NULL && !MAPINFOView_IsOutdoorGroup(0)) {
        g_map.lighting.lightCount = MAPINFOView_GroupLights(0, g_map.lighting.ambient, g_map.renderLights, NULL, g_map.file.lightCount);
        g_map.lighting.lights = g_map.renderLights;
        g_map.lighting.fromCamera = MAPINFOView_CharacterLight(0, g_map.lighting.sunColor);
        lighting = &g_map.lighting;
        mapLighting = lighting;
    }
    if (mapLighting != NULL && mapLighting->fromCamera) {
        g_map.mapLighting = *mapLighting;
        g_map.mapLighting.fromCamera = false;
        memset(g_map.mapLighting.sunColor, 0, sizeof(g_map.mapLighting.sunColor));
        mapLighting = &g_map.mapLighting;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_map.mapModelCount && g_mapOptions.layers[LAYER_MAP]; i++) {
        if (!MAPINFOView_IsItemVisible(ITEM_MAP_MODEL, i))
            continue;
        bool shown = !MAPINFODate_IsHidden(i);
        float fade = 0.0f;
        int slot = g_map.mapModelSlots != NULL ? g_map.mapModelSlots[i] : -1;
        if (changes != NULL && changes->mapModel != NULL && slot >= 0)
            changes->mapModel(slot, &shown, &fade);
        if (!shown || fade >= 1.0f)
            continue;
        g_map.visibleInstances[count] = g_map.instances[i];
        g_map.visibleInstances[count].lighting = mapLighting;
        g_map.visibleInstances[count++].fade = fade;
    }
    for (uint32_t i = 0; i < g_map.file.doorCount && g_mapOptions.layers[LAYER_DOORS]; i++) {
        if (g_map.doorInstances[i] >= 0 && MAPINFOView_IsItemVisible(ITEM_DOOR, i)) {
            g_map.visibleInstances[count] = g_map.instances[g_map.doorInstances[i]];
            g_map.visibleInstances[count].frame = MAPINFODate_IsDoorClosed(i) ? MT5MESH_FRAME_CLOSE : MT5MESH_FRAME_OPEN;
            g_map.visibleInstances[count++].lighting = mapLighting;
        }
    }
    // In the order of MAPINFOModels_ShowCharacters, which gives an instance to the shown characters with a model
    uint32_t instance = g_map.characterInstances;
    for (uint32_t i = 0; i < g_map.characterCount && instance < g_map.instanceCount && g_mapOptions.layers[LAYER_CHARACTERS]; i++) {
        const MapCharacter* character = &g_map.characters[i];
        if (!character->shown || character->model < 0 || !MAPINFOView_IsItemVisible(ITEM_CHARACTER, i))
            continue;
        float fade = changes != NULL && changes->characterFade != NULL ? changes->characterFade(character->info.id) : 0.0f;
        if (fade < 1.0f) {
            g_map.visibleInstances[count] = g_map.instances[instance];
            // The objects (e.g. the doors DOOR and DOR2 of scene/02/toki) are lit as the map, without the light of the people
            g_map.visibleInstances[count].lighting = character->info.human || mapLighting == NULL ? lighting : mapLighting;
            g_map.visibleInstances[count++].fade = fade;
        }
        instance++;
    }
    if (count > 0)
        MT5Render_DrawInstances(&g_map.render, g_map.visibleInstances, count, camera);
}

bool MAPINFOView_LightsShown(void) {
    return g_mapOptions.lights;
}

uint32_t MAPINFOView_LightCount(void) {
    return g_map.open ? g_map.file.lightCount : 0;
}

bool MAPINFOView_IsOutdoorGroup(uint32_t group) {
    return g_map.open && group < g_map.file.lightGroupCount && g_map.file.lightGroups[group].mode == MAPINFO_LIGHTS_OUTDOOR;
}

// The ambient color is the last 3 values of the group
uint32_t MAPINFOView_GroupLights(uint32_t group, float ambient[3], MT5Render_Light* lights, uint16_t* ids, uint32_t max) {
    const MAPINFO_File* file = &g_map.file;
    for (int c = 0; c < 3; c++)
        ambient[c] = g_map.open && group < file->lightGroupCount ? file->lightGroups[group].ambient[c + 1] : 1.0f;
    uint32_t count = 0;
    for (uint32_t i = 0; g_map.open && i < file->lightCount && count < max; i++) {
        const MAPINFO_Light* light = &file->lights[i];
        if (light->group != group || !light->on)
            continue;
        MT5Render_Light* l = &lights[count];
        for (int c = 0; c < 3; c++) {
            l->position[c] = light->position[c];
            l->color[c] = light->color[c];
        }
        l->intensity = light->intensity;
        l->fullRange = light->range * light->full;
        l->range = light->range;
        if (ids != NULL)
            ids[count] = light->id;
        count++;
    }
    return count;
}

// The game sets this light of each character before drawing it (code at 0x0C1450E0 in 1ST_READ.BIN PAL): by default a
// directional light from the camera to the character (0x0C144CD8), whatever its state in the group, with the color that
// the game gives it (0.3 gray, set at 0x0C144A34) instead of the one of the group, and the intensity of the group
bool MAPINFOView_CharacterLight(uint32_t group, float color[3]) {
    const MAPINFO_File* file = &g_map.file;
    for (uint32_t i = 0; g_map.open && i < file->lightCount; i++) {
        const MAPINFO_Light* light = &file->lights[i];
        if (light->group == group && light->id == MAPINFOVIEW_CHARACTER_LIGHT) {
            for (int c = 0; c < 3; c++)
                color[c] = MAPINFOVIEW_CHARACTER_LIGHT_COLOR * light->intensity;
            return true;
        }
    }
    return false;
}

const char* MAPINFOView_Path(void) {
    return g_map.open ? g_map.filename : NULL;
}

void MAPINFOView_DrawMap(Camera3D camera, const MAPINFOView_Changes* changes) {
    if (g_map.open)
        DrawModels(camera, changes);
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
    Scene3D_DrawCameraGizmo(camera, 0.6f, SCENE3D_GAME_ASPECT, false, color);
}

// The layers over the map, always visible
static void DrawOverlays(void) {
    const MAPINFO_File* file = &g_map.file;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    // The selected one too, over the others
    for (uint32_t i = 0; i < file->shapeCount; i++) {
        const MAPINFO_Shape* shape = &file->shapes[i];
        if (g_mapOptions.layers[LAYER_SHAPES] && g_mapOptions.layers[LAYER_GROUPS + shape->group] && MAPINFOView_IsItemVisible(ITEM_SHAPE, i) &&
            !IsSelected(ITEM_SHAPE, i))
            DrawShape(shape, g_groupColors[shape->group]);
    }
    if (g_map.selected >= 0 && g_map.items[g_map.selected].kind == ITEM_SHAPE)
        DrawShape(&file->shapes[g_map.items[g_map.selected].index], g_selectedColor);
    // Not seen from the camera of the map: they would cover the view. The selected one is drawn even if it is hidden
    const MAPINFO_Camera* selectedCamera = SelectedCamera();
    if (!g_map.mapCamera) {
        for (uint32_t i = 0; i < file->cameraCount; i++) {
            const MAPINFO_Camera* camera = &file->cameras[i];
            if (camera == selectedCamera) {
                Camera3D view = MapCamera(camera);
                DrawCameraGizmo(view, g_selectedColor);
                DrawLine3D(view.position, view.target, Fade(g_selectedColor, 0.5f));
            }
            else if (g_mapOptions.layers[LAYER_CAMERAS] && MAPINFOView_IsItemVisible(ITEM_CAMERA, i))
                DrawCameraGizmo(MapCamera(camera), Fade(g_cameraColor, 0.6f));
        }
    }
    for (uint32_t i = 0; i < file->lightCount; i++) {
        bool selected = IsSelected(ITEM_LIGHT, i);
        if (selected || (g_mapOptions.layers[LAYER_LIGHTS] && MAPINFOView_IsItemVisible(ITEM_LIGHT, i))) {
            const MAPINFO_Light* light = &file->lights[i];
            Vector3 position = LightPosition(light);
            Color color = ColorFromNormalized((Vector4) { light->color[0], light->color[1], light->color[2], 1.0f });
            DrawSphere(position, selected ? 0.15f : 0.08f, color);
            Vector3 direction = { light->direction[0], light->direction[1], light->direction[2] };
            DrawLine3D(position, Vector3Add(position, Vector3Scale(direction, 0.5f)), selected ? g_selectedColor : color);
        }
    }
    // The shown characters without their model are drawn as cylinders, and the selected one too
    for (uint32_t i = 0; i < g_map.characterCount; i++) {
        const CHRT_Character* character = &g_map.characters[i].info;
        bool selected = IsSelected(ITEM_CHARACTER, i);
        bool visible = g_mapOptions.layers[LAYER_CHARACTERS] && g_map.characters[i].shown && MAPINFOView_IsItemVisible(ITEM_CHARACTER, i);
        if (!character->positioned || (!selected && (!visible || g_map.characters[i].model >= 0)))
            continue;
        Color color = selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor;
        DrawCylinderWires(MAPINFOModels_CharacterPosition(character), SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_HEIGHT, 8, color);
        // Direction it faces: angle 0 faces -Z, as the bodies of the models
        float yaw = character->angle[1] * DEG2RAD;
        Vector3 eyes = Vector3Add(MAPINFOModels_CharacterPosition(character), (Vector3) { 0.0f, SCENE3D_CHARACTER_HEIGHT * 0.9f, 0.0f });
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
    const MAPINFO_File* file = &g_map.file;
    for (uint32_t i = 0; i < g_map.characterCount; i++) {
        const CHRT_Character* character = &g_map.characters[i].info;
        bool selected = IsSelected(ITEM_CHARACTER, i);
        bool visible = g_mapOptions.layers[LAYER_CHARACTERS] && g_map.characters[i].shown && MAPINFOView_IsItemVisible(ITEM_CHARACTER, i);
        if (character->positioned && (visible || selected))
            DrawLabel(camera, area, font, fontSize, Vector3Add(MAPINFOModels_CharacterPosition(character), (Vector3) { 0.0f, SCENE3D_CHARACTER_HEIGHT + 0.1f, 0.0f }),
                GameDB_CharacterDisplay(character->id), selected ? g_selectedColor : character->sleep ? GRAY : g_characterColor);
    }
    for (uint32_t i = 0; i < file->cameraCount && !g_map.mapCamera; i++) {
        bool selected = &file->cameras[i] == SelectedCamera();
        if (selected || (g_mapOptions.layers[LAYER_CAMERAS] && MAPINFOView_IsItemVisible(ITEM_CAMERA, i)))
            DrawLabel(camera, area, font, fontSize, MapCamera(&file->cameras[i]).position, file->cameras[i].id,
                selected ? g_selectedColor : g_cameraColor);
    }
}

// The file of a sky: in the folder of the map (e.g. scroll69.spr of scene/03/mend), else in the sprite folder of the
// content folder. Returns false if it isn't found
static bool SkyPath(const char* name, char* path, size_t size) {
    char lower[SCNSKY_FILE_SIZE];
    snprintf(lower, sizeof(lower), "%s", TextToLower(name));
    snprintf(path, size, "%s/%s", GetDirectoryPath(g_map.filename), lower);
    if (FileExists(path))
        return true;
    char content[1024];
    if (!MapProgram_ContentFolder(g_map.filename, content, sizeof(content)))
        return false;
    snprintf(path, size, "%s/sprite/%s", content, lower);
    return FileExists(path);
}

// The sky of the map seen from the camera, in the view, if it is shown
static void DrawSky(Camera3D camera, Rectangle view) {
    const char* name = g_mapOptions.sky ? MAPINFODate_SkyFile() : NULL;
    char path[1200];
    if (name == NULL || !SkyPath(name, path, sizeof(path)))
        return;
    if (!g_skyRenderInit) {
        SkyRender_Init(&g_skyRender);
        g_skyRenderInit = true;
    }
    // The colors of the background that the weather module sets, over the sky
    SCNSky_Air air;
    bool back = (g_map.sky.air & SCNSKY_AIR_BACK) && MAPINFODate_Air(&air);
    if (SkyRender_Load(&g_skyRender, path))
        SkyRender_Draw(&g_skyRender, camera, view, g_map.sky.offset, back ? air.fog.top : 0, back ? air.fog.bottom : 0);
}

void MAPINFOView_DrawSky(Camera3D camera, Rectangle area) {
    if (g_map.open)
        DrawSky(camera, area);
}

static void DrawScene(Rectangle area, Font font, float fontSize) {
    const MAPINFO_Camera* source = g_map.mapCamera ? SelectedCamera() : NULL;
    Camera3D camera;
    Rectangle view = area;
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    if (source != NULL) {
        camera = MapCamera(source);
        view = Scene3D_Letterbox(area, SCENE3D_GAME_ASPECT);
        DrawSky(camera, view);
        FreeCamera_BeginCamera3D(camera, view, 0.05, g_map.radius * 4.0 + 100.0);
    }
    else {
        DrawSky(FreeCamera_Camera3D(&g_map.camera), area);
        camera = FreeCamera_Begin3D(&g_map.camera, area);
    }

    DrawModels(camera, NULL);
    DrawOverlays();
    FreeCamera_End3D();
    if (g_mapOptions.labels)
        DrawLabels(camera, view, font, fontSize);
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// An item of the tree, with its eye or not. A click sets *clicked. Returns true when its eye is clicked
static bool TreeItem(uint32_t item, bool eye, bool dimmed, int* clicked) {
    bool* visible = eye ? &g_map.visibleItems[item] : NULL;
    bool previous = visible != NULL && *visible;
    int flags = ((int)item == g_map.selected ? TREE_SELECTED : 0) | (g_map.failedItems[item] ? TREE_FAILED : 0) | (dimmed ? TREE_DIMMED : 0) |
        (IsItemEdited(&g_map.items[item]) ? TREE_EDITED : 0);
    if (Tree_Item(&g_map.tree, g_map.labels[item], flags, visible))
        *clicked = (int)item;
    return visible != NULL && *visible != previous;
}

// The items of a kind in a group, whose eye is the layer if they have one
static void TreeLayer(Layer layer, ItemKind kind, uint32_t count, bool eyes, int* clicked) {
    Tree_BeginGroup(&g_map.tree, g_mapLayerNames[layer], &g_mapOptions.expanded[layer], eyes ? &g_mapOptions.layers[layer] : NULL);
    for (uint32_t i = 0; i < count; i++)
        TreeItem(g_map.firstItems[kind] + i, eyes, false, clicked);
    Tree_EndGroup(&g_map.tree);
}

// A group per kind of item. The shapes are in a subgroup per COLS group, and the characters in a subgroup per package,
// whose eye shows or hides the package. A click on an item selects it, or unselects it if it is selected
static void DrawTree(Rectangle bounds) {
    const MAPINFO_File* file = &g_map.file;
    Tree* tree = &g_map.tree;
    int clicked = -1;
    Tree_Begin(tree, bounds);
    TreeLayer(LAYER_INFO, ITEM_INFO, g_map.firstItems[ITEM_MAP_MODEL] - g_map.firstItems[ITEM_INFO], false, &clicked);
    // The models hidden at the date are dimmed
    Tree_BeginGroup(tree, g_mapLayerNames[LAYER_MAP], &g_mapOptions.expanded[LAYER_MAP], &g_mapOptions.layers[LAYER_MAP]);
    for (uint32_t i = 0; i < g_map.mapModelCount; i++)
        TreeItem(g_map.firstItems[ITEM_MAP_MODEL] + i, true, MAPINFODate_IsHidden(i), &clicked);
    Tree_EndGroup(tree);
    TreeLayer(LAYER_DOORS, ITEM_DOOR, file->doorCount, true, &clicked);
    TreeLayer(LAYER_CAMERAS, ITEM_CAMERA, file->cameraCount, true, &clicked);
    TreeLayer(LAYER_LIGHTS, ITEM_LIGHT, file->lightCount, true, &clicked);

    Tree_BeginGroup(tree, g_mapLayerNames[LAYER_SHAPES], &g_mapOptions.expanded[LAYER_SHAPES], &g_mapOptions.layers[LAYER_SHAPES]);
    for (int g = 0; g < MAPINFO_GROUP_COUNT; g++) {
        Tree_BeginGroup(tree, g_mapLayerNames[LAYER_GROUPS + g], &g_mapOptions.expanded[LAYER_GROUPS + g], &g_mapOptions.layers[LAYER_GROUPS + g]);
        for (uint32_t i = 0; i < file->shapeCount; i++) {
            if ((int)file->shapes[i].group == g)
                TreeItem(g_map.firstItems[ITEM_SHAPE] + i, true, false, &clicked);
        }
        Tree_EndGroup(tree);
    }
    Tree_EndGroup(tree);

    // The characters hidden by a character with their ID in a previous package are dimmed
    bool changed = false;
    Tree_BeginGroup(tree, g_mapLayerNames[LAYER_CHARACTERS], &g_mapOptions.expanded[LAYER_CHARACTERS], &g_mapOptions.layers[LAYER_CHARACTERS]);
    for (int p = -1; p < (int)g_map.packageCount; p++) {
        MapPackage* package = p >= 0 ? &g_map.packages[p] : NULL;
        if (package == NULL && (g_map.characterCount == 0 || g_map.characters[0].package >= 0))
            continue;
        Tree_BeginGroup(tree, package != NULL ? MAPINFOItems_PackageText(package) : "CHRD chunk", package != NULL ? &package->expanded : &g_map.mapExpanded,
            package != NULL ? &package->shown : NULL);
        for (uint32_t i = 0; i < g_map.characterCount; i++) {
            if (g_map.characters[i].package == p)
                changed |= TreeItem(g_map.firstItems[ITEM_CHARACTER] + i, true, !g_map.characters[i].shown, &clicked);
        }
        changed |= Tree_EndGroup(tree);
    }
    Tree_EndGroup(tree);
    // The empty chunks are dimmed
    Tree_BeginGroup(tree, g_mapLayerNames[LAYER_CUTSCENES], &g_mapOptions.expanded[LAYER_CUTSCENES], NULL);
    for (uint32_t i = 0; i < file->cutsceneCount; i++)
        TreeItem(g_map.firstItems[ITEM_CUTSCENE] + i, false, !g_map.cutscenes[i].valid, &clicked);
    for (uint32_t i = 0; i < g_map.sequenceCount; i++)
        TreeItem(g_map.firstItems[ITEM_SEQUENCE] + i, false, !g_map.sequenceFound[i], &clicked);
    Tree_EndGroup(tree);
    if (g_map.flagCount > 0)
        TreeLayer(LAYER_FLAGS, ITEM_FLAG, g_map.flagCount, false, &clicked);
    Tree_End(tree);

    if (changed)
        MAPINFOModels_ShowCharacters();
    if (clicked >= 0)
        Select(clicked == g_map.selected ? -1 : clicked);
}

void MAPINFOView_Draw(Rectangle area) {
    if (!g_map.open || area.width <= 0.0f || area.height <= 0.0f)
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
    MAPINFOProperties_Draw(properties);
    DrawScene(scene, font, fontSize);
}

void MAPINFOView_Look(Vector3 position, Vector3 target) {
    if (!g_map.open)
        return;
    FreeCamera_LookFrom(&g_map.camera, (Camera3D) { position, target, { 0.0f, 1.0f, 0.0f }, SCENE3D_FOVY, CAMERA_PERSPECTIVE });
    g_map.mapCamera = false;
}

void MAPINFOView_Status(ViewStatus* status) {
    const MAPINFO_File* file = &g_map.file;
    const MAPINFO_Camera* camera = SelectedCamera();
    snprintf(status->file, sizeof(status->file), "%s", g_map.filename);
    int length = snprintf(status->info, sizeof(status->info),
        "%u cameras, %u lights, %u shapes, %u characters (%u with a model), %u models, %u doors", file->cameraCount,
        file->lightCount, file->shapeCount, g_map.characterCount, g_map.modelledCharacters, g_map.mapModelCount, file->doorCount);
    if (g_map.loadedDoors < file->doorCount)
        snprintf(status->info + length, sizeof(status->info) - length, " (%u without a model)", file->doorCount - g_map.loadedDoors);
    if (g_map.mapCamera && camera != NULL)
        snprintf(status->hint, sizeof(status->hint), "Seen from the camera %s", camera->id);
    else
        snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_map.camera));
}

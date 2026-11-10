#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "app/gameDB.h"
#include "formats/script/chrt.h"
#include "render/mt5Render.h"
#include "render/freeCamera.h"
#include "render/scene3D.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/chrtView.h"

#define CHRTVIEW_FOVY 45.0f
#define CHRTVIEW_FRAME_RATE 15.0f
#define CHRTVIEW_PATH_SIZE 1024
#define CHRTVIEW_LABEL_SIZE 128
#define CHRTVIEW_LIST_WIDTH 360.0f

typedef struct {
    char            path[CHRTVIEW_PATH_SIZE];
    bool            loaded;
    MT5Render_Model model;
} CHRTView_Model;

typedef struct {
    CHRT_Character  info;   // The characters not positioned in the scene are arranged in rows (see ArrangeUnpositioned).
                            // Its position, if it is positioned, and its angle can be edited
    CHRT_Character  original; // As read from the script, to restore the edits
    int32_t         model;  // Index in the model list, -1 if the model can't be loaded
    const char*     error;  // Why the model can't be loaded, NULL if it is
    Vector3         arranged; // Offset of the characters not positioned, to their place in the rows
    Matrix          transform;
    Vector3         center; // Bounding sphere of the model in the scene
    float           radius;
} CHRTView_Character;

// Groups of the characters in the tree
typedef enum {
    GROUP_POSITIONED,
    GROUP_UNPOSITIONED,     // Arranged in rows next to the positioned ones
    GROUP_FAILED,           // Without a model, not drawn
    GROUP_COUNT,
} Group;

static const char* g_groupNames[GROUP_COUNT] = { "Positioned", "Not positioned", "Without a model" };

typedef struct {
    bool                open;
    char                path[1024];  // Of the open file
    CHRT_Script         script;
    MT5Render           render;
    uint32_t            modelCount;
    CHRTView_Model*     models;
    uint32_t            characterCount;
    CHRTView_Character* characters;
    MT5Render_Instance* visible;     // The visible characters, filled before drawing them
    uint32_t            modelledCount;     // Characters with a model
    uint32_t            unpositionedCount; // Characters with a model and without a Position command
    char                (*labels)[CHRTVIEW_LABEL_SIZE];
    bool*               shown;       // Their eye in the tree
    Tree                tree;
    Properties          properties;  // Of the selected character
    Vector3             center;      // Bounding sphere of the scene
    float               radius;
    FreeCamera         camera;
    bool                fit;         // The camera is fitted to the models in the next draw, when the size of the view is known
    int32_t             selected;    // Character the camera looks at, -1 for the whole scene
    uint32_t            frame;       // Animation frame of the animated models
    float               frameTime;
} CHRTView_State;

// Kept when another scene is opened
typedef struct {
    bool                showLabels;
    bool                hideSleeping;
    bool                wireframe;
    bool                noCulling;   // Draw the back faces too
    bool                fly;
    bool                groupShown[GROUP_COUNT];    // Their eye in the tree
    bool                expanded[GROUP_COUNT];
} CHRTView_Options;

static CHRTView_State g_view;
static CHRTView_Options g_options = {
    .showLabels = true,
    .groupShown = { true, true, true },
    .expanded = { true, true, true },
};

// Model file of an Image name: the file of the DefImage with that name, or the name itself (some names are defined in
// other scripts, and they are usually the name of the model file)
static const char* ImageFile(const char* name) {
    const char* file = CHRT_ImageFile(&g_view.script, name);
    return file != NULL ? file : name;
}

// Path of the model file in the folder of the script: <name>.chrm or <name>.mt5, without the extension of the script
// (always .MT5) and the $ and @ prefixes of some names. Returns false if there is none
static bool ModelPath(const char* folder, const char* file, char* path, size_t size) {
    while (*file == '$' || *file == '@')
        file++;
    const char* dot = strrchr(file, '.');
    int length = (int)(dot != NULL ? dot - file : strlen(file));
    const char* extensions[2] = { "chrm", "mt5" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, size, "%s/%.*s.%s", folder, length, file, extensions[i]);
        if (FileExists(path))
            return true;
    }
    return false;
}

// Index of the model, loaded the first time. Returns -1 if it can't be loaded
static int32_t LoadCharacterModel(const char* path) {
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (CHRT_EqualsIgnoreCase(g_view.models[i].path, path))
            return g_view.models[i].loaded ? (int32_t)i : -1;
    }
    CHRTView_Model* model = &g_view.models[g_view.modelCount++];
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->loaded = MT5Render_LoadModel(path, &model->model);
    return model->loaded ? (int32_t)(model - g_view.models) : -1;
}

// As the nodes of the models: rotation around X, Y and Z, and translation. The characters not positioned in the scene
// are placed at the origin. The models of the persons have their origin at the hips (the motions place them), so they
// are lifted to stand at their position
static Matrix CharacterTransform(const CHRT_Character* character, const MT5Render_Model* model) {
    const float* angle = character->angle;
    const float* position = character->position;
    Matrix matrix = MatrixTranslate(0.0f, character->human ? -MT5Render_Bottom(model) : 0.0f, 0.0f);
    matrix = MatrixMultiply(matrix, MatrixRotateX(angle[0] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateY(angle[1] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateZ(angle[2] * DEG2RAD));
    if (!character->positioned)
        return matrix;
    return MatrixMultiply(matrix, MatrixTranslate(position[0], position[1], position[2]));
}

static void LoadCharacter(uint32_t index, const char* folder) {
    CHRTView_Character* character = &g_view.characters[g_view.characterCount++];
    CHRT_ReadCharacter(&g_view.script, index, &character->info);
    character->original = character->info;
    character->model = -1;

    char path[CHRTVIEW_PATH_SIZE];
    if (character->info.image == NULL)
        character->error = "no image";
    else if (!ModelPath(folder, ImageFile(character->info.image), path, sizeof(path)))
        character->error = "no model file";
    else if ((character->model = LoadCharacterModel(path)) < 0)
        character->error = "invalid model file";
    if (character->model < 0) {
        printf("[CHRTView_Open]: No model for %s (%s, %s)\n", character->info.id, character->info.image != NULL ? character->info.image : "no image",
            character->error);
        return;
    }

    const MT5Mesh* mesh = &g_view.models[character->model].model.mesh;
    character->transform = CharacterTransform(&character->info, &g_view.models[character->model].model);
    character->center = Vector3Transform(mesh->center, character->transform);
    character->radius = mesh->radius;
    g_view.modelledCount++;
    g_view.unpositionedCount += !character->info.positioned;
}

// The models of the characters without a Position are at the origin (e.g. the shutters and the counters of the shops
// of a street, placed by other files), so they are arranged in rows next to the positioned ones, not to overlap
static void ArrangeUnpositioned(void) {
    const float gap = 0.5f;
    Vector3 min = { INFINITY, INFINITY, INFINITY }, max = { -INFINITY, -INFINITY, -INFINITY };
    double area = 0.0;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        if (character->model < 0)
            continue;
        if (character->info.positioned) {
            Vector3 extent = { character->radius, character->radius, character->radius };
            min = Vector3Min(min, Vector3Subtract(character->center, extent));
            max = Vector3Max(max, Vector3Add(character->center, extent));
        }
        else
            area += (2.0 * character->radius + gap) * (2.0 * character->radius + gap);
    }
    bool positioned = g_view.unpositionedCount < g_view.modelledCount;
    float startX = positioned ? max.x + gap * 4.0f : 0.0f;
    float startZ = positioned ? min.z : 0.0f;
    float rowWidth = (float)sqrt(area);

    float x = 0.0f, z = 0.0f, rowDepth = 0.0f;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        CHRTView_Character* character = &g_view.characters[i];
        if (character->model < 0 || character->info.positioned)
            continue;
        float size = 2.0f * character->radius;
        if (x > 0.0f && x + size > rowWidth) {
            x = 0.0f;
            z += rowDepth + gap;
            rowDepth = 0.0f;
        }
        Vector3 offset = { startX + x + character->radius - character->center.x, 0.0f, startZ + z + character->radius - character->center.z };
        character->arranged = offset;
        character->transform = MatrixMultiply(character->transform, MatrixTranslate(offset.x, offset.y, offset.z));
        character->center = Vector3Add(character->center, offset);
        x += size + gap;
        rowDepth = fmaxf(rowDepth, size);
    }
}

// Bounding sphere of the characters with a model
static void ComputeBounds(void) {
    Vector3 min = { INFINITY, INFINITY, INFINITY }, max = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        if (character->model < 0)
            continue;
        Vector3 extent = { character->radius, character->radius, character->radius };
        min = Vector3Min(min, Vector3Subtract(character->center, extent));
        max = Vector3Max(max, Vector3Add(character->center, extent));
    }
    if (g_view.modelledCount == 0) {
        g_view.center = Vector3Zero();
        g_view.radius = 1.0f;
        return;
    }
    g_view.center = Vector3Scale(Vector3Add(min, max), 0.5f);
    g_view.radius = fmaxf(Vector3Distance(min, max) * 0.5f, 0.1f);
}

// The ID and the name of each character
static bool CreateLabels(void) {
    g_view.labels = malloc((g_view.characterCount + 1) * sizeof(*g_view.labels));
    g_view.shown = malloc((g_view.characterCount + 1) * sizeof(bool));
    if (g_view.labels == NULL || g_view.shown == NULL)
        return false;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        g_view.shown[i] = true;
        snprintf(g_view.labels[i], CHRTVIEW_LABEL_SIZE, "%s", GameDB_CharacterLabel(character->info.id));
    }
    return true;
}

static Group CharacterGroup(const CHRTView_Character* character) {
    return character->model < 0 ? GROUP_FAILED : character->info.positioned ? GROUP_POSITIONED : GROUP_UNPOSITIONED;
}

// From the front of the characters with angle 0, which face -Z as the bodies of the models
static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, g_view.center, (Vector3) { 0.0f, 0.0f, -1.0f }, g_view.radius, CHRTVIEW_FOVY);
    g_view.selected = -1;
    g_view.fit = true;
}

// Looks at a character from the current direction. The characters without a model are only selected
static void Select(int32_t index) {
    const CHRTView_Character* character = &g_view.characters[index];
    g_view.selected = index;
    if (character->model < 0)
        return;
    g_view.camera.target = character->center;
    g_view.camera.distance = 1.5f * character->radius / sinf(CHRTVIEW_FOVY * 0.5f * DEG2RAD);
}

void CHRTView_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (g_view.models[i].loaded)
            MT5Render_FreeModel(&g_view.models[i].model);
    }
    free(g_view.models);
    free(g_view.characters);
    free(g_view.visible);
    free(g_view.labels);
    free(g_view.shown);
    CHRT_Free(&g_view.script);
    MT5Render_Free(&g_view.render);
    memset(&g_view, 0, sizeof(g_view));
}

bool CHRTView_Open(const char* filepath) {
    CHRT_Script script;
    if (!CHRT_Load(filepath, &script))
        return false;

    CHRTView_Close();
    g_view.script = script;
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    uint32_t count = 0;
    for (uint32_t c = 0; c < script.commandCount; c++)
        count += script.commands[c].parent == -1 && CHRT_IsCommand(&script.commands[c], "Character");
    g_view.characters = calloc(count + 1, sizeof(CHRTView_Character));
    g_view.models = calloc(count + 1, sizeof(CHRTView_Model));
    g_view.visible = calloc(count + 1, sizeof(MT5Render_Instance));
    if (g_view.characters == NULL || g_view.models == NULL || g_view.visible == NULL) {
        fputs("[CHRTView_Open]: Memory error\n", stderr);
        CHRTView_Close();
        return false;
    }

    MT5Render_Init(&g_view.render);
    char folder[CHRTVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    for (uint32_t c = 0; c < script.commandCount; c++) {
        if (script.commands[c].parent == -1 && CHRT_IsCommand(&script.commands[c], "Character"))
            LoadCharacter(c, folder);
    }
    printf("[CHRTView_Open]: %u of %u characters with a model (%u without a position), %u models\n", g_view.modelledCount,
        g_view.characterCount, g_view.unpositionedCount, g_view.modelCount);
    if (!CreateLabels()) {
        fputs("[CHRTView_Open]: Memory error\n", stderr);
        CHRTView_Close();
        return false;
    }

    ArrangeUnpositioned();
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.open = true;
    return true;
}

bool CHRTView_IsOpen(void) {
    return g_view.open;
}

// With a model, shown in the tree with its group, and not a sleeping one if they are hidden
static bool IsVisible(const CHRTView_Character* character) {
    return character->model >= 0 && !(g_options.hideSleeping && character->info.sleep) && g_view.shown[character - g_view.characters] &&
        g_options.groupShown[CharacterGroup(character)];
}

// Selects the next or previous visible character
static void SelectNext(int direction) {
    int32_t count = (int32_t)g_view.characterCount;
    int32_t index = g_view.selected >= 0 ? g_view.selected : (direction > 0 ? -1 : 0);
    for (int32_t i = 0; i < count; i++) {
        index = ((index + direction) % count + count) % count;
        if (IsVisible(&g_view.characters[index])) {
            Select(index);
            Tree_Reveal(&g_view.tree, true);
            return;
        }
    }
}

// Fills the instances of the visible characters, with the current frame of the animated models. Returns their count
static uint32_t VisibleInstances(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        if (!IsVisible(character))
            continue;
        MT5Render_Model* model = &g_view.models[character->model].model;
        uint32_t frame = model->mesh.frameCount > 0 ? g_view.frame % model->mesh.frameCount : 0;
        g_view.visible[count++] = (MT5Render_Instance) { model, character->transform, character->center, frame };
    }
    return count;
}

// Fits the camera to the visible models
static void FitCamera(Rectangle area) {
    g_view.fit = false;
    uint32_t count;
    Vector3* points = MT5Render_InstanceVertices(g_view.visible, VisibleInstances(), 0, &count);
    if (points == NULL)
        return;
    FreeCamera_Fit(&g_view.camera, points, count, area.width / area.height);
    free(points);
}

// The edits of the properties, in memory: the position and the angle differ from the file
static bool IsEdited(const CHRTView_Character* character) {
    return memcmp(character->info.position, character->original.position, sizeof(character->info.position)) != 0 ||
        memcmp(character->info.angle, character->original.angle, sizeof(character->info.angle)) != 0;
}

static bool IsAnyEdited(void) {
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        if (IsEdited(&g_view.characters[i]))
            return true;
    }
    return false;
}

// The placement of the model, after the position or the angle is edited
static void UpdateCharacter(CHRTView_Character* character) {
    if (character->model < 0)
        return;
    const MT5Render_Model* model = &g_view.models[character->model].model;
    character->transform = MatrixMultiply(CharacterTransform(&character->info, model),
        MatrixTranslate(character->arranged.x, character->arranged.y, character->arranged.z));
    character->center = Vector3Transform(model->mesh.center, character->transform);
}

// The values of the file
static void RestoreAll(void) {
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        CHRTView_Character* character = &g_view.characters[i];
        if (IsEdited(character)) {
            character->info = character->original;
            UpdateCharacter(character);
        }
    }
}

void CHRTView_Toolbar(void) {
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the camera", KEY_R))
        ResetCamera();
    bool fly = g_view.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the camera", KEY_F, &fly))
        FreeCamera_SetFly(&g_view.camera, fly);
    Toolbar_Separator();
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous character", TOOLBAR_REPEAT | TOOLBAR_SHIFT | KEY_TAB))
        SelectNext(-1);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next character", TOOLBAR_REPEAT | KEY_TAB))
        SelectNext(1);
    Toolbar_Separator();
    Toolbar_Toggle(ICON_TEXT_A, "Labels", "Show the names of the characters", KEY_L, &g_options.showLabels);
    bool sleeping = !g_options.hideSleeping;
    if (Toolbar_Toggle(ICON_EYE_ON, "Sleeping", "Show the sleeping characters", KEY_H, &sleeping))
        g_options.hideSleeping = !sleeping;
    Toolbar_Toggle(ICON_GRID, "Wireframe", "Draw the edges of the triangles", KEY_Z, &g_options.wireframe);
    Toolbar_Toggle(ICON_CUBE_FACE_BACK, "Back faces", "Draw the back faces", KEY_B, &g_options.noCulling);
    Toolbar_Separator();
    if (!IsAnyEdited())
        GuiDisable();
    if (Toolbar_Button(ICON_UNDO, "Restore", "Restore the values of the file of the edited characters", 0))
        RestoreAll();
    GuiEnable();
}

static void UpdateInput(Rectangle area) {
    FreeCamera_Update(&g_view.camera, area, g_view.radius * 0.001f);
    g_view.frameTime += Scene3D_FrameTime();
    for (; g_view.frameTime >= 1.0f / CHRTVIEW_FRAME_RATE; g_view.frameTime -= 1.0f / CHRTVIEW_FRAME_RATE)
        g_view.frame++;
}

// Name of the characters (their ID if they have none), over the top of their model. A click on a label selects the character
static void DrawLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    int32_t clicked = -1;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        Vector2 point;
        if (!IsVisible(character) ||
            !Scene3D_ScreenPoint(camera, area, Vector3Add(character->center, (Vector3) { 0.0f, character->radius, 0.0f }), &point))
            continue;

        const char* name = GameDB_CharacterDisplay(character->info.id);
        const char* text = character->info.sleep ? TextFormat("%s (sleep)", name) : name;
        Color color = (int32_t)i == g_view.selected ? YELLOW : character->info.sleep ? GRAY : LIGHTGRAY;
        Rectangle box = Scene3D_DrawLabel(point, text, font, fontSize, color);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), box))
            clicked = (int32_t)i; // The last one drawn, on top of the others
    }
    if (clicked >= 0) {
        Select(clicked);
        Tree_Reveal(&g_view.tree, true);
    }
}

// The commands of the selected character, and its model
static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_view.properties;
    if (g_view.selected < 0) {
        Properties_Begin(properties, bounds, NULL);
        Properties_Note(properties, "Select a character of the tree or click its label");
        Properties_End(properties);
        return;
    }
    CHRTView_Character* character = &g_view.characters[g_view.selected];
    CHRT_Character* info = &character->info;
    const float* p = info->position;
    Properties_Begin(properties, bounds, TextFormat("Character %s", GameDB_CharacterDisplay(info->id)));
    Properties_Text(properties, "ID", info->id);
    GameDB_CharacterProperties(properties, info->id);
    Properties_Text(properties, "Image", info->image != NULL ? info->image : "none");
    // The characters not positioned stay in their row
    bool changed = false;
    if (info->positioned)
        changed = Properties_EditVector(properties, "Position", info->position, character->original.position, 3, 0.01f);
    else
        Properties_Text(properties, "Position", !info->hasPosition ? "none" : TextFormat("off the scene (%.3f, %.3f, %.3f)", p[0], p[1], p[2]));
    changed |= Properties_EditVector(properties, "Angle", info->angle, character->original.angle, 3, 0.5f);
    if (changed)
        UpdateCharacter(character);
    Properties_Bool(properties, "Sleep", info->sleep);
    Properties_Bool(properties, "Human", info->human);
    Properties_Bool(properties, "Arm of Ryo", info->arm);
    if (character->model < 0)
        Properties_Text(properties, "Model", character->error);
    else {
        const CHRTView_Model* model = &g_view.models[character->model];
        const MT5Mesh* mesh = &model->model.mesh;
        Properties_Section(properties, "Model");
        Properties_Text(properties, "File", GetFileName(model->path));
        Properties_Int(properties, "Nodes", model->model.nodeCount);
        Properties_Int(properties, "Triangles", mesh->triangleCount);
        Properties_Text(properties, "Textures", TextFormat("%u/%u", model->model.loadedTextures, model->model.textureCount));
        Properties_Int(properties, "Animation frames", mesh->frameCount);
        Properties_Float(properties, "Radius", mesh->radius);
    }
    Properties_End(properties);
}

// The characters by group. A click selects a character, or unselects it if it is selected. The sleeping characters are
// dimmed while they are hidden
static void DrawTree(Rectangle bounds) {
    int32_t clicked = -1;
    Tree_Begin(&g_view.tree, bounds);
    for (int g = 0; g < GROUP_COUNT; g++) {
        bool drawn = g != GROUP_FAILED;
        Tree_BeginGroup(&g_view.tree, g_groupNames[g], &g_options.expanded[g], drawn ? &g_options.groupShown[g] : NULL);
        for (uint32_t i = 0; i < g_view.characterCount; i++) {
            const CHRTView_Character* character = &g_view.characters[i];
            if ((int)CharacterGroup(character) != g)
                continue;
            int flags = ((int32_t)i == g_view.selected ? TREE_SELECTED : 0) | (character->model < 0 ? TREE_FAILED : 0) |
                (g_options.hideSleeping && character->info.sleep ? TREE_DIMMED : 0) | (IsEdited(character) ? TREE_EDITED : 0);
            if (Tree_Item(&g_view.tree, g_view.labels[i], flags, drawn ? &g_view.shown[i] : NULL))
                clicked = (int32_t)i;
        }
        Tree_EndGroup(&g_view.tree);
    }
    Tree_End(&g_view.tree);
    if (clicked >= 0 && clicked == g_view.selected)
        g_view.selected = -1;
    else if (clicked >= 0)
        Select(clicked);
}

void CHRTView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float margin = 10.0f;
    float top = area.y + margin;
    Rectangle column = { area.x + margin, top, CHRTVIEW_LIST_WIDTH, area.y + area.height - margin - top };
    Rectangle scene = { column.x + column.width + margin, top, area.width - column.width - margin * 3.0f, column.height };
    Rectangle list, properties;
    Properties_Split(column, &list, &properties);
    if (list.height <= 0.0f || scene.width <= 0.0f)
        return;
    UpdateInput(scene);
    if (g_view.fit)
        FitCamera(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    DrawTree(list);
    DrawProperties(properties);

    Camera3D camera = FreeCamera_Begin3D(&g_view.camera, scene);
    // The models have explicit back faces where both sides are seen (see MT5View_Draw)
    if (g_options.noCulling)
        rlDisableBackfaceCulling();
    if (g_options.wireframe)
        rlEnableWireMode();
    MT5Render_DrawInstances(&g_view.render, g_view.visible, VisibleInstances(), camera);
    rlDisableWireMode();
    rlEnableBackfaceCulling();
    FreeCamera_End3D();
    if (g_options.showLabels)
        DrawLabels(camera, scene, font, fontSize);
    DrawRectangleLinesEx(scene, 1.0f, (Color) { 112, 112, 112, 255 });
}

void CHRTView_Status(ViewStatus* status) {
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    snprintf(status->info, sizeof(status->info), "%u characters, %u without a model, %u not placed, %u models%s",
        g_view.characterCount, g_view.characterCount - g_view.modelledCount, g_view.unpositionedCount, g_view.modelCount,
        g_options.hideSleeping ? ", sleeping hidden" : "");
    snprintf(status->hint, sizeof(status->hint), "%s, click a label: select", FreeCamera_Help(&g_view.camera));
}

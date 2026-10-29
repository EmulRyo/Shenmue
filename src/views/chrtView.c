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
#include "formats/text/chrt.h"
#include "views/mt5Render.h"
#include "views/orbitCamera.h"
#include "views/chrtView.h"

#define CHRTVIEW_FOVY 45.0f
#define CHRTVIEW_FRAME_RATE 15.0f
#define CHRTVIEW_PATH_SIZE 1024
#define CHRTVIEW_OFF_SCENE 999.0f // Coordinate of the positions off the scene

typedef struct {
    char            path[CHRTVIEW_PATH_SIZE];
    bool            loaded;
    MT5Render_Model model;
} CHRTView_Model;

typedef struct {
    char            id[5];
    const char*     image;  // Name given by the Image command, NULL if none
    int32_t         model;  // Index in the model list, -1 if the model can't be loaded
    bool            sleep;  // SLEEP flag
    bool            positioned; // Has a Position in the scene, the others are arranged in rows (see ArrangeUnpositioned)
    Vector3         position;   // From the script, (0, 0, 0) if none
    Vector3         angle;
    Matrix          transform;
    Vector3         center; // Bounding sphere of the model in the scene
    float           radius;
} CHRTView_Character;

typedef struct {
    bool                open;
    char                filename[256];
    CHRT_Script         script;
    MT5Render           render;
    uint32_t            modelCount;
    CHRTView_Model*     models;
    uint32_t            characterCount;
    CHRTView_Character* characters;
    uint32_t            modelledCount;     // Characters with a model
    uint32_t            unpositionedCount; // Characters with a model and without a Position command
    Vector3             center;      // Bounding sphere of the scene
    float               radius;
    OrbitCamera         camera;
    int32_t             selected;    // Character the camera looks at, -1 for the whole scene
    bool                labels;
    bool                hideSleeping;
    bool                wireframe;
    bool                noCulling;   // Draw the back faces too
    uint32_t            frame;       // Animation frame of the animated models
    float               frameTime;
} CHRTView_State;

static CHRTView_State g_view;

// Last string argument, NULL if none
static const char* StringArg(const CHRT_Command* command) {
    for (uint32_t i = command->argCount; i > 0; i--) {
        if (command->args[i - 1].type == CHRT_ARG_STRING)
            return command->args[i - 1].string;
    }
    return NULL;
}

// Model file of an Image name: the file of the DefImage with that name, e.g. DefImage("HOS_L") { Image(1, "HOS_L.MT5") },
// or the name itself (some names are defined in other scripts, and they are usually the name of the model file)
static const char* ImageFile(const char* name) {
    const CHRT_Script* script = &g_view.script;
    for (uint32_t c = 0; c < script->commandCount; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != -1 || !CHRT_IsCommand(command, "DefImage") || command->argCount == 0 ||
            command->args[0].type != CHRT_ARG_STRING || !CHRT_EqualsIgnoreCase(command->args[0].string, name))
            continue;
        for (uint32_t i = c + 1; i < script->commandCount && script->commands[i].parent == (int32_t)c; i++) {
            const char* file = StringArg(&script->commands[i]);
            if (CHRT_IsCommand(&script->commands[i], "Image") && file != NULL)
                return file;
        }
    }
    return name;
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

// Reads the Position and Angle commands of a block. Returns true if it has a Position in the scene: positions with a
// coordinate of 1000 or more, e.g. (0, 0, -1000) or (-1300, 0, 1000), are off the scene, where the characters wait to
// be placed by other scripts
static bool ReadPlacement(uint32_t block, float position[3], float angle[3]) {
    const CHRT_Script* script = &g_view.script;
    bool hasPosition = false;
    for (uint32_t c = block + 1; c < script->commandCount && script->commands[c].depth > script->commands[block].depth; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != (int32_t)block || command->argCount == 0 || command->args[0].type != CHRT_ARG_FLOAT)
            continue;
        if (CHRT_IsCommand(command, "Position") && command->argCount == 3) {
            hasPosition = true;
            for (int i = 0; i < 3; i++) {
                position[i] = command->args[i].f;
                hasPosition &= fabsf(position[i]) < CHRTVIEW_OFF_SCENE;
            }
        }
        else if (CHRT_IsCommand(command, "Angle") && command->argCount == 1) // Only the rotation around the vertical axis
            angle[1] = command->args[0].f;
        else if (CHRT_IsCommand(command, "Angle") && command->argCount == 3) {
            for (int i = 0; i < 3; i++)
                angle[i] = command->args[i].f;
        }
    }
    return hasPosition;
}

// Position and angle of a character: its last Position and Angle commands (the first Position is usually off the
// scene), or those of its first Entry if it has none
static void PlaceCharacter(uint32_t index, CHRTView_Character* character) {
    const CHRT_Script* script = &g_view.script;
    float position[3] = { 0 }, angle[3] = { 0 };
    character->positioned = ReadPlacement(index, position, angle);
    for (uint32_t c = index + 1; !character->positioned && c < script->commandCount && script->commands[c].depth > script->commands[index].depth; c++) {
        if (script->commands[c].parent == (int32_t)index && CHRT_IsCommand(&script->commands[c], "Entry")) {
            character->positioned = ReadPlacement(c, position, angle);
            break;
        }
    }
    character->position = (Vector3) { position[0], position[1], position[2] };
    character->angle = (Vector3) { angle[0], angle[1], angle[2] };
    if (!character->positioned)
        position[0] = position[1] = position[2] = 0.0f;

    // As the nodes of the models: rotation around X, Y and Z, and translation
    Matrix matrix = MatrixRotateX(angle[0] * DEG2RAD);
    matrix = MatrixMultiply(matrix, MatrixRotateY(angle[1] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateZ(angle[2] * DEG2RAD));
    character->transform = MatrixMultiply(matrix, MatrixTranslate(position[0], position[1], position[2]));
}

static void LoadCharacter(uint32_t index, const char* folder) {
    const CHRT_Script* script = &g_view.script;
    CHRTView_Character* character = &g_view.characters[g_view.characterCount++];
    const CHRT_Command* command = &script->commands[index];
    snprintf(character->id, sizeof(character->id), "%s", command->argCount > 0 && command->args[0].type == CHRT_ARG_LITERAL ? command->args[0].literal : "?");
    character->model = -1;

    for (uint32_t c = index + 1; c < script->commandCount && script->commands[c].parent == (int32_t)index; c++) {
        // Children of the blocks of the character (Entry, animated Image) are skipped
        const CHRT_Command* child = &script->commands[c];
        if (CHRT_IsCommand(child, "Image") && character->image == NULL && child->argCount > 0 && child->args[0].type == CHRT_ARG_STRING)
            character->image = child->args[0].string;
        else if (CHRT_IsCommand(child, "Flags")) {
            for (uint32_t i = 0; i < child->argCount; i++)
                character->sleep |= child->args[i].type == CHRT_ARG_STRING && CHRT_EqualsIgnoreCase(child->args[i].string, "SLEEP");
        }
        while (c + 1 < script->commandCount && script->commands[c + 1].depth > child->depth)
            c++;
    }

    PlaceCharacter(index, character);
    char path[CHRTVIEW_PATH_SIZE];
    if (character->image != NULL && ModelPath(folder, ImageFile(character->image), path, sizeof(path)))
        character->model = LoadCharacterModel(path);
    if (character->model < 0) {
        printf("[CHRTView_Open]: No model for %s (%s)\n", character->id, character->image != NULL ? character->image : "no image");
        return;
    }

    const MT5Mesh* mesh = &g_view.models[character->model].model.mesh;
    character->center = Vector3Transform(mesh->center, character->transform);
    character->radius = mesh->radius;
    g_view.modelledCount++;
    g_view.unpositionedCount += !character->positioned;
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
        if (character->positioned) {
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
        if (character->model < 0 || character->positioned)
            continue;
        float size = 2.0f * character->radius;
        if (x > 0.0f && x + size > rowWidth) {
            x = 0.0f;
            z += rowDepth + gap;
            rowDepth = 0.0f;
        }
        Vector3 offset = { startX + x + character->radius - character->center.x, 0.0f, startZ + z + character->radius - character->center.z };
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

static void ResetCamera(void) {
    OrbitCamera_Reset(&g_view.camera, g_view.center, (Vector3) { 0.0f, 0.0f, 1.0f }, g_view.radius, CHRTVIEW_FOVY);
    g_view.selected = -1;
}

// Looks at a character from the current direction
static void Select(int32_t index) {
    const CHRTView_Character* character = &g_view.characters[index];
    g_view.selected = index;
    g_view.camera.target = character->center;
    g_view.camera.distance = 1.5f * character->radius / sinf(CHRTVIEW_FOVY * 0.5f * DEG2RAD);
}

void CHRTView_Close(void) {
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (g_view.models[i].loaded)
            MT5Render_FreeModel(&g_view.models[i].model);
    }
    free(g_view.models);
    free(g_view.characters);
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
    snprintf(g_view.filename, sizeof(g_view.filename), "%s", GetFileName(filepath));
    uint32_t count = 0;
    for (uint32_t c = 0; c < script.commandCount; c++)
        count += script.commands[c].parent == -1 && CHRT_IsCommand(&script.commands[c], "Character");
    g_view.characters = calloc(count + 1, sizeof(CHRTView_Character));
    g_view.models = calloc(count + 1, sizeof(CHRTView_Model));
    if (g_view.characters == NULL || g_view.models == NULL) {
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

    ArrangeUnpositioned();
    ComputeBounds();
    ResetCamera();
    g_view.labels = true;
    g_view.open = true;
    return true;
}

bool CHRTView_IsOpen(void) {
    return g_view.open;
}

static bool IsVisible(const CHRTView_Character* character) {
    return character->model >= 0 && !(g_view.hideSleeping && character->sleep);
}

// Selects the next or previous visible character
static void SelectNext(int direction) {
    int32_t count = (int32_t)g_view.characterCount;
    int32_t index = g_view.selected >= 0 ? g_view.selected : (direction > 0 ? -1 : 0);
    for (int32_t i = 0; i < count; i++) {
        index = ((index + direction) % count + count) % count;
        if (IsVisible(&g_view.characters[index])) {
            Select(index);
            return;
        }
    }
}

static void UpdateInput(Rectangle area) {
    OrbitCamera_Update(&g_view.camera, area, g_view.radius * 0.001f);
    if (IsKeyPressed(KEY_R))
        ResetCamera();
    if (IsKeyPressed(KEY_TAB) || IsKeyPressedRepeat(KEY_TAB))
        SelectNext(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? -1 : 1);
    if (IsKeyPressed(KEY_L))
        g_view.labels = !g_view.labels;
    if (IsKeyPressed(KEY_S))
        g_view.hideSleeping = !g_view.hideSleeping;
    if (IsKeyPressed(KEY_W))
        g_view.wireframe = !g_view.wireframe;
    if (IsKeyPressed(KEY_C))
        g_view.noCulling = !g_view.noCulling;

    g_view.frameTime += fminf(GetFrameTime(), 0.25f); // Without skipping frames after a long one (loading)
    for (; g_view.frameTime >= 1.0f / CHRTVIEW_FRAME_RATE; g_view.frameTime -= 1.0f / CHRTVIEW_FRAME_RATE)
        g_view.frame++;
}

static uint32_t ModelFrame(const MT5Render_Model* model) {
    return model->mesh.frameCount > 0 ? g_view.frame % model->mesh.frameCount : 0;
}

static Camera3D g_sortCamera;

static int CompareDistance(const void* a, const void* b) {
    const CHRTView_Character* characterA = &g_view.characters[*(const uint32_t*)a];
    const CHRTView_Character* characterB = &g_view.characters[*(const uint32_t*)b];
    float distanceA = Vector3DistanceSqr(characterA->center, g_sortCamera.position);
    float distanceB = Vector3DistanceSqr(characterB->center, g_sortCamera.position);
    return distanceA < distanceB ? 1 : distanceA > distanceB ? -1 : 0; // Farthest first
}

// The opaque part of all the models, then the translucent part from the farthest model to the nearest one
static void DrawCharacters(Camera3D camera) {
    uint32_t* order = malloc((g_view.characterCount + 1) * sizeof(uint32_t));
    if (order == NULL)
        return;
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        if (!IsVisible(character))
            continue;
        MT5Render_Model* model = &g_view.models[character->model].model;
        MT5Render_DrawOpaque(&g_view.render, model, character->transform, ModelFrame(model));
        order[count++] = i;
    }

    g_sortCamera = camera;
    qsort(order, count, sizeof(uint32_t), CompareDistance);
    for (uint32_t i = 0; i < count; i++) {
        const CHRTView_Character* character = &g_view.characters[order[i]];
        MT5Render_Model* model = &g_view.models[character->model].model;
        MT5Render_DrawTranslucent(&g_view.render, model, character->transform, ModelFrame(model), camera);
    }
    free(order);
}

// ID of the characters, over the top of their model. A click on a label selects the character
static void DrawLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    int32_t clicked = -1;
    for (uint32_t i = 0; i < g_view.characterCount; i++) {
        const CHRTView_Character* character = &g_view.characters[i];
        if (!IsVisible(character))
            continue;
        Vector3 top = Vector3Add(character->center, (Vector3) { 0.0f, character->radius, 0.0f });
        if (Vector3DotProduct(Vector3Subtract(top, camera.position), forward) <= 0.0f)
            continue;
        Vector2 point = GetWorldToScreenEx(top, camera, (int)area.width, (int)area.height);
        point = (Vector2) { area.x + point.x, area.y + point.y };
        if (!CheckCollisionPointRec(point, area))
            continue;

        const char* text = character->sleep ? TextFormat("%s (sleep)", character->id) : character->id;
        Vector2 size = MeasureTextEx(font, text, fontSize, 1.0f);
        Rectangle box = { point.x - size.x * 0.5f - 3.0f, point.y - fontSize - 1.0f, size.x + 6.0f, fontSize + 2.0f };
        DrawRectangleRec(box, Fade(BLACK, 0.6f));
        Color color = (int32_t)i == g_view.selected ? YELLOW : character->sleep ? GRAY : LIGHTGRAY;
        DrawTextEx(font, text, (Vector2) { box.x + 3.0f, box.y + 1.0f }, fontSize, 1.0f, color);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), box))
            clicked = (int32_t)i; // The last one drawn, on top of the others
    }
    if (clicked >= 0)
        Select(clicked);
}

// Image, model file, position and angle of the selected character
static const char* SelectedText(void) {
    const CHRTView_Character* character = &g_view.characters[g_view.selected];
    const char* position = character->positioned ?
        TextFormat("position (%g, %g, %g)", character->position.x, character->position.y, character->position.z) :
        Vector3Equals(character->position, Vector3Zero()) ? "no position" :
        TextFormat("off the scene (%g, %g, %g)", character->position.x, character->position.y, character->position.z);
    return TextFormat("%s: image \"%s\" (%s), %s, angle (%g, %g, %g)%s", character->id, character->image,
        GetFileName(g_view.models[character->model].path), position, character->angle.x, character->angle.y,
        character->angle.z, character->sleep ? ", sleep" : "");
}

void CHRTView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    UpdateInput(area);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    double distance = g_view.camera.distance;
    Camera3D camera = OrbitCamera_Begin3D(&g_view.camera, area, distance * 0.01, distance + g_view.radius * 4.0);
    MT5Render_SetCamera(&g_view.render, camera);
    // The models have explicit back faces where both sides are seen (see MT5View_Draw)
    if (g_view.noCulling)
        rlDisableBackfaceCulling();
    if (g_view.wireframe)
        rlEnableWireMode();
    DrawCharacters(camera);
    rlDisableWireMode();
    rlEnableBackfaceCulling();
    OrbitCamera_End3D();

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    if (g_view.labels)
        DrawLabels(camera, area, font, fontSize);

    // TextFormat has 4 buffers: 1 for the counts, 2 for the selected character
    const char* lines[4] = {
        g_view.filename,
        TextFormat("%u characters, %u with a model (%u without a position in the scene, arranged in rows), %u models%s", g_view.characterCount,
            g_view.modelledCount, g_view.unpositionedCount, g_view.modelCount, g_view.hideSleeping ? ", sleeping characters hidden" : ""),
        "Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset   Tab or click on a label: select   L: labels   S: sleeping   W: wireframe   C: back faces",
        g_view.selected >= 0 ? SelectedText() : NULL,
    };
    int lineCount = g_view.selected >= 0 ? 4 : 3;
    for (int i = 0; i < lineCount; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + 10.0f, area.y + 10.0f + i * fontSize * 1.2f }, fontSize, 1.0f, LIGHTGRAY);
}

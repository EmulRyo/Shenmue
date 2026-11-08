#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <nfd.h>
#include "raygui.h"
#include "core/file.h"
#include "render/mt5Export.h"
#include "render/mt5Render.h"
#include "render/freeCamera.h"
#include "render/scene3D.h"
#include "ui/toolbar.h"
#include "views/mt5View.h"

#define MT5VIEW_FOVY 45.0f
#define MT5VIEW_FRAME_RATE 15.0f

typedef struct {
    bool            open;
    char            path[1024];  // Of the open file
    MT5Render       render;
    MT5Render_Model model;
    FreeCamera     camera;
    bool            fit;        // The camera is fitted to the model in the next draw, when the size of the view is known
    uint32_t        frame;
    float           frameTime;  // Time since the current frame was shown
    bool            paused;
} MT5View_State;

// Kept when another model is opened
typedef struct {
    bool            wireframe;
    bool            noCulling;  // Draw the back faces too
    bool            fly;
} MT5View_Options;

static MT5View_State g_view;
static MT5View_Options g_options;

static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, g_view.model.mesh.center, g_view.model.mesh.front, g_view.model.mesh.radius, MT5VIEW_FOVY);
    g_view.fit = true;
}

static void FitCamera(Rectangle area) {
    g_view.fit = false;
    MT5Render_Instance instance = { &g_view.model, MatrixIdentity(), g_view.model.mesh.center, 0 };
    uint32_t count;
    Vector3* points = MT5Render_InstanceVertices(&instance, 1, 0, &count);
    if (points == NULL)
        return;
    FreeCamera_Fit(&g_view.camera, points, count, area.width / area.height);
    free(points);
}

void MT5View_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    MT5Render_FreeModel(&g_view.model);
    MT5Render_Free(&g_view.render);
    memset(&g_view, 0, sizeof(g_view));
}

bool MT5View_Open(const char* filepath) {
    MT5Render_Model model;
    if (!MT5Render_LoadModel(filepath, &model))
        return false;

    MT5View_Close();
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.model = model;
    MT5Render_Init(&g_view.render);
    if (model.mesh.hiddenNodes > 0)
        printf("[MT5View_Open]: %u alternative meshes, CLOSE signs and invisible meshes are not shown\n", model.mesh.hiddenNodes);
    if (model.mesh.skippedTriangles > 0)
        printf("[MT5View_Open]: %u triangles with unresolved vertex indices are not shown\n", model.mesh.skippedTriangles);

    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.open = true;
    return true;
}

bool MT5View_IsOpen(void) {
    return g_view.open;
}

static void StepFrame(int direction) {
    uint32_t frameCount = g_view.model.mesh.frameCount;
    g_view.paused = true;
    g_view.frame = (g_view.frame + frameCount + direction) % frameCount;
}

void MT5View_Toolbar(void) {
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the camera", KEY_R))
        ResetCamera();
    bool fly = g_view.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the camera", KEY_F, &fly))
        FreeCamera_SetFly(&g_view.camera, fly);
    Toolbar_Separator();
    Toolbar_Toggle(ICON_GRID, "Wireframe", "Draw the edges of the triangles", KEY_Z, &g_options.wireframe);
    Toolbar_Toggle(ICON_CUBE_FACE_BACK, "Back faces", "Draw the back faces", KEY_B, &g_options.noCulling);
    if (g_view.model.mesh.frameCount == 0)
        return;
    Toolbar_Separator();
    if (Toolbar_Button(g_view.paused ? ICON_PLAYER_PLAY : ICON_PLAYER_PAUSE, NULL, "Play or pause the animation", KEY_SPACE))
        g_view.paused = !g_view.paused;
    if (Toolbar_Button(ICON_PLAYER_PREVIOUS, NULL, "First frame", KEY_HOME)) {
        g_view.frame = 0;
        g_view.frameTime = 0.0f;
    }
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Previous frame", TOOLBAR_REPEAT | KEY_LEFT))
        StepFrame(-1);
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Next frame", TOOLBAR_REPEAT | KEY_RIGHT))
        StepFrame(1);
}

static void UpdateInput(Rectangle area) {
    FreeCamera_Update(&g_view.camera, area, g_view.model.mesh.radius * 0.01f);
    uint32_t frameCount = g_view.model.mesh.frameCount;
    if (frameCount > 0 && !g_view.paused) {
        g_view.frameTime += Scene3D_FrameTime();
        for (; g_view.frameTime >= 1.0f / MT5VIEW_FRAME_RATE; g_view.frameTime -= 1.0f / MT5VIEW_FRAME_RATE)
            g_view.frame = (g_view.frame + 1) % frameCount;
    }
}

void MT5View_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    UpdateInput(area);
    if (g_view.fit)
        FitCamera(area);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    Camera3D camera = FreeCamera_Begin3D(&g_view.camera, area);

    // The faces are counter-clockwise, and the models have explicit back faces where both sides are seen (e.g. the
    // posters): the culling mode of the polygon parameters is always "small polygons", so the game culls them itself
    if (g_options.noCulling)
        rlDisableBackfaceCulling();
    if (g_options.wireframe)
        rlEnableWireMode();
    MT5Render_Instance instance = { &g_view.model, MatrixIdentity(), g_view.model.mesh.center, g_view.frame };
    MT5Render_DrawInstances(&g_view.render, &instance, 1, camera);
    rlDisableWireMode();
    rlEnableBackfaceCulling();

    FreeCamera_End3D();
}

void MT5View_ExportDialog(void) {
    static char lastFolder[1024];
    static bool lastOBJ;
    if (!g_view.open)
        return;

    // The name of the model, with the frame of the animated ones, and the format of the last export
    char name[256];
    File_GetBaseName(g_view.path, name, sizeof(name));
    if (name[0] == '\0')
        snprintf(name, sizeof(name), "model");
    if (g_view.model.mesh.frameCount > 0) {
        size_t length = strlen(name);
        snprintf(name + length, sizeof(name) - length, "_frame%u", g_view.frame + 1);
    }
    File_SanitizeName(name);
    const nfdu8filteritem_t glb = { "glTF binary", "glb" }, obj = { "Wavefront OBJ", "obj" };
    nfdu8filteritem_t filters[2] = { lastOBJ ? obj : glb, lastOBJ ? glb : obj };
    strncat(name, lastOBJ ? ".obj" : ".glb", sizeof(name) - strlen(name) - 1);

    // It opens in the folder of the last export, or in that of the model
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", lastFolder[0] != '\0' ? lastFolder : GetDirectoryPath(g_view.path));

    nfdu8char_t* outPath = NULL;
    nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 2, folder[0] != '\0' ? folder : NULL, name);
    if (result == NFD_OKAY) {
        // A name typed with another extension is exported in the format of the last export
        char path[1100];
        bool known = IsFileExtension(outPath, ".glb") || IsFileExtension(outPath, ".obj");
        snprintf(path, sizeof(path), "%s%s", outPath, known ? "" : (lastOBJ ? ".obj" : ".glb"));
        if (MT5Export_Save(&g_view.model, g_view.frame, NULL, path)) {
            snprintf(lastFolder, sizeof(lastFolder), "%s", GetDirectoryPath(path));
            lastOBJ = IsFileExtension(path, ".obj");
            printf("Exported the model to %s\n", path);
        }
        else
            printf("Failed to export the model to %s\n", path);
        NFD_FreePathU8(outPath);
    }
    else if (result == NFD_ERROR)
        printf("Error: %s\n", NFD_GetError());
}

void MT5View_Status(ViewStatus* status) {
    const MT5Render_Model* model = &g_view.model;
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    int length = snprintf(status->info, sizeof(status->info), "%u nodes, %u triangles, %u/%u textures", model->nodeCount,
        model->mesh.triangleCount, model->loadedTextures, model->textureCount);
    if (model->mesh.frameCount > 0)
        snprintf(status->info + length, sizeof(status->info) - length, ", frame %u/%u", g_view.frame + 1, model->mesh.frameCount);
    snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_view.camera));
}

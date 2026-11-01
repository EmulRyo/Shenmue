#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "views/mt5Render.h"
#include "views/freeCamera.h"
#include "views/scene3D.h"
#include "views/mt5View.h"

#define MT5VIEW_FOVY 45.0f
#define MT5VIEW_FRAME_RATE 15.0f

typedef struct {
    bool            open;
    char            filename[256];
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
    snprintf(g_view.filename, sizeof(g_view.filename), "%s", GetFileName(filepath));
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

static void UpdateInput(Rectangle area) {
    FreeCamera_Update(&g_view.camera, area, g_view.model.mesh.radius * 0.01f);
    if (IsKeyPressed(KEY_R))
        ResetCamera();
    if (IsKeyPressed(KEY_F))
        FreeCamera_SetFly(&g_view.camera, !g_view.camera.fly);
    // The keys of the flight move the camera
    bool fly = g_view.camera.fly;
    if (IsKeyPressed(KEY_W) && !fly)
        g_options.wireframe = !g_options.wireframe;
    if (IsKeyPressed(KEY_C))
        g_options.noCulling = !g_options.noCulling;

    uint32_t frameCount = g_view.model.mesh.frameCount;
    if (frameCount > 0) {
        if (IsKeyPressed(KEY_SPACE))
            g_view.paused = !g_view.paused;
        if (IsKeyPressed(KEY_RIGHT) && !fly) {
            g_view.paused = true;
            g_view.frame = (g_view.frame + 1) % frameCount;
        }
        if (IsKeyPressed(KEY_LEFT) && !fly) {
            g_view.paused = true;
            g_view.frame = (g_view.frame + frameCount - 1) % frameCount;
        }
        if (!g_view.paused) {
            g_view.frameTime += Scene3D_FrameTime();
            for (; g_view.frameTime >= 1.0f / MT5VIEW_FRAME_RATE; g_view.frameTime -= 1.0f / MT5VIEW_FRAME_RATE)
                g_view.frame = (g_view.frame + 1) % frameCount;
        }
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

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const MT5Render_Model* model = &g_view.model;
    const char* lines[4] = {
        g_view.filename,
        TextFormat("%u nodes, %u triangles, %u/%u textures", model->nodeCount, model->mesh.triangleCount, model->loadedTextures, model->textureCount),
        TextFormat("%s   R: reset   %sC: back faces", FreeCamera_Help(&g_view.camera), g_view.camera.fly ? "" : "W: wireframe   "),
        TextFormat("Frame %u/%u   Space: play/pause%s", g_view.frame + 1, model->mesh.frameCount,
            g_view.camera.fly ? "" : "   Left/Right: previous/next frame"),
    };
    int lineCount = model->mesh.frameCount > 0 ? 4 : 3;
    for (int i = 0; i < lineCount; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + 10.0f, area.y + 10.0f + i * fontSize * 1.2f }, fontSize, 1.0f, LIGHTGRAY);
}

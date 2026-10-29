#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "views/mt5Render.h"
#include "views/orbitCamera.h"
#include "views/mt5View.h"

#define MT5VIEW_FOVY 45.0f
#define MT5VIEW_FRAME_RATE 15.0f

typedef struct {
    bool            open;
    char            filename[256];
    MT5Render       render;
    MT5Render_Model model;
    OrbitCamera     camera;
    bool            wireframe;
    bool            noCulling; // Draw the back faces too
    uint32_t        frame;
    float           frameTime;  // Time since the current frame was shown
    bool            paused;
} MT5View_State;

static MT5View_State g_view;

static void ResetCamera(void) {
    OrbitCamera_Reset(&g_view.camera, g_view.model.mesh.center, g_view.model.mesh.front, g_view.model.mesh.radius, MT5VIEW_FOVY);
}

void MT5View_Close(void) {
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
        printf("[MT5View_Open]: %u alternative meshes are not shown\n", model.mesh.hiddenNodes);
    if (model.mesh.skippedTriangles > 0)
        printf("[MT5View_Open]: %u triangles with unresolved vertex indices are not shown\n", model.mesh.skippedTriangles);

    ResetCamera();
    g_view.open = true;
    return true;
}

bool MT5View_IsOpen(void) {
    return g_view.open;
}

static void UpdateInput(Rectangle area) {
    OrbitCamera_Update(&g_view.camera, area, g_view.model.mesh.radius * 0.01f);
    if (IsKeyPressed(KEY_R))
        ResetCamera();
    if (IsKeyPressed(KEY_W))
        g_view.wireframe = !g_view.wireframe;
    if (IsKeyPressed(KEY_C))
        g_view.noCulling = !g_view.noCulling;

    uint32_t frameCount = g_view.model.mesh.frameCount;
    if (frameCount > 0) {
        if (IsKeyPressed(KEY_SPACE))
            g_view.paused = !g_view.paused;
        if (IsKeyPressed(KEY_RIGHT)) {
            g_view.paused = true;
            g_view.frame = (g_view.frame + 1) % frameCount;
        }
        if (IsKeyPressed(KEY_LEFT)) {
            g_view.paused = true;
            g_view.frame = (g_view.frame + frameCount - 1) % frameCount;
        }
        if (!g_view.paused) {
            g_view.frameTime += fminf(GetFrameTime(), 0.25f); // Without skipping frames after a long one (loading)
            for (; g_view.frameTime >= 1.0f / MT5VIEW_FRAME_RATE; g_view.frameTime -= 1.0f / MT5VIEW_FRAME_RATE)
                g_view.frame = (g_view.frame + 1) % frameCount;
        }
    }
}

void MT5View_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    UpdateInput(area);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    double distance = g_view.camera.distance;
    Camera3D camera = OrbitCamera_Begin3D(&g_view.camera, area, distance * 0.01, distance + g_view.model.mesh.radius * 4.0);

    MT5Render_SetCamera(&g_view.render, camera);
    // The faces are counter-clockwise, and the models have explicit back faces where both sides are seen (e.g. the
    // posters): the culling mode of the polygon parameters is always "small polygons", so the game culls them itself
    if (g_view.noCulling)
        rlDisableBackfaceCulling();
    if (g_view.wireframe)
        rlEnableWireMode();
    MT5Render_DrawOpaque(&g_view.render, &g_view.model, MatrixIdentity(), g_view.frame);
    MT5Render_DrawTranslucent(&g_view.render, &g_view.model, MatrixIdentity(), g_view.frame, camera);
    rlDisableWireMode();
    rlEnableBackfaceCulling();

    OrbitCamera_End3D();

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const MT5Render_Model* model = &g_view.model;
    const char* lines[4] = {
        g_view.filename,
        TextFormat("%u nodes, %u triangles, %u/%u textures", model->nodeCount, model->mesh.triangleCount, model->loadedTextures, model->textureCount),
        "Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset   W: wireframe   C: back faces",
        TextFormat("Frame %u/%u   Space: play/pause   Left/Right: previous/next frame", g_view.frame + 1, model->mesh.frameCount),
    };
    int lineCount = model->mesh.frameCount > 0 ? 4 : 3;
    for (int i = 0; i < lineCount; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + 10.0f, area.y + 10.0f + i * fontSize * 1.2f }, fontSize, 1.0f, LIGHTGRAY);
}

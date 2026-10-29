#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "views/orbitCamera.h"

void OrbitCamera_Reset(OrbitCamera* camera, Vector3 center, Vector3 front, float radius, float fovy) {
    camera->target = center;
    camera->yaw = atan2f(front.x, front.z);
    camera->pitch = 0.2f;
    camera->distance = radius / sinf(fovy * 0.5f * DEG2RAD);
    camera->fovy = fovy;
    camera->dragButton = -1;
}

Vector3 OrbitCamera_Position(const OrbitCamera* camera) {
    Vector3 direction = {
        cosf(camera->pitch) * sinf(camera->yaw),
        sinf(camera->pitch),
        cosf(camera->pitch) * cosf(camera->yaw),
    };
    return Vector3Add(camera->target, Vector3Scale(direction, camera->distance));
}

Camera3D OrbitCamera_Camera3D(const OrbitCamera* camera) {
    return (Camera3D) { OrbitCamera_Position(camera), camera->target, { 0.0f, 1.0f, 0.0f }, camera->fovy, CAMERA_PERSPECTIVE };
}

void OrbitCamera_Update(OrbitCamera* camera, Rectangle area, float minDistance) {
    bool inside = CheckCollisionPointRec(GetMousePosition(), area);
    if (camera->dragButton < 0 && inside) {
        const int buttons[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
        for (int i = 0; i < 3; i++) {
            if (IsMouseButtonPressed(buttons[i]))
                camera->dragButton = buttons[i];
        }
    }

    if (camera->dragButton >= 0) {
        Vector2 delta = GetMouseDelta();
        if (!IsMouseButtonDown(camera->dragButton))
            camera->dragButton = -1;
        else if (camera->dragButton == MOUSE_BUTTON_LEFT) {
            camera->yaw -= delta.x * 0.01f;
            camera->pitch = Clamp(camera->pitch + delta.y * 0.01f, -1.55f, 1.55f);
        }
        else {
            // Move the target with the mouse, in the plane of the screen
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, OrbitCamera_Position(camera)));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3) { 0.0f, 1.0f, 0.0f }));
            Vector3 up = Vector3CrossProduct(right, forward);
            float scale = 2.0f * camera->distance * tanf(camera->fovy * 0.5f * DEG2RAD) / area.height;
            camera->target = Vector3Add(camera->target, Vector3Scale(right, -delta.x * scale));
            camera->target = Vector3Add(camera->target, Vector3Scale(up, delta.y * scale));
        }
    }

    if (inside) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            camera->distance = fmaxf(camera->distance * powf(0.85f, wheel), minDistance);
    }
}

Camera3D OrbitCamera_Begin3D(const OrbitCamera* camera, Rectangle area, double nearPlane, double farPlane) {
    Camera3D camera3D = OrbitCamera_Camera3D(camera);
    BeginMode3D(camera3D);
    rlViewport((int)area.x, GetRenderHeight() - (int)(area.y + area.height), (int)area.width, (int)area.height);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    double top = nearPlane * tan(camera->fovy * 0.5 * DEG2RAD);
    double right = top * area.width / area.height;
    rlFrustum(-right, right, -top, top, nearPlane, farPlane);
    rlMatrixMode(RL_MODELVIEW);
    return camera3D;
}

void OrbitCamera_End3D(void) {
    EndMode3D();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}

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

// From the target to the camera
static Vector3 Direction(const OrbitCamera* camera) {
    return (Vector3) {
        cosf(camera->pitch) * sinf(camera->yaw),
        sinf(camera->pitch),
        cosf(camera->pitch) * cosf(camera->yaw),
    };
}

Vector3 OrbitCamera_Position(const OrbitCamera* camera) {
    return Vector3Add(camera->target, Vector3Scale(Direction(camera), camera->distance));
}

// Nearest distance at which the points are inside the view: a point at x, y from the target in the plane of the screen
// and f behind it is inside if |x| <= (distance + f) * tanX and |y| <= (distance + f) * tanY
static float FitDistance(const OrbitCamera* camera, const Vector3* points, uint32_t count, Vector3 right, Vector3 up,
    Vector3 forward, float tanX, float tanY) {
    float distance = 0.01f;
    for (uint32_t i = 0; i < count; i++) {
        Vector3 offset = Vector3Subtract(points[i], camera->target);
        float x = fabsf(Vector3DotProduct(offset, right)) / tanX;
        float y = fabsf(Vector3DotProduct(offset, up)) / tanY;
        distance = fmaxf(distance, fmaxf(x, y) - Vector3DotProduct(offset, forward));
    }
    return distance;
}

void OrbitCamera_Fit(OrbitCamera* camera, const Vector3* points, uint32_t count, float aspect) {
    if (count == 0)
        return;
    Vector3 forward = Vector3Negate(Direction(camera));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3) { 0.0f, 1.0f, 0.0f }));
    Vector3 up = Vector3CrossProduct(right, forward);
    float tanY = tanf(camera->fovy * 0.5f * DEG2RAD);
    float tanX = tanY * aspect;
    // The points are centered on the screen by moving the target, which changes the distance: a few iterations converge
    for (int iteration = 0; iteration < 4; iteration++) {
        float distance = FitDistance(camera, points, count, right, up, forward, tanX, tanY);
        Vector2 min = { INFINITY, INFINITY }, max = { -INFINITY, -INFINITY };
        for (uint32_t i = 0; i < count; i++) {
            Vector3 offset = Vector3Subtract(points[i], camera->target);
            float depth = fmaxf(distance + Vector3DotProduct(offset, forward), 1e-6f);
            Vector2 screen = { Vector3DotProduct(offset, right) / depth, Vector3DotProduct(offset, up) / depth };
            min = Vector2Min(min, screen);
            max = Vector2Max(max, screen);
        }
        Vector2 center = Vector2Scale(Vector2Add(min, max), 0.5f * distance);
        camera->target = Vector3Add(camera->target, Vector3Add(Vector3Scale(right, center.x), Vector3Scale(up, center.y)));
    }
    camera->distance = FitDistance(camera, points, count, right, up, forward, tanX, tanY);
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

void OrbitCamera_Viewport(Rectangle area) {
    // The area is in pixels of the screen, the viewport in pixels of the render (scaled by the DPI)
    Vector2 scale = GetWindowScaleDPI();
    int x = (int)roundf(area.x * scale.x);
    int y = (int)roundf(area.y * scale.y);
    int width = (int)roundf((area.x + area.width) * scale.x) - x;
    int height = (int)roundf((area.y + area.height) * scale.y) - y;
    rlViewport(x, GetRenderHeight() - (y + height), width, height);
}

Camera3D OrbitCamera_Begin3D(const OrbitCamera* camera, Rectangle area, double nearPlane, double farPlane) {
    Camera3D camera3D = OrbitCamera_Camera3D(camera);
    BeginMode3D(camera3D);
    OrbitCamera_Viewport(area);
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

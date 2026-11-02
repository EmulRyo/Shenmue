#include <math.h>
#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "render/freeCamera.h"

void FreeCamera_Reset(FreeCamera* camera, Vector3 center, Vector3 front, float radius, float fovy) {
    camera->target = center;
    camera->yaw = atan2f(front.x, front.z);
    camera->pitch = 0.2f;
    camera->distance = radius / sinf(fovy * 0.5f * DEG2RAD);
    camera->fovy = fovy;
    camera->center = center;
    camera->radius = radius;
    camera->fly = false;
    camera->speed = sqrtf(radius); // Slower than the size in the large scenes, where the details are small
    camera->dragButton = -1;
}

// From the target to the camera
static Vector3 Direction(const FreeCamera* camera) {
    return (Vector3) {
        cosf(camera->pitch) * sinf(camera->yaw),
        sinf(camera->pitch),
        cosf(camera->pitch) * cosf(camera->yaw),
    };
}

Vector3 FreeCamera_Position(const FreeCamera* camera) {
    return Vector3Add(camera->target, Vector3Scale(Direction(camera), camera->distance));
}

// Nearest distance at which the points are inside the view: a point at x, y from the target in the plane of the screen
// and f behind it is inside if |x| <= (distance + f) * tanX and |y| <= (distance + f) * tanY
static float FitDistance(const FreeCamera* camera, const Vector3* points, uint32_t count, Vector3 right, Vector3 up,
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

void FreeCamera_Fit(FreeCamera* camera, const Vector3* points, uint32_t count, float aspect) {
    if (count == 0)
        return;
    // Flying, the target is near the position and the points can be in front of the fitted position: from the center
    if (camera->fly)
        camera->target = camera->center;
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
    if (camera->fly)
        FreeCamera_SetFly(camera, true);
}

Camera3D FreeCamera_Camera3D(const FreeCamera* camera) {
    return (Camera3D) { FreeCamera_Position(camera), camera->target, { 0.0f, 1.0f, 0.0f }, camera->fovy, CAMERA_PERSPECTIVE };
}

// Moves the target along the view to the given distance, without moving the position
static void SetDistance(FreeCamera* camera, float distance) {
    Vector3 position = FreeCamera_Position(camera);
    camera->distance = distance;
    camera->target = Vector3Subtract(position, Vector3Scale(Direction(camera), distance));
}

void FreeCamera_LookFrom(FreeCamera* camera, Camera3D view) {
    Vector3 direction = Vector3Normalize(Vector3Subtract(view.position, view.target));
    camera->yaw = atan2f(direction.x, direction.z);
    camera->pitch = Clamp(asinf(direction.y), -1.55f, 1.55f);
    camera->target = view.target;
    camera->distance = Vector3Distance(view.position, view.target);
}

void FreeCamera_SetFly(FreeCamera* camera, bool fly) {
    // The target is where the orbit goes back to
    if (fly)
        SetDistance(camera, camera->radius * 0.1f);
    camera->fly = fly;
}

// Starts or ends a drag with any button pressed inside area. Returns the movement of the mouse while dragging
static bool Drag(FreeCamera* camera, Rectangle area, Vector2* delta) {
    if (camera->dragButton < 0 && CheckCollisionPointRec(GetMousePosition(), area)) {
        const int buttons[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
        for (int i = 0; i < 3; i++) {
            if (IsMouseButtonPressed(buttons[i]))
                camera->dragButton = buttons[i];
        }
    }
    if (camera->dragButton < 0)
        return false;
    if (!IsMouseButtonDown(camera->dragButton)) {
        camera->dragButton = -1;
        return false;
    }
    *delta = GetMouseDelta();
    return true;
}

static float Axis(int positive, int alternative, int negative, int alternativeNegative) {
    return (IsKeyDown(positive) || IsKeyDown(alternative) ? 1.0f : 0.0f) - (IsKeyDown(negative) || IsKeyDown(alternativeNegative) ? 1.0f : 0.0f);
}

static void UpdateFly(FreeCamera* camera, Rectangle area) {
    // Turns around the position: the target moves around it
    Vector2 delta;
    if (Drag(camera, area, &delta)) {
        Vector3 position = FreeCamera_Position(camera);
        camera->yaw -= delta.x * 0.005f;
        camera->pitch = Clamp(camera->pitch + delta.y * 0.005f, -1.55f, 1.55f);
        camera->target = Vector3Subtract(position, Vector3Scale(Direction(camera), camera->distance));
    }
    if (CheckCollisionPointRec(GetMousePosition(), area)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            camera->speed = Clamp(camera->speed * powf(1.25f, wheel), 0.001f, 10000.0f);
    }

    // Forward in the direction of the view, sideways and vertically in the horizontal plane
    Vector3 forward = Vector3Negate(Direction(camera));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3) { 0.0f, 1.0f, 0.0f }));
    Vector3 move = Vector3Scale(forward, Axis(KEY_W, KEY_UP, KEY_S, KEY_DOWN));
    move = Vector3Add(move, Vector3Scale(right, Axis(KEY_D, KEY_RIGHT, KEY_A, KEY_LEFT)));
    move.y += Axis(KEY_E, KEY_PAGE_UP, KEY_Q, KEY_PAGE_DOWN);
    if (Vector3LengthSqr(move) == 0.0f)
        return;
    float speed = camera->speed * (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? 4.0f : 1.0f);
    // Without jumping after a long frame (loading)
    camera->target = Vector3Add(camera->target, Vector3Scale(Vector3Normalize(move), speed * fminf(GetFrameTime(), 0.1f)));
}

void FreeCamera_Update(FreeCamera* camera, Rectangle area, float minDistance) {
    if (camera->fly) {
        UpdateFly(camera, area);
        return;
    }
    bool inside = CheckCollisionPointRec(GetMousePosition(), area);
    Vector2 delta;
    if (Drag(camera, area, &delta)) {
        if (camera->dragButton == MOUSE_BUTTON_LEFT) {
            camera->yaw -= delta.x * 0.01f;
            camera->pitch = Clamp(camera->pitch + delta.y * 0.01f, -1.55f, 1.55f);
        }
        else {
            // Move the target with the mouse, in the plane of the screen
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, FreeCamera_Position(camera)));
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

const char* FreeCamera_Help(const FreeCamera* camera) {
    static char help[256];
    if (camera->fly)
        snprintf(help, sizeof(help), "Fly: drag: look   W/A/S/D: move   Q/E: down/up   Shift: faster   Wheel: speed (%.3g/s)   F: orbit",
            camera->speed);
    else
        snprintf(help, sizeof(help), "Left drag: rotate   Right drag: pan   Wheel: zoom   F: fly");
    return help;
}

void FreeCamera_Viewport(Rectangle area) {
    // The area is in pixels of the screen, the viewport in pixels of the render (scaled by the DPI)
    Vector2 scale = GetWindowScaleDPI();
    int x = (int)roundf(area.x * scale.x);
    int y = (int)roundf(area.y * scale.y);
    int width = (int)roundf((area.x + area.width) * scale.x) - x;
    int height = (int)roundf((area.y + area.height) * scale.y) - y;
    rlViewport(x, GetRenderHeight() - (y + height), width, height);
}

void FreeCamera_BeginCamera3D(Camera3D camera, Rectangle area, double nearPlane, double farPlane) {
    BeginMode3D(camera);
    FreeCamera_Viewport(area);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    double top = nearPlane * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * area.width / area.height;
    rlFrustum(-right, right, -top, top, nearPlane, farPlane);
    rlMatrixMode(RL_MODELVIEW);
}

Camera3D FreeCamera_Begin3D(const FreeCamera* camera, Rectangle area) {
    double nearPlane = camera->distance * 0.01;
    double farPlane = camera->distance + camera->radius * 4.0;
    if (camera->fly) {
        // Near enough to go inside the scene, far enough to see all of it from outside
        nearPlane = fmin(camera->radius * 0.001, 0.05);
        farPlane = Vector3Distance(FreeCamera_Position(camera), camera->center) + camera->radius * 2.0;
    }
    Camera3D camera3D = FreeCamera_Camera3D(camera);
    FreeCamera_BeginCamera3D(camera3D, area, nearPlane, farPlane);
    return camera3D;
}

void FreeCamera_End3D(void) {
    EndMode3D();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}

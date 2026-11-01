#include <stdbool.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "formats/animation/auth.h"
#include "render/scene3D.h"

float Scene3D_FrameTime(void) {
    return fminf(GetFrameTime(), 0.25f);
}

static Vector3 CurvePoint(const AUTH_Curve* curves, float time) {
    return (Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
        AUTH_CurveValue(&curves[2], time, 0.0f) };
}

Camera3D Scene3D_CurveCamera(const AUTH_Curve curves[AUTH_CAMERA_CURVES], float time, float defaultFovy) {
    Vector3 position = CurvePoint(&curves[AUTH_CAMERA_X], time);
    Vector3 target = CurvePoint(&curves[AUTH_CAMERA_TARGET_X], time);
    if (Vector3Equals(position, target))
        target.z -= 1.0f;
    Vector3 forward = Vector3Normalize(Vector3Subtract(target, position));
    float roll = AUTH_CurveValue(&curves[AUTH_CAMERA_ROLL], time, 0.0f);
    Vector3 up = Vector3RotateByAxisAngle((Vector3) { 0.0f, 1.0f, 0.0f }, forward, roll * DEG2RAD);
    float fovy = AUTH_CurveValue(&curves[AUTH_CAMERA_FOV], time, defaultFovy);
    return (Camera3D) { position, target, up, fovy > 1.0f && fovy < 179.0f ? fovy : defaultFovy, CAMERA_PERSPECTIVE };
}

Rectangle Scene3D_Letterbox(Rectangle area, float aspect) {
    Rectangle screen;
    screen.width = fminf(area.width, area.height * aspect);
    screen.height = screen.width / aspect;
    screen.x = roundf(area.x + (area.width - screen.width) * 0.5f);
    screen.y = roundf(area.y + (area.height - screen.height) * 0.5f);
    DrawRectangleRec(area, BLACK);
    DrawRectangleRec(screen, (Color) { 45, 45, 48, 255 });
    return screen;
}

void Scene3D_DrawCameraGizmo(Camera3D camera, float depth, float aspect, bool up, Color color) {
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 screenUp = Vector3CrossProduct(right, forward);
    float halfHeight = depth * tanf(camera.fovy * 0.5f * DEG2RAD);
    float halfWidth = halfHeight * aspect;
    Vector3 center = Vector3Add(camera.position, Vector3Scale(forward, depth));
    Vector3 corners[4];
    for (int i = 0; i < 4; i++) {
        float sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
        float sy = i < 2 ? 1.0f : -1.0f;
        corners[i] = Vector3Add(center, Vector3Add(Vector3Scale(right, sx * halfWidth), Vector3Scale(screenUp, sy * halfHeight)));
    }
    for (int i = 0; i < 4; i++) {
        DrawLine3D(camera.position, corners[i], color);
        DrawLine3D(corners[i], corners[(i + 1) % 4], color);
    }
    if (up) {
        Vector3 top = Vector3Add(Vector3Scale(Vector3Add(corners[0], corners[1]), 0.5f), Vector3Scale(screenUp, halfHeight * 0.4f));
        DrawLine3D(corners[0], top, color);
        DrawLine3D(corners[1], top, color);
    }
}

bool Scene3D_ScreenPoint(Camera3D camera, Rectangle area, Vector3 position, Vector2* point) {
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    if (Vector3DotProduct(Vector3Subtract(position, camera.position), forward) <= 0.0f)
        return false;
    Vector2 screen = GetWorldToScreenEx(position, camera, (int)area.width, (int)area.height);
    *point = (Vector2) { area.x + screen.x, area.y + screen.y };
    return CheckCollisionPointRec(*point, area);
}

Rectangle Scene3D_DrawLabel(Vector2 point, const char* text, Font font, float fontSize, Color color) {
    Vector2 size = MeasureTextEx(font, text, fontSize, 1.0f);
    Rectangle box = { point.x - size.x * 0.5f - 3.0f, point.y - fontSize - 1.0f, size.x + 6.0f, fontSize + 2.0f };
    DrawRectangleRec(box, Fade(BLACK, 0.6f));
    DrawTextEx(font, text, (Vector2) { box.x + 3.0f, box.y + 1.0f }, fontSize, 1.0f, color);
    return box;
}

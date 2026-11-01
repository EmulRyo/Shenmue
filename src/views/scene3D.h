#ifndef __SCENE3D_H__
#define __SCENE3D_H__

#include <stdbool.h>
#include <raylib.h>
#include "formats/animation/auth.h"

// Helpers of the 3D views: the cameras of the scenes, the screen of the game and the labels over the scene

// Time to advance the animations: the time of the last frame, without skipping frames after a long one (loading)
float       Scene3D_FrameTime(void);
// Camera of the curves of an AUTH camera (position, target, roll and field of view) at a time, with defaultFovy when
// the field of view is missing or invalid
Camera3D    Scene3D_CurveCamera(const AUTH_Curve curves[AUTH_CAMERA_CURVES], float time, float defaultFovy);
// Draws the screen of the game, with the given aspect ratio, letterboxed in area. Returns the screen
Rectangle   Scene3D_Letterbox(Rectangle area, float aspect);
// Frustum of a camera to the given depth, for a screen with the given aspect ratio. With up, a triangle over its top
// edge shows the up direction
void        Scene3D_DrawCameraGizmo(Camera3D camera, float depth, float aspect, bool up, Color color);
// Point of the screen where a position of the scene drawn inside area is. Returns false if it is behind the camera or
// out of area
bool        Scene3D_ScreenPoint(Camera3D camera, Rectangle area, Vector3 position, Vector2* point);
// Draws a text over a dark box, centered on point with its bottom at point. Returns the box
Rectangle   Scene3D_DrawLabel(Vector2 point, const char* text, Font font, float fontSize, Color color);

#endif

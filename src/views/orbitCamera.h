#ifndef __ORBITCAMERA_H__
#define __ORBITCAMERA_H__

#include <stdint.h>
#include <raylib.h>

// Camera that orbits around a target, moved with the mouse: left drag rotates, right/middle drag pans, the wheel zooms

typedef struct {
    Vector3     target;
    float       yaw, pitch, distance;
    float       fovy;
    int         dragButton; // -1 if the mouse isn't dragging
} OrbitCamera;

// Looks at center from the direction front, at the distance that shows a sphere of the given radius
void        OrbitCamera_Reset(OrbitCamera* camera, Vector3 center, Vector3 front, float radius, float fovy);
// Moves the target in the plane of the screen and sets the distance so that the points fill the width or the height of
// a view with the given aspect ratio (width / height), seen from the current direction
void        OrbitCamera_Fit(OrbitCamera* camera, const Vector3* points, uint32_t count, float aspect);
Vector3     OrbitCamera_Position(const OrbitCamera* camera);
Camera3D    OrbitCamera_Camera3D(const OrbitCamera* camera);
// Handles the mouse inside area. The zoom doesn't get nearer than minDistance
void        OrbitCamera_Update(OrbitCamera* camera, Rectangle area, float minDistance);
// Draws only inside area, given in pixels of the screen
void        OrbitCamera_Viewport(Rectangle area);
// Begins drawing in 3D only inside area, with its aspect ratio. Returns the camera
Camera3D    OrbitCamera_Begin3D(const OrbitCamera* camera, Rectangle area, double nearPlane, double farPlane);
void        OrbitCamera_End3D(void);

#endif

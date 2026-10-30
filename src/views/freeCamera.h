#ifndef __FREECAMERA_H__
#define __FREECAMERA_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>

// Free camera of the 3D views, with two modes. It orbits around a target, moved with the mouse: left drag rotates,
// right/middle drag pans, the wheel zooms. Or it flies: a drag looks around from the position, W/A/S/D or the arrows
// move, Q/E or Page Up/Down move down and up, Shift moves faster and the wheel changes the speed. The views don't use
// those keys for anything else while it flies

typedef struct {
    Vector3     target;
    float       yaw, pitch, distance;
    float       fovy;
    Vector3     center;     // Of the scene, for the clipping planes
    float       radius;
    bool        fly;
    float       speed;      // Of the flight, units per second
    int         dragButton; // -1 if the mouse isn't dragging
} FreeCamera;

// Looks at the scene, a sphere at center, from the direction front at the distance that shows it. Ends the flight
void        FreeCamera_Reset(FreeCamera* camera, Vector3 center, Vector3 front, float radius, float fovy);
// Moves the target in the plane of the screen and sets the distance so that the points fill the width or the height of
// a view with the given aspect ratio (width / height), seen from the current direction
void        FreeCamera_Fit(FreeCamera* camera, const Vector3* points, uint32_t count, float aspect);
Vector3     FreeCamera_Position(const FreeCamera* camera);
Camera3D    FreeCamera_Camera3D(const FreeCamera* camera);
// Starts flying from the current position, or orbits again around a point in front of it
void        FreeCamera_SetFly(FreeCamera* camera, bool fly);
// Handles the mouse inside area, and the keys while it flies. The zoom doesn't get nearer than minDistance
void        FreeCamera_Update(FreeCamera* camera, Rectangle area, float minDistance);
// Controls of the current mode, with the speed of the flight, for the help of the views. Valid until the next call
const char* FreeCamera_Help(const FreeCamera* camera);
// Draws only inside area, given in pixels of the screen
void        FreeCamera_Viewport(Rectangle area);
// Begins drawing in 3D only inside area, with its aspect ratio and clipping planes around the scene. Returns the camera
Camera3D    FreeCamera_Begin3D(const FreeCamera* camera, Rectangle area);
void        FreeCamera_End3D(void);

#endif

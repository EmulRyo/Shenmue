#ifndef __BOUNDS_H__
#define __BOUNDS_H__

#include <math.h>
#include <stdbool.h>
#include <raylib.h>
#include <raymath.h>

// Axis-aligned box around the points of a scene, to place its camera

typedef struct {
    Vector3 min, max;
} Bounds;

// A box with no point: the first point added is the whole box
static inline Bounds Bounds_Empty(void) {
    return (Bounds) { { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
}

static inline bool Bounds_IsEmpty(const Bounds* bounds) {
    return bounds->min.x > bounds->max.x;
}

static inline void Bounds_AddPoint(Bounds* bounds, Vector3 point) {
    bounds->min = Vector3Min(bounds->min, point);
    bounds->max = Vector3Max(bounds->max, point);
}

// The box around a sphere
static inline void Bounds_AddSphere(Bounds* bounds, Vector3 center, float radius) {
    Vector3 extent = { radius, radius, radius };
    Bounds_AddPoint(bounds, Vector3Subtract(center, extent));
    Bounds_AddPoint(bounds, Vector3Add(center, extent));
}

static inline Vector3 Bounds_Center(const Bounds* bounds) {
    return Vector3Scale(Vector3Add(bounds->min, bounds->max), 0.5f);
}

// Radius of the sphere around the box, at least minRadius
static inline float Bounds_Radius(const Bounds* bounds, float minRadius) {
    return fmaxf(Vector3Distance(bounds->min, bounds->max) * 0.5f, minRadius);
}

#endif

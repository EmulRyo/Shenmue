#ifndef __FTBL_H__
#define __FTBL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FTBL: the skin of a detailed face (xxx_ftbl.bin next to xxx_f.mt5 in model/face): 25 face bones, and for each vertex
// of the mesh of the root of the face model the bones that move it and their weights. The bones only move the vertices:
// each vertex goes to its position plus the weighted offsets of its bones. The TALK files give the poses of the bones

#define FTBL_BONES  25

typedef struct {
    float       tip[3];     // A point on the face, moved by the poses
    int32_t     type;       // Always 2: a chain of two segments from the root to the tip
    float       root[3];    // Inside the head
    float       angles[3];  // Radians, always (pi/2, 0, 0)
    float       lengths[2]; // Of the two segments of the chain
} FTBL_Bone;

typedef struct {
    int32_t     talkSet;        // Index of the set of poses in a TALK chain (0 to 3)
    int32_t     eyeLimits[6];   // 0x10000 = 360 degrees: Y max and min of both eyes, Z max and min of the eyes 0x4D and 0x4E
    FTBL_Bone   bones[FTBL_BONES];
    uint32_t    vertexCount;    // Of the influence counts in the file
    uint32_t*   firsts;         // First influence of each vertex
    uint8_t*    counts;         // Number of influences of each vertex
    uint32_t    influenceCount;
    uint8_t*    bones8;         // Bone of each influence
    float*      weights;        // Weight of each influence
} FTBL_Skin;

// Loads an FTBL file. Returns false if it isn't valid
bool FTBL_Load(const char* filepath, FTBL_Skin* skin);
bool FTBL_LoadFromMemory(const uint8_t* data, size_t size, const char* name, FTBL_Skin* skin);
void FTBL_Free(FTBL_Skin* skin);
// The offsets of the first vertexCount vertices (3 floats each, in the space of the face) for the offsets of the bones
void FTBL_Deform(const FTBL_Skin* skin, const float boneOffsets[FTBL_BONES][3], uint32_t vertexCount, float* vertexOffsets);

#endif

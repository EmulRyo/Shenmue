#ifndef __HM_H__
#define __HM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HM: the skeleton and the skin of the detailed hands of a character (xxx_hm.bin next to xxx_tl.mt5 and xxx_tr.mt5 in
// model/hand). The skeleton has 71 nodes, listed depth first: each one is turned around its Z, Y and X axes after a
// translation from its parent, and has its world matrix at rest. Each vertex of the mesh of a hand is moved by up to 6
// nodes with weights. The skeleton is the one of the right hand, with the fingers along -Y: a vertex (x, y, z) of a
// hand model is the point (y, -x, z) of the skeleton, (y, -x, -z) for the left hand. The HMOT files pose the nodes

#define HM_NODES    71

typedef struct {
    uint8_t     joint;      // Index of the joint (0 to 70), 0 for the root
    uint8_t     children;   // Number of nodes under it: the following ones, depth first
    float       length;     // Translation along X from the parent (the root: translation in offset)
    float       angles[3];  // Radians around X, Y and Z at rest (the root: its translation)
    float       rest[16];   // World matrix at rest, by columns
} HM_Node;

typedef struct {
    HM_Node     nodes[HM_NODES];
    uint32_t    vertexCount;    // Of the influence counts in the file
    uint32_t*   firsts;         // First influence of each vertex
    uint8_t*    counts;         // Number of influences of each vertex
    uint32_t    influenceCount;
    uint8_t*    nodes8;         // Node of each influence
    float*      weights;        // Weight of each influence
} HM_Skin;

// Loads an HM file. Returns false if it isn't valid
bool HM_Load(const char* filepath, HM_Skin* skin);
bool HM_LoadFromMemory(const uint8_t* data, size_t size, const char* name, HM_Skin* skin);
void HM_Free(HM_Skin* skin);
// World matrices (by columns) of the nodes with the given angles in radians around X, Y and Z (NULL: at rest)
void HM_Pose(const HM_Skin* skin, const float (*angles)[3], float matrices[HM_NODES][16]);
// The offsets of the first vertexCount vertices of a hand model (positions: 3 floats each, in the space of the model)
// in a pose of the skeleton: each vertex moves by the weighted movements of its nodes from their rest
void HM_Deform(const HM_Skin* skin, const float matrices[HM_NODES][16], const float* positions, uint32_t vertexCount, bool left,
    float* offsets);

#endif

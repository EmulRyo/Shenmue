#ifndef __MT5MESH_H__
#define __MT5MESH_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/model/mt5.h"

// Meshes of an MT5/CHRM model in the pose of its node hierarchy, ready to draw with raylib. They keep the node of each
// vertex, so that they can be drawn in another pose of the nodes (the motions of the characters)

// Triangles that share texture, list type and texture wrapping, drawn with one mesh
typedef struct {
    uint16_t    texture;
    uint8_t     listType;
    uint8_t     uvFlags;
    int32_t     frame; // Animation frame of the triangles, -1 if they are always shown
    uint32_t    triangleCount;
    uint32_t    filled; // Triangles copied to the mesh while it is built
    float       depth;  // Mean depth of the triangles of a translucent group, to draw the groups from back to front
    Mesh        mesh;
    uint16_t*   nodes;      // Node of each vertex of the mesh
    float*      positions;  // Of each vertex in the space of its node
    float*      normals;
} MT5Mesh_Group;

// A node of the model, for the poses
typedef struct {
    int32_t     parent;     // -1 for the root
    uint32_t    id;
    Vector3     position;   // Relative to the parent
    Vector3     rotation;   // Degrees, around X, Y and Z in this order
    Vector3     scale;
} MT5Mesh_Node;

typedef struct {
    uint32_t        groupCount;
    MT5Mesh_Group*  groups;
    uint32_t        triangleCount;
    uint32_t        skippedTriangles; // Triangles with vertex indices that can't be resolved
    uint32_t        hiddenNodes;      // Alternative meshes, CLOSE signs and invisible meshes
    uint32_t        frameCount;       // Animation frames (see FindFrames), 0 if the model is not animated
    Vector3         front;            // Direction the model looks to
    Vector3         center;           // Bounding sphere of the model
    float           radius;
    uint32_t        nodeCount;
    MT5Mesh_Node*   nodes;
    Matrix*         pose;             // World matrix of each node in the pose of the meshes
    bool            posed;            // The meshes are in another pose than the one of the file
} MT5Mesh;

// Builds one mesh per group with the vertices in model space and uploads them. On error, result has to be freed too
bool MT5Mesh_Build(const MT5_Model* model, MT5Mesh* result);
void MT5Mesh_Free(MT5Mesh* meshes);
// Local matrix of a node of the file: scale, rotation around X, Y and Z, and translation, relative to the parent
Matrix MT5Mesh_NodeMatrix(const MT5Mesh_Node* node);
// Moves the vertices to the pose given by the world matrix of each node, and uploads them. NULL goes back to the pose of
// the file
void MT5Mesh_Pose(MT5Mesh* meshes, const Matrix* world);

#endif

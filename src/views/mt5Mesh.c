#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "formats/model/mt5.h"
#include "views/mt5Mesh.h"

#define MT5MESH_ARM_SPREAD 12.0f // Degrees

// Rotation around Y of a node in degrees. The feet (0x12, 0x17) of the bodies are rotated (45, -90, 45), but a few
// models (SG*_L.chrm, BGA_L.chrm, bga_l.mt5) have 0xFFFF (almost 0) instead of 0xC000 (-90) and the feet point sideways
static float NodeRotationY(const MT5_Node* node) {
    uint32_t id = node->id & 0xFFFF;
    bool foot = id == 0x12 || id == 0x17;
    if (foot && node->rotation[1] > 359.99f && fabsf(node->rotation[0] - 45.0f) < 2.0f && fabsf(node->rotation[2] - 45.0f) < 2.0f)
        return -90.0f;
    return node->rotation[1];
}

// Scale, rotation around X, Y and Z, and translation, relative to the parent. spread rotates the node around the Y axis
// of the parent (see ArmSpread)
static Matrix NodeMatrix(const MT5_Node* node, float spread) {
    Matrix matrix = MatrixScale(node->scale[0], node->scale[1], node->scale[2]);
    matrix = MatrixMultiply(matrix, MatrixRotateX(node->rotation[0] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateY(NodeRotationY(node) * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateZ(node->rotation[2] * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateY(spread * DEG2RAD));
    return MatrixMultiply(matrix, MatrixTranslate(node->position[0], node->position[1], node->position[2]));
}

// In the pose of the files the arms of the bodies hang close to the body and the sleeves go through it (the motions
// move them in the game). The upper arms (5 and 0xA, under the shoulders 4 and 9) are opened a few degrees around the
// front-back axis of the shoulders; their side is the sign of their Z position
static float ArmSpread(const MT5_Model* model, uint32_t node) {
    int32_t parent = model->nodes[node].parent;
    if (parent < 0)
        return 0.0f;
    uint32_t id = model->nodes[node].id & 0xFFFF;
    uint32_t parentID = model->nodes[parent].id & 0xFFFF;
    if ((id == 0x05 && parentID == 0x04) || (id == 0x0A && parentID == 0x09))
        return model->nodes[node].position[2] >= 0.0f ? MT5MESH_ARM_SPREAD : -MT5MESH_ARM_SPREAD;
    return 0.0f;
}

static Vector3 TransformNormal(const float normal[3], Matrix normalMatrix) {
    Vector3 result = {
        normal[0] * normalMatrix.m0 + normal[1] * normalMatrix.m4 + normal[2] * normalMatrix.m8,
        normal[0] * normalMatrix.m1 + normal[1] * normalMatrix.m5 + normal[2] * normalMatrix.m9,
        normal[0] * normalMatrix.m2 + normal[1] * normalMatrix.m6 + normal[2] * normalMatrix.m10,
    };
    return Vector3Normalize(result);
}

// Node and vertex of a corner: negative indices point to the vertices of the parent mesh
static bool ResolveVertex(const MT5_Model* model, uint32_t node, int32_t vertex, uint32_t* outNode, uint32_t* outVertex) {
    if (vertex < 0) {
        int32_t parent = MT5_ParentMeshNode(model, model->nodes[node].parent);
        if (parent < 0)
            return false;
        node = (uint32_t)parent;
        vertex += (int32_t)model->nodes[node].vertexCount;
        if (vertex < 0)
            return false;
    }
    if ((uint32_t)vertex >= model->nodes[node].vertexCount)
        return false;
    *outNode = node;
    *outVertex = (uint32_t)vertex;
    return true;
}

static bool IsDescendant(const MT5_Model* model, uint32_t node, uint32_t ancestor) {
    for (int32_t parent = model->nodes[node].parent; parent >= 0; parent = model->nodes[parent].parent) {
        if ((uint32_t)parent == ancestor)
            return true;
    }
    return false;
}

// Copy without mesh of a node of another branch that has one: the root of a branch of alternative meshes (e.g. the
// expressions of the face of a character), which are not placed on the node they replace. The nodes of the maps share
// the same ID (0xFFFF), the root included
static bool IsAlternativeRoot(const MT5_Model* model, uint32_t node) {
    if (model->nodes[node].hasMesh || model->nodes[node].parent < 0)
        return false;
    for (uint32_t i = 0; i < model->nodeCount; i++) {
        if (model->nodes[i].id == model->nodes[node].id && model->nodes[i].hasMesh && !IsDescendant(model, i, node))
            return true;
    }
    return false;
}

static bool IsTriangleValid(const MT5_Model* model, uint32_t node, const MT5_Triangle* triangle) {
    uint32_t owner, vertex;
    for (int c = 0; c < 3; c++) {
        if (!ResolveVertex(model, node, triangle->corners[c].vertex, &owner, &vertex))
            return false;
    }
    return true;
}

static MT5Mesh_Group* FindGroup(MT5Mesh* result, const MT5_Triangle* triangle, int32_t frame) {
    for (uint32_t i = 0; i < result->groupCount; i++) {
        const MT5Mesh_Group* group = &result->groups[i];
        if (group->texture == triangle->texture && group->listType == triangle->listType && group->uvFlags == triangle->uvFlags &&
            group->frame == frame)
            return &result->groups[i];
    }
    return NULL;
}

static bool AddGroup(MT5Mesh* result, const MT5_Triangle* triangle, int32_t frame) {
    MT5Mesh_Group* groups = realloc(result->groups, (result->groupCount + 1) * sizeof(MT5Mesh_Group));
    if (groups == NULL)
        return false;
    result->groups = groups;
    MT5Mesh_Group* group = &result->groups[result->groupCount++];
    memset(group, 0, sizeof(MT5Mesh_Group));
    group->texture = triangle->texture;
    group->listType = triangle->listType;
    group->uvFlags = triangle->uvFlags;
    group->frame = frame;
    return true;
}

static void AddCorner(Mesh* mesh, uint32_t index, const MT5_Corner* corner, Vector3 position, Vector3 normal) {
    memcpy(&mesh->vertices[index * 3], &position, sizeof(Vector3));
    memcpy(&mesh->normals[index * 3], &normal, sizeof(Vector3));
    mesh->texcoords[index * 2] = corner->u;
    mesh->texcoords[index * 2 + 1] = 1.0f - corner->v; // The rows of the decoded PVR textures are bottom to top
    mesh->colors[index * 4] = (corner->color >> 16) & 0xFF;
    mesh->colors[index * 4 + 1] = (corner->color >> 8) & 0xFF;
    mesh->colors[index * 4 + 2] = corner->color & 0xFF;
    mesh->colors[index * 4 + 3] = (corner->color >> 24) & 0xFF;
}

static bool HasNegativeIndices(const MT5_Node* node) {
    for (uint32_t t = 0; t < node->triangleCount; t++) {
        for (int c = 0; c < 3; c++) {
            if (node->triangles[t].corners[c].vertex < 0)
                return true;
        }
    }
    return false;
}

// The faces (_F) and hands (_TL, _TR) of the characters are separate models that replace the head (0xFFBD) or a hand
// (0xFFBE, 0xFFBF) of the body: their root has a mesh with negative vertex indices, which point to the mesh of the
// parent of that node in the body, and their vertices are in the space of that node. They are rotated as that node in
// the pose of the bodies, identified by the ID of the root: 3 for the faces, the lower arm (5-7, 0xA-0xC) for the hands.
// Returns false (and the identity) if the model is not a face or a hand
static bool PartMatrix(const MT5_Model* model, Matrix* matrix) {
    *matrix = MatrixIdentity();
    if (model->nodeCount == 0 || !model->nodes[0].hasMesh || !HasNegativeIndices(&model->nodes[0]))
        return false;
    switch (model->nodes[0].id & 0xFFFF) {
    case 0x03: // X -> -Z, Y -> -X, Z -> Y
        *matrix = (Matrix) { 0, -1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 0, 0, 0, 0, 1 };
        return true;
    case 0x05: case 0x06: case 0x07:
    case 0x0A: case 0x0B: case 0x0C: // X -> -Y, Y -> -Z, Z -> X
        *matrix = (Matrix) { 0, 0, 1, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1 };
        return true;
    default:
        return false;
    }
}

// First node with the given ID (lowest 16 bits) whose parent has the given ID, or any parent if parentID is UINT32_MAX.
// Returns -1 if there is none
static int32_t FindNode(const MT5_Model* model, uint32_t id, uint32_t parentID) {
    for (uint32_t i = 0; i < model->nodeCount; i++) {
        int32_t parent = model->nodes[i].parent;
        if ((model->nodes[i].id & 0xFFFF) == id &&
            (parentID == UINT32_MAX || (parent >= 0 && (model->nodes[parent].id & 0xFFFF) == parentID)))
            return (int32_t)i;
    }
    return -1;
}

// Head of the body of a character: the node 0xFFBD, or in some old models (e.g. KOB_L.chrm) the node 3 under the node 1
// if the model has the shoulders and upper arms (4 -> 5, 9 -> 0xA). Returns -1 if the model is not a body (items,
// objects)
static int32_t FindHead(const MT5_Model* model) {
    int32_t head = FindNode(model, 0xFFBD, UINT32_MAX);
    if (head < 0 && FindNode(model, 0x05, 0x04) >= 0 && FindNode(model, 0x0A, 0x09) >= 0)
        head = FindNode(model, 0x03, 0x01);
    return head;
}

// The face looks to the X axis of the head: -Z in most bodies, +Z in a few whose root is rotated (e.g. OGM_L.chrm). The
// faces and hands are rotated to look to -Z, and the rest of the models look to +Z
static Vector3 FrontDirection(const MT5_Model* model, const Matrix* world, bool part) {
    if (part)
        return (Vector3) { 0.0f, 0.0f, -1.0f };
    int32_t head = FindHead(model);
    if (head < 0)
        return (Vector3) { 0.0f, 0.0f, 1.0f };
    Vector3 front = { world[head].m0, 0.0f, world[head].m2 }; // X axis on the ground
    return Vector3Length(front) > 0.1f ? Vector3Normalize(front) : (Vector3) { 0.0f, 0.0f, -1.0f };
}

static bool IsLeaf(const MT5_Model* model, uint32_t node) {
    return node + 1 >= model->nodeCount || model->nodes[node + 1].parent != (int32_t)node; // Depth first order
}

// Some models are animations made of frames (e.g. the birds of model/object, bf60200g.mt5, or the flame bfa0101g.mt5):
// a node without mesh with 3 or more children that are the frames, leaf nodes with a mesh, the same transform and
// overlapping bounding spheres. Sets the frame of those nodes (-1 for the rest) and returns the number of frames
static uint32_t FindFrames(const MT5_Model* model, int32_t* nodeFrame) {
    for (uint32_t i = 0; i < model->nodeCount; i++)
        nodeFrame[i] = -1;

    for (uint32_t p = 0; p < model->nodeCount; p++) {
        if (model->nodes[p].hasMesh)
            continue;
        uint32_t count = 0, first = 0;
        bool frames = true;
        float maxRadius = 0.0f;
        for (uint32_t i = p + 1; i < model->nodeCount && frames; i++) {
            const MT5_Node* node = &model->nodes[i];
            if (node->parent != (int32_t)p)
                continue;
            if (count++ == 0)
                first = i;
            const MT5_Node* other = &model->nodes[first];
            frames = node->hasMesh && IsLeaf(model, i) &&
                memcmp(node->position, other->position, sizeof(node->position)) == 0 &&
                memcmp(node->rotation, other->rotation, sizeof(node->rotation)) == 0 &&
                memcmp(node->scale, other->scale, sizeof(node->scale)) == 0;
            maxRadius = fmaxf(maxRadius, node->radius);
        }
        if (!frames || count < 3)
            continue;

        // The frames overlap; the parts of an object are next to each other
        for (uint32_t i = p + 1; i < model->nodeCount && frames; i++) {
            for (uint32_t j = i + 1; j < model->nodeCount && frames && model->nodes[i].parent == (int32_t)p; j++) {
                if (model->nodes[j].parent == (int32_t)p) {
                    Vector3 a = { model->nodes[i].center[0], model->nodes[i].center[1], model->nodes[i].center[2] };
                    Vector3 b = { model->nodes[j].center[0], model->nodes[j].center[1], model->nodes[j].center[2] };
                    frames = Vector3Distance(a, b) <= maxRadius;
                }
            }
        }
        if (!frames)
            continue;

        uint32_t frame = 0;
        for (uint32_t i = p + 1; i < model->nodeCount; i++) {
            if (model->nodes[i].parent == (int32_t)p)
                nodeFrame[i] = (int32_t)frame++;
        }
        return count;
    }
    return 0;
}

bool MT5Mesh_Build(const MT5_Model* model, MT5Mesh* result) {
    Matrix* world = malloc(model->nodeCount * (2 * sizeof(Matrix) + sizeof(int32_t) + sizeof(bool)));
    if (world == NULL)
        return false;
    Matrix* normalMatrix = world + model->nodeCount;
    int32_t* nodeFrame = (int32_t*)(normalMatrix + model->nodeCount);
    bool* hidden = (bool*)(nodeFrame + model->nodeCount);
    result->frameCount = FindFrames(model, nodeFrame);
    Matrix part;
    bool isPart = PartMatrix(model, &part);
    bool body = FindHead(model) >= 0;
    // The nodes are in depth first order, the parents are before their children
    for (uint32_t i = 0; i < model->nodeCount; i++) {
        int32_t parent = model->nodes[i].parent;
        Matrix local = NodeMatrix(&model->nodes[i], body ? ArmSpread(model, i) : 0.0f);
        world[i] = parent >= 0 ? MatrixMultiply(local, world[parent]) : MatrixMultiply(local, part);
        normalMatrix[i] = MatrixTranspose(MatrixInvert(world[i]));
        hidden[i] = IsAlternativeRoot(model, i) || (parent >= 0 && hidden[parent]);
        if (hidden[i] && model->nodes[i].hasMesh)
            result->hiddenNodes++;
    }
    result->front = FrontDirection(model, world, isPart);

    // Count the triangles of each group
    for (uint32_t n = 0; n < model->nodeCount; n++) {
        const MT5_Node* node = &model->nodes[n];
        if (hidden[n])
            continue;
        for (uint32_t t = 0; t < node->triangleCount; t++) {
            const MT5_Triangle* triangle = &node->triangles[t];
            if (!IsTriangleValid(model, n, triangle)) {
                result->skippedTriangles++;
                continue;
            }
            MT5Mesh_Group* group = FindGroup(result, triangle, nodeFrame[n]);
            if (group == NULL) {
                if (!AddGroup(result, triangle, nodeFrame[n])) {
                    free(world);
                    return false;
                }
                group = &result->groups[result->groupCount - 1];
            }
            group->triangleCount++;
            result->triangleCount++;
        }
    }

    for (uint32_t i = 0; i < result->groupCount; i++) {
        Mesh* mesh = &result->groups[i].mesh;
        mesh->triangleCount = (int)result->groups[i].triangleCount;
        mesh->vertexCount = mesh->triangleCount * 3;
        mesh->vertices = MemAlloc(mesh->vertexCount * 3 * sizeof(float));
        mesh->normals = MemAlloc(mesh->vertexCount * 3 * sizeof(float));
        mesh->texcoords = MemAlloc(mesh->vertexCount * 2 * sizeof(float));
        mesh->colors = MemAlloc(mesh->vertexCount * 4);
        if (mesh->vertices == NULL || mesh->normals == NULL || mesh->texcoords == NULL || mesh->colors == NULL) {
            free(world);
            return false;
        }
    }

    Vector3 min = { INFINITY, INFINITY, INFINITY };
    Vector3 max = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t n = 0; n < model->nodeCount; n++) {
        const MT5_Node* node = &model->nodes[n];
        if (hidden[n])
            continue;
        for (uint32_t t = 0; t < node->triangleCount; t++) {
            const MT5_Triangle* triangle = &node->triangles[t];
            if (!IsTriangleValid(model, n, triangle))
                continue;
            MT5Mesh_Group* group = FindGroup(result, triangle, nodeFrame[n]);
            // A mirrored transform (negative scale) changes the winding
            bool mirrored = MatrixDeterminant(world[n]) < 0.0f;
            for (int c = 0; c < 3; c++) {
                uint32_t owner, vertex;
                ResolveVertex(model, n, triangle->corners[c].vertex, &owner, &vertex);
                const MT5_Vertex* source = &model->nodes[owner].vertices[vertex];
                Vector3 position = Vector3Transform((Vector3) { source->position[0], source->position[1], source->position[2] }, world[owner]);
                Vector3 normal = TransformNormal(source->normal, normalMatrix[owner]);
                int index = mirrored && c > 0 ? 3 - c : c;
                AddCorner(&group->mesh, group->filled * 3 + index, &triangle->corners[c], position, normal);
                min = Vector3Min(min, position);
                max = Vector3Max(max, position);
            }
            group->filled++;
        }
    }
    free(world);

    for (uint32_t i = 0; i < result->groupCount; i++)
        UploadMesh(&result->groups[i].mesh, false);

    if (result->triangleCount > 0) {
        result->center = Vector3Scale(Vector3Add(min, max), 0.5f);
        result->radius = Vector3Distance(min, max) * 0.5f;
    }
    if (result->radius <= 0.0f)
        result->radius = 1.0f;
    return true;
}

void MT5Mesh_Free(MT5Mesh* meshes) {
    for (uint32_t i = 0; i < meshes->groupCount; i++)
        UnloadMesh(meshes->groups[i].mesh);
    free(meshes->groups);
    memset(meshes, 0, sizeof(*meshes));
}

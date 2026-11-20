#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "core/log.h"
#include "formats/model/hm.h"

#define HM_HEADER_SIZE  0x18
#define HM_NODE_SIZE    0x50

static float ReadFloat(const uint8_t* p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// Header: offsets of the order of the nodes (2 bytes each), of the nodes (0x50 bytes each), of the influence counts (1
// byte per vertex, up to the end of the file), of the nodes of the influences (1 byte each) and of their weights (float)
bool HM_LoadFromMemory(const uint8_t* data, size_t size, const char* name, HM_Skin* skin) {
    memset(skin, 0, sizeof(*skin));
    if (size < HM_HEADER_SIZE) {
        Log_Error("[HM_Load]: File too small: %s\n", name);
        return false;
    }
    uint32_t orderOffset = Bytes_ReadU32(data), nodesOffset = Bytes_ReadU32(data + 4), countsOffset = Bytes_ReadU32(data + 8);
    uint32_t indicesOffset = Bytes_ReadU32(data + 12), weightsOffset = Bytes_ReadU32(data + 16);
    if (orderOffset > size || size - orderOffset < HM_NODES * 2 || nodesOffset > size || size - nodesOffset < HM_NODES * HM_NODE_SIZE ||
        indicesOffset > weightsOffset || weightsOffset > countsOffset || countsOffset > size) {
        Log_Error("[HM_Load]: Invalid offsets: %s\n", name);
        return false;
    }
    for (int n = 0; n < HM_NODES; n++) {
        HM_Node* node = &skin->nodes[n];
        const uint8_t* p = data + nodesOffset + n * HM_NODE_SIZE;
        node->joint = data[orderOffset + n * 2];
        node->children = data[orderOffset + n * 2 + 1];
        node->length = ReadFloat(p);
        for (int v = 0; v < 3; v++)
            node->angles[v] = ReadFloat(p + 4 + v * 4);
        for (int v = 0; v < 16; v++)
            node->rest[v] = ReadFloat(p + 0x10 + v * 4);
    }

    uint32_t vertexCount = (uint32_t)(size - countsOffset);
    uint32_t maxInfluences = weightsOffset - indicesOffset;
    skin->firsts = malloc((vertexCount + 1) * sizeof(uint32_t));
    skin->counts = malloc(vertexCount + 1);
    skin->nodes8 = malloc(maxInfluences + 1);
    skin->weights = malloc((maxInfluences + 1) * sizeof(float));
    if (skin->firsts == NULL || skin->counts == NULL || skin->nodes8 == NULL || skin->weights == NULL) {
        HM_Free(skin);
        return false;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < vertexCount; i++) {
        uint32_t count = data[countsOffset + i];
        // Stops at the first count that goes past the influences (padding at the end)
        if (count == 0 || total + count > maxInfluences || total + count > (countsOffset - weightsOffset) / 4)
            break;
        skin->firsts[i] = total;
        skin->counts[i] = (uint8_t)count;
        for (uint32_t k = total; k < total + count; k++) {
            uint8_t node = data[indicesOffset + k];
            skin->nodes8[k] = node < HM_NODES ? node : 0;
            skin->weights[k] = node < HM_NODES ? ReadFloat(data + weightsOffset + k * 4) : 0.0f;
        }
        total += count;
        skin->vertexCount = i + 1;
    }
    skin->influenceCount = total;
    return true;
}

bool HM_Load(const char* filepath, HM_Skin* skin) {
    size_t size = 0;
    uint8_t* data = File_LoadToMemoryEx(filepath, &size);
    if (data == NULL)
        return false;
    bool loaded = HM_LoadFromMemory(data, size, filepath, skin);
    free(data);
    return loaded;
}

void HM_Free(HM_Skin* skin) {
    free(skin->firsts);
    free(skin->counts);
    free(skin->nodes8);
    free(skin->weights);
    memset(skin, 0, sizeof(*skin));
}

// 4x4 matrices by columns: m[column * 4 + row]
static void Multiply(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] + a[12 + row] * b[c * 4 + 3];
    }
    memcpy(out, r, sizeof(r));
}

// The local matrix of a node: translation, then rotation around Z, Y and X (as the game, code at 0x0C0905DC of
// 1ST_READ.BIN)
static void Local(float x, float y, float z, const float angles[3], float out[16]) {
    float cx = cosf(angles[0]), sx = sinf(angles[0]), cy = cosf(angles[1]), sy = sinf(angles[1]);
    float cz = cosf(angles[2]), sz = sinf(angles[2]);
    // Rz * Ry * Rx
    float r[9] = {
        cz * cy, sz * cy, -sy,
        cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx,
        cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx,
    };
    float m[16] = { r[0], r[1], r[2], 0.0f, r[3], r[4], r[5], 0.0f, r[6], r[7], r[8], 0.0f, x, y, z, 1.0f };
    memcpy(out, m, sizeof(m));
}

static uint32_t PoseNode(const HM_Skin* skin, const float (*angles)[3], uint32_t n, const float parent[16], float matrices[HM_NODES][16]) {
    if (n >= HM_NODES)
        return n;
    const HM_Node* node = &skin->nodes[n];
    float local[16];
    if (node->joint == 0)
        Local(node->angles[0], node->angles[1], node->angles[2], (const float[3]) { 0.0f, 0.0f, 0.0f }, local);
    else
        Local(node->length, 0.0f, 0.0f, angles != NULL ? angles[n] : node->angles, local);
    Multiply(parent, local, matrices[n]);
    uint32_t next = n + 1;
    for (uint32_t c = 0; c < node->children; c++)
        next = PoseNode(skin, angles, next, matrices[n], matrices);
    return next;
}

void HM_Pose(const HM_Skin* skin, const float (*angles)[3], float matrices[HM_NODES][16]) {
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for (uint32_t n = 0; n < HM_NODES;)
        n = PoseNode(skin, angles, n, identity, matrices);
}

// Code at 0x0C09082C: the point goes to the space of each node at rest, then back to the skeleton with the node posed
void HM_Deform(const HM_Skin* skin, const float matrices[HM_NODES][16], const float* positions, uint32_t vertexCount, bool left,
    float* offsets) {
    for (uint32_t i = 0; i < vertexCount; i++) {
        float* out = &offsets[i * 3];
        out[0] = out[1] = out[2] = 0.0f;
        if (i >= skin->vertexCount)
            continue;
        const float* p = &positions[i * 3];
        float s[3] = { p[1], -p[0], left ? -p[2] : p[2] }, moved[3] = { 0.0f, 0.0f, 0.0f };
        for (uint32_t k = skin->firsts[i]; k < skin->firsts[i] + skin->counts[i]; k++) {
            const float* rest = skin->nodes[skin->nodes8[k]].rest;
            const float* posed = matrices[skin->nodes8[k]];
            float u[3] = { s[0] - rest[12], s[1] - rest[13], s[2] - rest[14] }, local[3];
            for (int a = 0; a < 3; a++)
                local[a] = rest[a * 4] * u[0] + rest[a * 4 + 1] * u[1] + rest[a * 4 + 2] * u[2];
            for (int a = 0; a < 3; a++) {
                float w = posed[12 + a] + posed[a] * local[0] + posed[4 + a] * local[1] + posed[8 + a] * local[2];
                moved[a] += skin->weights[k] * (w - s[a]);
            }
        }
        // Back to the space of the model: x = -y, y = x
        out[0] = -moved[1];
        out[1] = moved[0];
        out[2] = left ? -moved[2] : moved[2];
    }
}

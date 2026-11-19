#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "core/log.h"
#include "formats/model/ftbl.h"

#define FTBL_HEADER_SIZE    0x30
#define FTBL_BONE_SIZE      0x30

static float ReadFloat(const uint8_t* p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// Header: talk set, offset of the bones (0x30), offset of the three offsets of the skin (0x24), 6 limits of the eyes,
// then the offsets of the counts, the indices and the weights. The skin has the bone indices (int32), the weights
// (float) and the number of bones of each vertex (int32); the sections can end with one or two unused values
bool FTBL_LoadFromMemory(const uint8_t* data, size_t size, const char* name, FTBL_Skin* skin) {
    memset(skin, 0, sizeof(*skin));
    if (size < FTBL_HEADER_SIZE) {
        Log_Error("[FTBL_Load]: File too small: %s\n", name);
        return false;
    }
    uint32_t bonesOffset = Bytes_ReadU32(data + 4), skinOffset = Bytes_ReadU32(data + 8);
    if (skinOffset > size - 12 || bonesOffset > size || size - bonesOffset < FTBL_BONES * FTBL_BONE_SIZE) {
        Log_Error("[FTBL_Load]: Invalid offsets: %s\n", name);
        return false;
    }
    uint32_t countsOffset = Bytes_ReadU32(data + skinOffset), indicesOffset = Bytes_ReadU32(data + skinOffset + 4);
    uint32_t weightsOffset = Bytes_ReadU32(data + skinOffset + 8);
    if (countsOffset > size || indicesOffset > weightsOffset || weightsOffset > countsOffset) {
        Log_Error("[FTBL_Load]: Invalid skin offsets: %s\n", name);
        return false;
    }
    skin->talkSet = (int32_t)Bytes_ReadU32(data);
    for (int i = 0; i < 6; i++)
        skin->eyeLimits[i] = (int32_t)Bytes_ReadU32(data + 0x0C + i * 4);
    for (int b = 0; b < FTBL_BONES; b++) {
        const uint8_t* bone = data + bonesOffset + b * FTBL_BONE_SIZE;
        FTBL_Bone* out = &skin->bones[b];
        for (int v = 0; v < 3; v++) {
            out->tip[v] = ReadFloat(bone + v * 4);
            out->root[v] = ReadFloat(bone + 0x10 + v * 4);
            out->angles[v] = ReadFloat(bone + 0x1C + v * 4);
        }
        out->type = (int32_t)Bytes_ReadU32(bone + 0x0C);
        out->lengths[0] = ReadFloat(bone + 0x28);
        out->lengths[1] = ReadFloat(bone + 0x2C);
    }

    uint32_t vertexCount = (uint32_t)((size - countsOffset) / 4);
    uint32_t maxInfluences = (weightsOffset - indicesOffset) / 4;
    skin->firsts = malloc((vertexCount + 1) * sizeof(uint32_t));
    skin->counts = malloc(vertexCount + 1);
    skin->bones8 = malloc(maxInfluences + 1);
    skin->weights = malloc((maxInfluences + 1) * sizeof(float));
    if (skin->firsts == NULL || skin->counts == NULL || skin->bones8 == NULL || skin->weights == NULL) {
        FTBL_Free(skin);
        return false;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < vertexCount; i++) {
        uint32_t count = Bytes_ReadU32(data + countsOffset + i * 4);
        // Stops at the first count that goes past the influences (unused values at the end)
        if (count > FTBL_BONES || total + count > maxInfluences || total + count > (countsOffset - weightsOffset) / 4)
            break;
        skin->firsts[i] = total;
        skin->counts[i] = (uint8_t)count;
        for (uint32_t k = total; k < total + count; k++) {
            uint32_t bone = Bytes_ReadU32(data + indicesOffset + k * 4);
            skin->bones8[k] = (uint8_t)(bone < FTBL_BONES ? bone : 0);
            skin->weights[k] = bone < FTBL_BONES ? ReadFloat(data + weightsOffset + k * 4) : 0.0f;
        }
        total += count;
        skin->vertexCount = i + 1;
    }
    skin->influenceCount = total;
    return true;
}

bool FTBL_Load(const char* filepath, FTBL_Skin* skin) {
    size_t size = 0;
    uint8_t* data = File_LoadToMemoryEx(filepath, &size);
    if (data == NULL)
        return false;
    bool loaded = FTBL_LoadFromMemory(data, size, filepath, skin);
    free(data);
    return loaded;
}

void FTBL_Free(FTBL_Skin* skin) {
    free(skin->firsts);
    free(skin->counts);
    free(skin->bones8);
    free(skin->weights);
    memset(skin, 0, sizeof(*skin));
}

void FTBL_Deform(const FTBL_Skin* skin, const float boneOffsets[FTBL_BONES][3], uint32_t vertexCount, float* vertexOffsets) {
    for (uint32_t i = 0; i < vertexCount; i++) {
        float* out = &vertexOffsets[i * 3];
        out[0] = out[1] = out[2] = 0.0f;
        if (i >= skin->vertexCount)
            continue;
        for (uint32_t k = skin->firsts[i]; k < skin->firsts[i] + skin->counts[i]; k++) {
            const float* offset = boneOffsets[skin->bones8[k]];
            out[0] += skin->weights[k] * offset[0];
            out[1] += skin->weights[k] * offset[1];
            out[2] += skin->weights[k] * offset[2];
        }
    }
}

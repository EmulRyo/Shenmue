#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "core/log.h"
#include "formats/animation/talk.h"

#define TALK_HEADER_SIZE    0x10
#define TALK_KEY_SIZE       8
#define TALK_TABLE_SIZE     (TALK_CHANNELS * 4)

// A set: header (offsets of the two channel tables, number of poses, size of the set or 0), the keys of the channels
// one after the other, then the two tables: for each channel its number of keys (0 in the table of the other part of
// the face) and the position of its first key divided by 16
static bool ReadSet(const uint8_t* data, size_t size, TALK_Set* set) {
    memset(set, 0, sizeof(*set));
    if (size < TALK_HEADER_SIZE)
        return false;
    uint32_t table1 = Bytes_ReadU32(data), table2 = Bytes_ReadU32(data + 4);
    set->poseCount = Bytes_ReadU32(data + 8);
    if (table1 < TALK_HEADER_SIZE || table1 > size - TALK_TABLE_SIZE || table2 > size - TALK_TABLE_SIZE || set->poseCount == 0)
        return false;
    uint32_t pos = TALK_HEADER_SIZE;
    for (uint32_t c = 0; c < TALK_CHANNELS; c++) {
        uint16_t count1 = Bytes_ReadU16(data + table1 + c * 4), count2 = Bytes_ReadU16(data + table2 + c * 4);
        uint32_t count = count1 != 0 ? count1 : count2;
        if (pos + (size_t)count * TALK_KEY_SIZE > table1)
            return false;
        set->keyCounts[c] = count;
        set->keys[c] = data + pos;
        set->layers[c] = count1 != 0 ? TALK_LAYER_EYES : TALK_LAYER_MOUTH;
        pos += count * TALK_KEY_SIZE;
    }
    return true;
}

bool TALK_Load(const char* filepath, TALK_File* file) {
    memset(file, 0, sizeof(*file));
    file->buffer = File_LoadToMemoryEx(filepath, &file->size);
    if (file->buffer == NULL)
        return false;
    size_t pos = 0;
    while (pos < file->size && file->setCount < TALK_MAX_SETS) {
        if (!ReadSet(file->buffer + pos, file->size - pos, &file->sets[file->setCount]))
            break;
        file->setCount++;
        uint32_t next = Bytes_ReadU32(file->buffer + pos + 12);
        if (next == 0)
            break;
        pos += next;
    }
    if (file->setCount == 0) {
        Log_Error("[TALK_Load]: Invalid file: %s\n", filepath);
        TALK_Free(file);
        return false;
    }
    return true;
}

void TALK_Free(TALK_File* file) {
    free(file->buffer);
    memset(file, 0, sizeof(*file));
}

float TALK_Float(uint16_t value) {
    if ((value & 0x7FFF) == 0)
        return 0.0f;
    uint32_t bits = ((uint32_t)(value & 0x8000) << 16) | ((uint32_t)(value & 0x3FFF) << 14);
    bits += 0x38000000u - ((uint32_t)((value >> 14) & 1) << 28);
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static inline uint16_t KeyField(const uint8_t* key, int field) {
    return Bytes_ReadU16(key + field * 2);
}

// The last key whose pose is before the pose (binary search), then a Hermite curve to the next one: the value at offset
// 6 of the keys, the slope out of the first key (offset 4) and the slope into the second (offset 2), per pose
float TALK_Value(const TALK_Set* set, uint32_t channel, int32_t pose) {
    uint32_t count = set->keyCounts[channel];
    const uint8_t* keys = set->keys[channel];
    if (count == 0)
        return 0.0f;
    uint32_t low = 0, high = count;
    do {
        uint32_t middle = (low + high) >> 1;
        if ((int16_t)KeyField(keys + middle * TALK_KEY_SIZE, 0) >= pose)
            high = middle;
        else
            low = middle;
    } while (high > low + 1);
    if (high == 0)
        return TALK_Float(KeyField(keys, 3));
    const uint8_t* a = keys + (high - 1) * TALK_KEY_SIZE;
    if (high >= count || KeyField(a, 0) == (uint16_t)pose)
        return TALK_Float(KeyField(a, 3));
    const uint8_t* b = a + TALK_KEY_SIZE;
    float span = (float)((int32_t)KeyField(b, 0) - (int32_t)KeyField(a, 0));
    float t = (float)(pose - (int32_t)KeyField(a, 0)) / span;
    float t2 = t * t, t3 = t2 * t;
    float h01 = 3.0f * t2 - 2.0f * t3, h00 = 1.0f - h01, h10 = t3 - 2.0f * t2 + t, h11 = t3 - t2;
    return span * (TALK_Float(KeyField(a, 2)) * h10 + TALK_Float(KeyField(b, 1)) * h11) +
        TALK_Float(KeyField(a, 3)) * h00 + TALK_Float(KeyField(b, 3)) * h01;
}

// The end of the chain of two segments of a bone that reaches to a point (code at 0x0C090008): the point at the same
// direction from the root, at a distance between the difference and the sum of the lengths of the segments
static void ChainEnd(const FTBL_Bone* bone, const float target[3], float end[3]) {
    float d[3] = { target[0] - bone->root[0], target[1] - bone->root[1], target[2] - bone->root[2] };
    float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float a = bone->lengths[0], b = bone->lengths[1];
    float reach = bone->type == 1 ? a : fminf(fmaxf(length, fabsf(a - b)), a + b);
    if (length <= 0.0f) {
        // Along the direction of the tip
        float t[3] = { bone->tip[0] - bone->root[0], bone->tip[1] - bone->root[1], bone->tip[2] - bone->root[2] };
        float tipLength = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        for (int v = 0; v < 3; v++)
            end[v] = bone->root[v] + (tipLength > 0.0f ? t[v] / tipLength * reach : 0.0f);
        return;
    }
    for (int v = 0; v < 3; v++)
        end[v] = bone->root[v] + d[v] / length * reach;
}

bool TALK_BoneOffsets(const TALK_Set* set, const FTBL_Skin* skin, TALK_Layer layer, int32_t pose, float offsets[FTBL_BONES][3]) {
    bool any = false;
    for (int b = 0; b < FTBL_BONES; b++) {
        if (set->keyCounts[b * 3] == 0 || set->layers[b * 3] != layer)
            continue;
        const FTBL_Bone* bone = &skin->bones[b];
        float target[3], end[3];
        for (int v = 0; v < 3; v++)
            target[v] = TALK_Value(set, (uint32_t)(b * 3 + v), pose);
        ChainEnd(bone, target, end);
        for (int v = 0; v < 3; v++)
            offsets[b][v] = end[v] - bone->tip[v];
        any = true;
    }
    return any;
}

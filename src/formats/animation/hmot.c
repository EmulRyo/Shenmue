#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "core/log.h"
#include "formats/animation/hmot.h"

#define HMOT_MOTION_HEADER  0x28    // Name (32 bytes), frames, nodes
#define HMOT_KEY_SIZE       16
#define HMOT_MAX_NODES      256

static float ReadFloat(const uint8_t* p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// Motion: name, frames, nodes, then (key count, offset of the keys from the end of the header) for each channel
static bool ReadMotion(const uint8_t* data, size_t size, uint32_t offset, HMOT_Motion* motion) {
    if (offset > size || size - offset < HMOT_MOTION_HEADER)
        return false;
    const uint8_t* p = data + offset;
    memcpy(motion->name, p, 32);
    motion->name[32] = '\0';
    motion->frames = (int32_t)Bytes_ReadU32(p + 0x20);
    motion->nodeCount = Bytes_ReadU32(p + 0x24);
    uint32_t channels = motion->nodeCount * 3, base = offset + HMOT_MOTION_HEADER;
    if (motion->nodeCount > HMOT_MAX_NODES || size - base < channels * 8)
        return false;
    motion->keyCounts = calloc(channels + 1, sizeof(uint32_t));
    motion->keys = calloc(channels + 1, sizeof(HMOT_Key*));
    if (motion->keyCounts == NULL || motion->keys == NULL)
        return false;
    for (uint32_t c = 0; c < channels; c++) {
        int32_t count = (int32_t)Bytes_ReadU32(data + base + c * 8);
        uint32_t keys = Bytes_ReadU32(data + base + c * 8 + 4);
        if (count <= 0)
            continue;
        if (keys > size - base || (size - base - keys) / HMOT_KEY_SIZE < (uint32_t)count)
            return false;
        motion->keys[c] = malloc((size_t)count * sizeof(HMOT_Key));
        if (motion->keys[c] == NULL)
            return false;
        motion->keyCounts[c] = (uint32_t)count;
        for (int32_t k = 0; k < count; k++) {
            const uint8_t* key = data + base + keys + k * HMOT_KEY_SIZE;
            motion->keys[c][k] = (HMOT_Key) { (int32_t)Bytes_ReadU32(key), ReadFloat(key + 4), ReadFloat(key + 8), ReadFloat(key + 12) };
        }
    }
    return true;
}

// Header: last ID (uint16), first ID (uint16), then the offset of each motion
bool HMOT_LoadFromMemory(const uint8_t* data, size_t size, const char* name, HMOT_File* file) {
    memset(file, 0, sizeof(*file));
    if (size < 8) {
        Log_Error("[HMOT_Load]: File too small: %s\n", name);
        return false;
    }
    uint32_t last = Bytes_ReadU16(data), first = Bytes_ReadU16(data + 2);
    if (last < first || (size - 4) / 4 < last - first + 1) {
        Log_Error("[HMOT_Load]: Invalid IDs %u to %u: %s\n", first, last, name);
        return false;
    }
    file->firstId = first;
    file->motionCount = last - first + 1;
    file->motions = calloc(file->motionCount, sizeof(HMOT_Motion));
    if (file->motions == NULL)
        return false;
    for (uint32_t m = 0; m < file->motionCount; m++) {
        file->motions[m].id = first + m;
        if (!ReadMotion(data, size, Bytes_ReadU32(data + 4 + m * 4), &file->motions[m])) {
            Log_Error("[HMOT_Load]: Invalid motion %u: %s\n", first + m, name);
            HMOT_Free(file);
            return false;
        }
    }
    return true;
}

bool HMOT_Load(const char* filepath, HMOT_File* file) {
    size_t size = 0;
    uint8_t* data = File_LoadToMemoryEx(filepath, &size);
    if (data == NULL)
        return false;
    bool loaded = HMOT_LoadFromMemory(data, size, filepath, file);
    free(data);
    return loaded;
}

void HMOT_Free(HMOT_File* file) {
    for (uint32_t m = 0; file->motions != NULL && m < file->motionCount; m++) {
        HMOT_Motion* motion = &file->motions[m];
        for (uint32_t c = 0; motion->keys != NULL && c < motion->nodeCount * 3; c++)
            free(motion->keys[c]);
        free(motion->keys);
        free(motion->keyCounts);
    }
    free(file->motions);
    memset(file, 0, sizeof(*file));
}

const HMOT_Motion* HMOT_Find(const HMOT_File* file, uint32_t id) {
    return id >= file->firstId && id - file->firstId < file->motionCount ? &file->motions[id - file->firstId] : NULL;
}

float HMOT_Value(const HMOT_Motion* motion, uint32_t channel, float frame, float rest) {
    if (channel >= motion->nodeCount * 3 || motion->keyCounts[channel] == 0)
        return rest;
    const HMOT_Key* keys = motion->keys[channel];
    uint32_t count = motion->keyCounts[channel], b = 0;
    while (b < count && (float)keys[b].frame < frame)
        b++;
    if (b == 0)
        return keys[0].value;
    if (b == count)
        return keys[count - 1].value;
    const HMOT_Key* ka = &keys[b - 1], * kb = &keys[b];
    float d = (float)(kb->frame - ka->frame), t = (frame - (float)ka->frame) / d;
    float t2 = t * t, t3 = t2 * t;
    return ka->value * (1.0f - 3.0f * t2 + 2.0f * t3) + kb->value * (3.0f * t2 - 2.0f * t3) +
        d * (ka->slopeOut * (t3 - 2.0f * t2 + t) + kb->slopeIn * (t3 - t2));
}

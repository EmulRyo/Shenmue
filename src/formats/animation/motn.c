#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/animation/motn.h"

// Header: table offset (always 0x20), names offset, data offset, last ID, size, first ID, last ID again, 0.
// There are lastID - firstID - 1 motions; the table has 8 bytes per motion (data offset from the data offset of the
// header, offset of the attribute block) and the names offset points to one name offset per motion
#define MOTN_HEADER_SIZE 0x20

// First word of a motion: frames in bits 0-14, wide counts in bit 15, type in bits 16-31
#define MOTN_FRAMES_MASK    0x7FFF
#define MOTN_WIDE           0x8000
#define MOTN_SAMPLED_TYPE   0xFFFF
#define MOTN_RAW_FLAG       0x8000

// Header of a keyframe motion: the first word, the offsets of the 4 sections, then the bone list
#define MOTN_KEYFRAME_HEADER 12

void MOTN_Free(MOTN_Bank* bank) {
    free(bank->names);
    free(bank->offsets);
    free(bank->data);
    memset(bank, 0, sizeof(MOTN_Bank));
}

static bool ReadTable(MOTN_Bank* bank) {
    const uint8_t* data = bank->data;
    if (bank->size < MOTN_HEADER_SIZE || Bytes_ReadU32(data) != MOTN_HEADER_SIZE)
        return false;
    uint32_t namesOffset = Bytes_ReadU32(data + 0x04);
    uint32_t dataOffset = Bytes_ReadU32(data + 0x08);
    uint32_t lastID = Bytes_ReadU32(data + 0x0C);
    bank->firstID = Bytes_ReadU32(data + 0x14);
    if (lastID <= bank->firstID || Bytes_ReadU32(data + 0x18) != lastID)
        return false;
    bank->count = lastID - bank->firstID - 1;
    if (namesOffset > bank->size || bank->count > (bank->size - namesOffset) / 4 ||
        bank->count > (bank->size - MOTN_HEADER_SIZE) / 8 || dataOffset > bank->size)
        return false;

    bank->names = calloc(bank->count + 1, sizeof(const char*));
    bank->offsets = calloc(bank->count + 1, sizeof(uint32_t));
    if (bank->names == NULL || bank->offsets == NULL)
        return false;
    for (uint32_t i = 0; i < bank->count; i++) {
        uint32_t offset = Bytes_ReadU32(data + namesOffset + i * 4);
        if (offset >= bank->size || memchr(data + offset, '\0', bank->size - offset) == NULL)
            return false;
        bank->names[i] = (const char*)(data + offset);
        uint32_t motion = Bytes_ReadU32(data + MOTN_HEADER_SIZE + i * 8);
        bank->offsets[i] = motion < bank->size - dataOffset ? dataOffset + motion : 0; // 0: invalid
    }
    return true;
}

bool MOTN_Load(const char* filepath, MOTN_Bank* bank) {
    memset(bank, 0, sizeof(MOTN_Bank));
    bank->data = File_LoadToMemoryDecompressed(filepath, &bank->size);
    if (bank->data == NULL)
        return false;
    if (!ReadTable(bank)) {
        fprintf(stderr, "[MOTN_Load]: Invalid MOTN file %s\n", filepath);
        MOTN_Free(bank);
        return false;
    }
    return true;
}

int32_t MOTN_FindMotion(const MOTN_Bank* bank, uint32_t id) {
    if (id <= bank->firstID || id - bank->firstID - 1 >= bank->count)
        return -1;
    return (int32_t)(id - bank->firstID - 1);
}

const char* MOTN_FindName(const MOTN_Bank* bank, uint32_t id) {
    int32_t index = MOTN_FindMotion(bank, id);
    return index >= 0 ? bank->names[index] : NULL;
}

void MOTN_FreeMotion(MOTN_Motion* motion) {
    free(motion->channels);
    free(motion->keys);
    memset(motion, 0, sizeof(MOTN_Motion));
}

// Reads the sections of a keyframe motion that starts at data and has size bytes up to the end of the file
static bool ReadKeyframes(const uint8_t* data, size_t size, bool wide, MOTN_Motion* motion) {
    if (size < MOTN_KEYFRAME_HEADER)
        return false;
    uint32_t sections[4];
    for (int i = 0; i < 4; i++) {
        sections[i] = Bytes_ReadU16(data + 4 + i * 2);
        if (sections[i] > size)
            return false;
    }

    // Bone list: one uint16 per bone, bits 8 to 3 the position X, Y, Z and the rotation X, Y, Z, ended by 0
    uint32_t count = 0;
    size_t p = MOTN_KEYFRAME_HEADER;
    for (; p + 2 <= sections[0] && Bytes_ReadU16(data + p) != 0; p += 2) {
        for (int bit = 8; bit >= 3; bit--)
            count += (Bytes_ReadU16(data + p) >> bit) & 1;
    }
    size_t end = p;
    if (end + 2 > size || Bytes_ReadU16(data + end) != 0)
        return false;
    motion->channels = calloc(count + 1, sizeof(MOTN_Channel));
    if (motion->channels == NULL)
        return false;
    for (p = MOTN_KEYFRAME_HEADER; p < end; p += 2) {
        uint16_t bone = Bytes_ReadU16(data + p);
        for (int bit = 8; bit >= 3; bit--) {
            if ((bone >> bit) & 1)
                motion->channels[motion->channelCount++] = (MOTN_Channel) { (uint8_t)(bone >> 9), (uint8_t)(8 - bit), 0, NULL };
        }
    }

    // Section 0: the keys of each channel besides the first and the last one
    uint32_t keyCount = 0;
    size_t countSize = wide ? 2 : 1;
    if (sections[0] + (size_t)count * countSize > size)
        return false;
    for (uint32_t c = 0; c < count; c++) {
        const uint8_t* q = data + sections[0] + c * countSize;
        motion->channels[c].keyCount = (wide ? Bytes_ReadU16(q) : *q) + 2u;
        keyCount += motion->channels[c].keyCount;
    }
    motion->keys = calloc(keyCount + 1, sizeof(MOTN_Key));
    if (motion->keys == NULL)
        return false;

    // Section 1: the frames of the inner keys, uint16 if there are more than 255 frames
    size_t frameSize = motion->frames > 255 ? 2 : 1;
    size_t frames = sections[1];
    MOTN_Key* key = motion->keys;
    for (uint32_t c = 0; c < count; c++) {
        MOTN_Channel* channel = &motion->channels[c];
        channel->keys = key;
        key[0].frame = 0;
        key[channel->keyCount - 1].frame = (uint16_t)motion->frames;
        if (frames + (channel->keyCount - 2) * frameSize > size)
            return false;
        for (uint32_t k = 1; k + 1 < channel->keyCount; k++, frames += frameSize)
            key[k].frame = frameSize == 2 ? Bytes_ReadU16(data + frames) : data[frames];
        key += channel->keyCount;
    }

    // Section 2: 2 bits per key, from the highest bits, every channel on a new byte. Section 3: as many float16 values
    // per key as its type: [in tangent, [out tangent,]] value
    size_t types = sections[2], values = sections[3];
    for (uint32_t c = 0; c < count; c++) {
        MOTN_Channel* channel = &motion->channels[c];
        if (types + (channel->keyCount + 3) / 4 > size)
            return false;
        for (uint32_t k = 0; k < channel->keyCount; k++) {
            MOTN_Key* current = &channel->keys[k];
            current->type = (data[types + k / 4] >> (6 - 2 * (k % 4))) & 3;
            if (values + current->type * 2u > size)
                return false;
            float stored[3] = { 0 };
            for (int v = 0; v < current->type; v++)
                stored[v] = Bytes_ReadF16(data + values + v * 2);
            values += current->type * 2u;
            if (current->type > 0)
                current->value = stored[current->type - 1];
            if (current->type >= 2)
                current->in = current->out = stored[0];
            if (current->type == 3)
                current->out = stored[1];
        }
        types += (channel->keyCount + 3) / 4;
    }
    return true;
}

bool MOTN_ReadMotion(const MOTN_Bank* bank, uint32_t index, MOTN_Motion* motion) {
    memset(motion, 0, sizeof(MOTN_Motion));
    if (index >= bank->count || bank->offsets[index] == 0 || bank->offsets[index] + 4 > bank->size)
        return false;
    const uint8_t* data = bank->data + bank->offsets[index];
    uint32_t word = Bytes_ReadU32(data);
    uint32_t type = word >> 16;
    motion->frames = word & MOTN_FRAMES_MASK;
    if (type == MOTN_SAMPLED_TYPE) {
        motion->kind = MOTN_SAMPLED;
        return true;
    }
    motion->type = type & ~MOTN_RAW_FLAG;
    if (type & MOTN_RAW_FLAG) {
        motion->kind = MOTN_RAW;
        return true;
    }
    motion->kind = MOTN_KEYFRAME;
    if (!ReadKeyframes(data, bank->size - bank->offsets[index], (word & MOTN_WIDE) != 0, motion)) {
        fprintf(stderr, "[MOTN_ReadMotion]: Invalid motion %u (%s)\n", index, bank->names[index]);
        MOTN_FreeMotion(motion);
        return false;
    }
    return true;
}

const MOTN_Channel* MOTN_FindChannel(const MOTN_Motion* motion, uint32_t bone, MOTN_Axis axis) {
    for (uint32_t i = 0; i < motion->channelCount; i++) {
        if (motion->channels[i].bone == bone && motion->channels[i].axis == axis)
            return &motion->channels[i];
    }
    return NULL;
}

// Cubic Hermite curve between two keys, with the tangents taken from units per second to the length of the segment
float MOTN_ChannelValue(const MOTN_Channel* channel, float frame) {
    const MOTN_Key* keys = channel->keys;
    if (channel->keyCount == 0)
        return 0.0f;
    if (frame <= keys[0].frame)
        return keys[0].value;
    for (uint32_t k = 0; k + 1 < channel->keyCount; k++) {
        const MOTN_Key* a = &keys[k];
        const MOTN_Key* b = &keys[k + 1];
        if (frame >= b->frame)
            continue;
        float length = (float)(b->frame - a->frame);
        if (length <= 0.0f)
            return b->value;
        float t = (frame - a->frame) / length;
        float t2 = t * t, t3 = t2 * t;
        float scale = length / MOTN_FRAME_RATE;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * a->value + (t3 - 2.0f * t2 + t) * a->out * scale +
            (-2.0f * t3 + 3.0f * t2) * b->value + (t3 - t2) * b->in * scale;
    }
    return keys[channel->keyCount - 1].value;
}

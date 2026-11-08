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

// Header of a raw motion: the first word, the size of each frame, a mask of 48 bones (3 uint16, bit b % 16 of word
// b / 16 for the bone b) and 0. Each frame holds the position (float16) and the rotation (int16, 0x10000 = 1 turn) of
// the hips, then the rotation of each bone of the mask, in turns, with 3 values (X, Y, Z) or one for the knees and the
// elbows, which bend around Z
#define MOTN_RAW_HEADER     14
#define MOTN_RAW_BONES      48
#define MOTN_RAW_HIPS       6
static const uint8_t g_rawHinges[] = { 7, 14, 28, 34 };

// Header of a sampled motion: the first word, 0x3C00, 0, the number of nodes and 0, then 32 bytes per node: a uint16
// (usually 0), the scale (float16 X, Y, Z) and 6 channels (rotation X, Y, Z and position X, Y, Z) of a uint16 offset of
// the samples from the start of the motion (0 if the channel is constant) and a uint16 value. The rotations are int16
// (0x10000 = 1 turn), the positions float16, with frames + 1 samples
#define MOTN_SAMPLED_HEADER 12
#define MOTN_SAMPLED_NODE   32

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
    uint32_t lastID2 = Bytes_ReadU32(data + 0x18); // 0 in motion/motion.bin
    if (lastID <= bank->firstID || (lastID2 != lastID && lastID2 != 0))
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

// Reads the table of the data of the bank, which it owns. Frees it if it is not a valid MOTN file
static bool ReadBank(MOTN_Bank* bank, const char* name) {
    if (!ReadTable(bank)) {
        fprintf(stderr, "[MOTN_Load]: Invalid MOTN file %s\n", name);
        MOTN_Free(bank);
        return false;
    }
    return true;
}

bool MOTN_Load(const char* filepath, MOTN_Bank* bank) {
    memset(bank, 0, sizeof(MOTN_Bank));
    bank->data = File_LoadToMemoryDecompressed(filepath, &bank->size);
    return bank->data != NULL && ReadBank(bank, filepath);
}

bool MOTN_LoadFromMemory(const uint8_t* data, size_t size, const char* name, MOTN_Bank* bank) {
    memset(bank, 0, sizeof(MOTN_Bank));
    bank->data = malloc(size + 1);
    if (bank->data == NULL)
        return false;
    memcpy(bank->data, data, size);
    bank->size = size;
    return ReadBank(bank, name);
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

static bool IsRawHinge(uint32_t bone) {
    for (size_t i = 0; i < sizeof(g_rawHinges); i++) {
        if (g_rawHinges[i] == bone)
            return true;
    }
    return false;
}

static float RawAngle(const uint8_t* data) {
    return (int16_t)Bytes_ReadU16(data) / 65536.0f;
}

// Reads a raw motion that starts at data and has size bytes up to the end of the file: one key per frame for each
// channel. frames is the number of frames stored
static bool ReadRaw(const uint8_t* data, size_t size, uint32_t frames, MOTN_Motion* motion) {
    if (size < MOTN_RAW_HEADER || frames == 0)
        return false;
    uint32_t frameSize = Bytes_ReadU16(data + 4);
    uint32_t values = frameSize / 2;
    if (frameSize % 2 != 0 || values < MOTN_RAW_HIPS || MOTN_RAW_HEADER + (size_t)frames * frameSize > size)
        return false;

    // The channels: the hips, then the bones of the mask
    uint32_t count = MOTN_RAW_HIPS;
    for (uint32_t bone = 0; bone < MOTN_RAW_BONES; bone++) {
        if ((Bytes_ReadU16(data + 6 + bone / 16 * 2) >> (bone % 16)) & 1)
            count += IsRawHinge(bone) ? 1 : 3;
    }
    // The animals have other knees (e.g. the cat of EXB_CAT_CAT_STAND): their values are left undecoded
    if (count != values)
        return true;
    motion->channels = calloc(count, sizeof(MOTN_Channel));
    motion->keys = calloc((size_t)count * frames, sizeof(MOTN_Key));
    if (motion->channels == NULL || motion->keys == NULL)
        return false;
    for (int axis = 0; axis < MOTN_AXES; axis++)
        motion->channels[motion->channelCount++] = (MOTN_Channel) { 0, (uint8_t)axis, 0, NULL };
    for (uint32_t bone = 1; bone < MOTN_RAW_BONES; bone++) {
        if (!((Bytes_ReadU16(data + 6 + bone / 16 * 2) >> (bone % 16)) & 1))
            continue;
        for (int axis = IsRawHinge(bone) ? MOTN_ROTATION_Z : MOTN_ROTATION_X; axis <= MOTN_ROTATION_Z; axis++)
            motion->channels[motion->channelCount++] = (MOTN_Channel) { (uint8_t)bone, (uint8_t)axis, 0, NULL };
    }

    for (uint32_t c = 0; c < count; c++) {
        MOTN_Channel* channel = &motion->channels[c];
        channel->keyCount = frames;
        channel->keys = motion->keys + (size_t)c * frames;
        for (uint32_t f = 0; f < frames; f++) {
            const uint8_t* value = data + MOTN_RAW_HEADER + (size_t)f * frameSize + c * 2;
            channel->keys[f] = (MOTN_Key) { (uint16_t)f, 1, c < 3 ? Bytes_ReadF16(value) : RawAngle(value), 0.0f, 0.0f };
        }
    }
    // The last frame, as in the keyframe motions
    motion->frames = frames - 1;
    return true;
}

// Value of a channel of a sampled motion: rotations (channels 0 to 2) in turns, positions (3 to 5) in float16
static float SampledValue(const uint8_t* data, int channel) {
    return channel < 3 ? RawAngle(data) : Bytes_ReadF16(data);
}

// Reads a sampled motion that starts at data and has size bytes up to the end of the file: a channel per axis of each
// node, the "bone" being the index of the node in the model, with one key per frame, or one key if it is constant.
// A few motions don't follow the layout (e.g. motion 22 of M_01JAN): they are left undecoded
static bool ReadSampled(const uint8_t* data, size_t size, MOTN_Motion* motion) {
    if (size < MOTN_SAMPLED_HEADER)
        return false;
    uint32_t nodes = Bytes_ReadU16(data + 8);
    size_t table = MOTN_SAMPLED_HEADER + (size_t)nodes * MOTN_SAMPLED_NODE;
    uint32_t samples = motion->frames + 1;
    if (nodes == 0 || nodes > 256 || table > size)
        return true;
    uint32_t keyCount = 0;
    for (uint32_t n = 0; n < nodes; n++) {
        for (int c = 0; c < MOTN_AXES; c++) {
            uint32_t offset = Bytes_ReadU16(data + MOTN_SAMPLED_HEADER + n * MOTN_SAMPLED_NODE + 8 + c * 4);
            if (offset != 0 && (offset < table || offset + (size_t)samples * 2 > size))
                return true;
            keyCount += offset != 0 ? samples : 1;
        }
    }
    motion->channels = calloc((size_t)nodes * MOTN_AXES, sizeof(MOTN_Channel));
    motion->keys = calloc(keyCount, sizeof(MOTN_Key));
    if (motion->channels == NULL || motion->keys == NULL)
        return false;
    MOTN_Key* key = motion->keys;
    for (uint32_t n = 0; n < nodes; n++) {
        for (int c = 0; c < MOTN_AXES; c++) {
            const uint8_t* entry = data + MOTN_SAMPLED_HEADER + n * MOTN_SAMPLED_NODE + 8 + c * 4;
            uint32_t offset = Bytes_ReadU16(entry);
            // The channels of the file are the rotations then the positions
            uint8_t axis = (uint8_t)(c < 3 ? MOTN_ROTATION_X + c : MOTN_POSITION_X + c - 3);
            MOTN_Channel* channel = &motion->channels[motion->channelCount++];
            *channel = (MOTN_Channel) { (uint8_t)n, axis, offset != 0 ? samples : 1, key };
            for (uint32_t f = 0; f < channel->keyCount; f++)
                key[f] = (MOTN_Key) { (uint16_t)f, 1, SampledValue(offset != 0 ? data + offset + f * 2 : entry + 2, c), 0.0f, 0.0f };
            key += channel->keyCount;
        }
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
    size_t size = bank->size - bank->offsets[index];
    bool read;
    if (type == MOTN_SAMPLED_TYPE) {
        motion->kind = MOTN_SAMPLED;
        read = ReadSampled(data, size, motion);
    }
    else {
        motion->type = type & ~MOTN_RAW_FLAG;
        motion->kind = type & MOTN_RAW_FLAG ? MOTN_RAW : MOTN_KEYFRAME;
        read = motion->kind == MOTN_RAW ? ReadRaw(data, size, motion->frames, motion) :
            ReadKeyframes(data, size, (word & MOTN_WIDE) != 0, motion);
    }
    if (!read) {
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

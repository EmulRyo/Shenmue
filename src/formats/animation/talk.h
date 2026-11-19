#ifndef __TALK_H__
#define __TALK_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/model/ftbl.h"

// TALK: the poses of the faces (talk_*.bin in model/face), a set or a chain of sets. A set has, for each of the 75
// channels (X, Y and Z of the 25 bones of the FTBL files), keys from pose 0 to the last one: the position of the tip of
// the bone, with the slopes of a Hermite curve between the keys. Each channel belongs to one of two parts of the face
// that move independently, the eyes or the mouth. The poses are in groups of 6 per expression: 0 and 1 the eyes open
// and closed, 0 to 5 the shapes of the mouth of the lip-sync tables

#define TALK_CHANNELS       (FTBL_BONES * 3)
#define TALK_MAX_SETS       8
#define TALK_POSES_PER_EXPRESSION 6

typedef enum {
    TALK_LAYER_EYES  = 0,
    TALK_LAYER_MOUTH = 1,
} TALK_Layer;

typedef struct {
    uint32_t        poseCount;
    uint32_t        keyCounts[TALK_CHANNELS];
    const uint8_t*  keys[TALK_CHANNELS];    // 8 bytes per key: uint16 pose, slopes in and out, value (TALK_Float)
    uint8_t         layers[TALK_CHANNELS];  // TALK_Layer
} TALK_Set;

typedef struct {
    uint8_t*        buffer;
    size_t          size;
    uint32_t        setCount;
    TALK_Set        sets[TALK_MAX_SETS];
} TALK_File;

bool TALK_Load(const char* filepath, TALK_File* file);
void TALK_Free(TALK_File* file);
// The 16-bit floats of the keys: sign, 6-bit exponent (from -32), 9-bit mantissa
float TALK_Float(uint16_t value);
// Value of a channel at a pose, as the game computes it (code at 0x0C0900B8 of 1ST_READ.BIN, PAL v1.001)
float TALK_Value(const TALK_Set* set, uint32_t channel, int32_t pose);
// Offsets of the bones of a face skin in a pose, for the bones of one part of the face (the others are left as they
// are): each bone is a chain of two segments from its root, whose end goes to the position of the pose (as far as it
// reaches); the offset is from the tip of the bone. Returns false if the set has no channel of that part
bool TALK_BoneOffsets(const TALK_Set* set, const FTBL_Skin* skin, TALK_Layer layer, int32_t pose, float offsets[FTBL_BONES][3]);

#endif

#ifndef __HMOT_H__
#define __HMOT_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HMOT: motions of the detailed hands (hmot_*.bin, hmot<nnnn>.bin in the folders of the maps), numbered from the first
// ID of the file to the last one. A motion has 3 channels (angles around X, Y and Z in degrees) per node of the skeleton
// of an HM file, with keys on frames interpolated by Hermite curves; a node without keys on a channel keeps its angle at
// rest

typedef struct {
    int32_t     frame;
    float       slopeIn;    // Per frame
    float       slopeOut;
    float       value;      // Degrees
} HMOT_Key;

typedef struct {
    char        name[33];   // e.g. "HMN_YKA_GOO_LP"
    uint32_t    id;
    int32_t     frames;
    uint32_t    nodeCount;  // 71
    uint32_t*   keyCounts;  // Per channel (3 per node), 0 for none
    HMOT_Key**  keys;
} HMOT_Motion;

typedef struct {
    uint32_t        firstId;
    uint32_t        motionCount;
    HMOT_Motion*    motions;
} HMOT_File;

// Loads an HMOT file. Returns false if it isn't valid
bool HMOT_Load(const char* filepath, HMOT_File* file);
bool HMOT_LoadFromMemory(const uint8_t* data, size_t size, const char* name, HMOT_File* file);
void HMOT_Free(HMOT_File* file);
// The motion of an ID, NULL if the file doesn't have it
const HMOT_Motion* HMOT_Find(const HMOT_File* file, uint32_t id);
// The value of a channel (3 x node + axis) at a frame in degrees, or rest if the channel has no keys (code at 0x0C0904E0
// of 1ST_READ.BIN)
float HMOT_Value(const HMOT_Motion* motion, uint32_t channel, float frame, float rest);

#endif

#ifndef __MOTN_H__
#define __MOTN_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// MOTN: a bank of motions of characters (no magic). The motion i has the ID firstID + 1 + i, the number the cutscenes
// and the character scripts play it by. The keyframe motions are curves of keys for each channel (one axis of the
// position or the rotation of a bone); the raw and the sampled motions store the value of each channel at every frame,
// read as one key per frame. The "bones" of the sampled motions are the nodes of the model, in the order of the file

#define MOTN_FRAME_RATE 30.0f // The tangents of the keys are in units per second

typedef struct {
    uint32_t        firstID;
    uint32_t        count;
    const char**    names;   // Point into data
    uint32_t*       offsets; // Of the data of each motion, from the start of the file
    uint8_t*        data;
    size_t          size;
} MOTN_Bank;

typedef enum {
    MOTN_KEYFRAME,
    MOTN_SAMPLED,   // Values of the nodes of the model for every frame (objects and animals)
    MOTN_RAW,       // The values of the channels at every frame, with the bend of the knees and the elbows
} MOTN_Kind;

// Axis of a channel
typedef enum {
    MOTN_POSITION_X,
    MOTN_POSITION_Y,
    MOTN_POSITION_Z,
    MOTN_ROTATION_X, // Turns (1 = 360 degrees)
    MOTN_ROTATION_Y,
    MOTN_ROTATION_Z,
    MOTN_AXES,
} MOTN_Axis;

typedef struct {
    uint16_t        frame;
    uint8_t         type;   // Values stored: 0 none (the value is 0), 1 the value, 2 a tangent and the value, 3 two
    float           value;
    float           in;     // Tangents in units per second, 0 for the keys of type 0 and 1
    float           out;
} MOTN_Key;

typedef struct {
    uint8_t         bone;
    uint8_t         axis;   // MOTN_Axis
    uint32_t        keyCount;
    MOTN_Key*       keys;   // Point into the keys of the motion
} MOTN_Channel;

typedef struct {
    MOTN_Kind       kind;
    uint32_t        frames;     // Last frame: the keys go from 0 to frames
    uint32_t        type;       // Character type of the keyframe and raw motions
    uint32_t        channelCount;
    MOTN_Channel*   channels;   // Empty if the layout of the motion is not known
    MOTN_Key*       keys;
} MOTN_Motion;

// Loads the names and the table of the motions of a MOTN file (gzipped or not). Returns false if it is not a valid MOTN
// file
bool MOTN_Load(const char* filepath, MOTN_Bank* bank);
void MOTN_Free(MOTN_Bank* bank);
// Name of the motion with this ID, NULL if it is not in the bank
const char* MOTN_FindName(const MOTN_Bank* bank, uint32_t id);
// Index of the motion with this ID, -1 if it is not in the bank
int32_t MOTN_FindMotion(const MOTN_Bank* bank, uint32_t id);

// Decodes a motion of the bank. Returns false if its data is not valid
bool MOTN_ReadMotion(const MOTN_Bank* bank, uint32_t index, MOTN_Motion* motion);
void MOTN_FreeMotion(MOTN_Motion* motion);
// Channel of an axis of a bone, NULL if the motion doesn't animate it
const MOTN_Channel* MOTN_FindChannel(const MOTN_Motion* motion, uint32_t bone, MOTN_Axis axis);
// Value of a channel at a frame (Hermite curves between the keys), clamped to its first and last key
float MOTN_ChannelValue(const MOTN_Channel* channel, float frame);

#endif

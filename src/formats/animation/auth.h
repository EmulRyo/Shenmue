#ifndef __AUTH_H__
#define __AUTH_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// AUTH: a cutscene (magic "TRCK"). A timeline of commands at given frames (30 per second), the curves of the camera and
// of the movement of each character (in seconds), and the names of the sounds played by the commands

#define AUTH_FRAME_RATE         30.0f
#define AUTH_CAMERA_CURVES      8
#define AUTH_CHARACTER_CURVES   9

typedef enum {
    AUTH_COMMAND_START      = 1, // "(^^;", 0
    AUTH_COMMAND_CHARACTER  = 2, // Character ID, index
    AUTH_COMMAND_MOTION     = 3, // Character ID, motion ID, first frame, last frame, 0
    AUTH_COMMAND_UNKNOWN    = 4, // Character ID, 0 to 12, 0 to 6, 1 to 49
    AUTH_COMMAND_VOICE      = 5, // Character ID, sound, -1, -1, length in seconds
    AUTH_COMMAND_SOUND      = 6, // Character ID, sound ID, sound; with 9 arguments: character ID, 0x7F, 4, 0x10, 0,
                                 // length in seconds
} AUTH_Opcode;

typedef enum {
    AUTH_CAMERA_X, AUTH_CAMERA_Y, AUTH_CAMERA_Z,
    AUTH_CAMERA_TARGET_X, AUTH_CAMERA_TARGET_Y, AUTH_CAMERA_TARGET_Z,
    AUTH_CAMERA_ROLL,   // Probably the roll, in degrees
    AUTH_CAMERA_FOV,    // Degrees
} AUTH_CameraCurve;

typedef enum {
    AUTH_CHARACTER_X, AUTH_CHARACTER_Y, AUTH_CHARACTER_Z,
    AUTH_CHARACTER_ROTATION_X, AUTH_CHARACTER_ROTATION_Y, AUTH_CHARACTER_ROTATION_Z, // Degrees
    // Curves 6 to 8 are unknown, usually empty
} AUTH_CharacterCurve;

// Keys of an animated value. Two keys with the same time make a jump (the cuts of the camera)
typedef struct {
    uint32_t        count;
    const float*    times;      // Seconds, growing
    const float*    values;
    const float*    tangents;   // Unknown, probably the slope at each key
} AUTH_Curve;

typedef struct {
    uint32_t    frame;
    uint8_t     opcode;
    uint8_t     argCount;
    const uint32_t* args;
} AUTH_Command;

typedef struct {
    char        id[5];  // e.g. "AKIR"
    uint32_t    idValue;
    AUTH_Curve  curves[AUTH_CHARACTER_CURVES]; // All empty if the file has no movement for the character
} AUTH_Character;

typedef struct {
    uint32_t        duration;   // Frames
    uint32_t        commandCount;
    AUTH_Command*   commands;
    AUTH_Curve      camera[AUTH_CAMERA_CURVES];
    uint32_t        characterCount;
    AUTH_Character* characters;
    uint32_t        soundCount;
    const char**    sounds;     // Paths of the .aiff files of the authoring tool
    uint8_t*        data;       // The whole file: the arguments, the keys and the sound names point into it
    size_t          size;
} AUTH_Sequence;

// Loads an AUTH file (gzipped or not). Returns false if it is not a valid AUTH file
bool AUTH_Load(const char* filepath, AUTH_Sequence* sequence);
void AUTH_Free(AUTH_Sequence* sequence);
// Value of a curve at a time in seconds, interpolated linearly between the keys. fallback if the curve has no key
float AUTH_CurveValue(const AUTH_Curve* curve, float time, float fallback);
// Index of the character with this ID (the first argument of the commands), -1 if none
int AUTH_FindCharacter(const AUTH_Sequence* sequence, uint32_t idValue);
// Argument of a command as a float
float AUTH_ArgFloat(const AUTH_Command* command, uint32_t arg);

#endif

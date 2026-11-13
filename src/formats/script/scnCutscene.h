#ifndef __SCNCUTSCENE_H__
#define __SCNCUTSCENE_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/script/scnBytecode.h"
#include "formats/script/scnVm.h"

// What a scene program does while one of the cutscenes of its table plays, found by the run with time of its bytecode
// (scnVm.h): the program is emulated up to its call that plays the slot of the cutscene (Cutscene, 0x050, with the slot;
// after the load of the AUTH file in the slot, LoadSequence 0x13E, when the file is given), and the function of that
// call runs again from its start, then frame after frame with the threads it starts, while Cutscene(-1) returns 1 (the
// frames of the cutscene, 30 per second). Of its calls to the game:
//   NodeTransform (0x0C9): (character, node, &position, &rotation, mode) moves a node of the model of a character (the
//                 doors and the wheels of the bus of scene/01/d000): the node whose ID has node in its low 16 bits. Mode
//                 0 sets its position (3 floats) and its rotation (3 integers, 0x10000 = 360°), 1 adds them, 2 reads
//                 them, 3 sets its scale (3 floats at the position argument). A NULL address leaves the value
//   SoundCommand  (0x06C): (command, 0, 0) plays a sequence of the sound bank of the cutscene, with the IDs of the sound
//                 commands of the AUTH files (group in the low 16 bits, index of the sequence in the high 16 bits)

#define SCNCUTSCENE_PLAY        0x050   // (slot): plays a cutscene; (-1): 1 while it plays
#define SCNCUTSCENE_SEQUENCE    0x13E   // (0, slot, file, package): loads an AUTH file in a slot
#define SCNCUTSCENE_NODE        0x0C9   // (character, node, &position, &rotation, mode)
#define SCNCUTSCENE_SOUND       0x06C   // (command, channel, value)
#define SCNCUTSCENE_NODE_SET    0
#define SCNCUTSCENE_NODE_ADD    1
#define SCNCUTSCENE_NODE_READ   2
#define SCNCUTSCENE_NODE_SCALE  3

typedef enum {
    SCNCUTSCENE_ROTATION,   // Integers, 0x10000 = 360°
    SCNCUTSCENE_POSITION,   // Floats, relative to the parent node
    SCNCUTSCENE_SCALE,      // Floats
} SCNCutscene_Field;

// A call that sets or adds a value of a node
typedef struct {
    uint32_t    frame;
    uint32_t    address;    // Of the call, in the bytecode
    uint32_t    character;  // ID (e.g. 'BUS_' = 0x5F535542)
    uint16_t    node;       // Low 16 bits of the ID of the node
    uint8_t     field;      // SCNCutscene_Field
    bool        add;        // Added to the value, else set
    uint32_t    values[3];  // X, Y, Z: integers for the rotation, bits of floats for the others
    uint8_t     known;      // Bit i: values[i] is known (an unknown value leaves the value of the node)
} SCNCutscene_Node;

// A sound command
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    command;
} SCNCutscene_Sound;

typedef struct {
    bool                found;      // The program plays the slot
    uint32_t            function;   // Start of the function that plays it, in the bytecode
    uint32_t            address;    // Of the call that plays it
    VM_TimedResult      run;
    uint32_t            nodeCount;
    SCNCutscene_Node*   nodes;      // In the order of the calls (the frames grow)
    uint32_t            soundCount;
    SCNCutscene_Sound*  sounds;
} SCNCutscene;

// The transform of a node: the values of its MT5 node
typedef struct {
    int32_t     rotation[3];
    float       position[3];
    float       scale[3];
} SCNCutscene_Transform;

// Runs the program for the cutscene of a slot of the table, of frames frames. file: the AUTH file loaded in the slot
// (e.g. "seqdata2.bin"), NULL if not known or not loaded by the program (a cutscene of the SCEX chunk). Returns false
// on memory error (found is false if the program doesn't play the slot)
bool SCNCutscene_Run(const BC_Program* program, uint32_t slot, const char* file, uint32_t frames, SCNCutscene* cutscene);
void SCNCutscene_Free(SCNCutscene* cutscene);
// The program moves nodes of the character
bool SCNCutscene_MovesCharacter(const SCNCutscene* cutscene, uint32_t character);
// Applies to a transform (the one of the node in the model) the calls for the node of the character up to a frame
// (included). Returns false if none moves it
bool SCNCutscene_NodeAt(const SCNCutscene* cutscene, uint32_t character, uint16_t node, uint32_t frame,
    SCNCutscene_Transform* transform);

#endif

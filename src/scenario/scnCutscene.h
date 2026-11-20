#ifndef __SCNCUTSCENE_H__
#define __SCNCUTSCENE_H__

#include <stdbool.h>
#include <stdint.h>
#include "scenario/scnBytecode.h"
#include "scenario/scnVm.h"

// What a scene program does while one of the cutscenes of its table plays, found by the run with time of its bytecode
// (scnVm.h): the program is emulated up to its call that plays the slot of the cutscene (Cutscene, 0x050, with the slot;
// after the load of the AUTH file in the slot, LoadSequence 0x13E, when the file is given), and the function of that
// call runs again from its start, then frame after frame with the threads it starts, while Cutscene(-1) returns 1 (the
// frames of the cutscene, 30 per second). The emulation starts from the entry point, or else from the functions that
// give the slot to a call that plays it; then, as long as it still gets to the call, from the functions that call that
// one (or start it as a thread), up to SCNCUTSCENE_MAX_LEVELS levels up, so that what the program does before it plays
// the cutscene is run (e.g. the vending machine JIHN of scene/01/stream/vend.pkf placed at the machine VM_0 of the map
// by the event of the machine before the cutscene 22 of scene/01/d000). On the way, the emulation keeps what the program
// sets of its characters (positions, angles, display and active flags) and gives it back to GetPosition and GetAngle;
// Ryo's position is that of the start of the cutscene (the program decides with it, e.g. where the machines VM_0 and
// VM_1 are). Of its calls to the game:
//   NodeTransform (0x0C9): (character, node, &position, &rotation, mode) moves a node of the model of a character (the
//                 doors and the wheels of the bus of scene/01/d000): the node whose ID has node in its low 16 bits. Mode
//                 0 sets its position (3 floats) and its rotation (3 integers, 0x10000 = 360°), 1 adds them, 2 reads
//                 them, 3 sets its scale (3 floats at the position argument). A NULL address leaves the value
//   SoundCommand  (0x06C): (command, slot, value) a command to the sound driver, its operation in the low byte:
//                 0xA8 plays a music sequence and 0xA9 or 0xAB an effect sequence (the low 16 bits, e.g. 0x3FA8 or
//                 0x04A9, are the ID of the group of the sequence in the loaded banks, the high 16 bits its index in the
//                 group, as the sound IDs of the AUTH files: 0x1D04A9 is the effect 29 of a1_busno.snd); 0xA0 sets a
//                 value of the bank of a slot (bits 8 to 15: 0x04 its volume, 0 to 127; 0x0A fades it out in value
//                 frames, e.g. 0xAA0 (2, 30) at the end of a cutscene)
//   SoundBank     (0x16D): (0, slot, name, 0) loads a SND bank of the sound folder of the scene in a slot ("FREE" empties
//                 it, "SELECT" loads the music chosen by the game); SoundBanks (0x0F3): the banks of the 8 slots at
//                 once, a name per argument (0 keeps the slot). The music is usually in the slot 2, the ambient sounds
//                 (amb*.snd) in the slots 1 and 3
//   SetPosition   (0x018) and SetAngle (0x01D): (character, flags, &vector) place a character of the map or of the
//                 package of the cutscene that the AUTH file doesn't declare (e.g. the phone books TBK1 and TBK2 of the
//                 cutscene of scene/01/d000/auth.pks). The flags 0x20000000, 0x10000000 and 0x08000000 select X, Y and Z,
//                 0x80000000 adds the vector. The angles are integers (0x10000 = 360°). The flag 0x40000000 transforms
//                 the vector by the character the character is attached to (the same values when it is attached to
//                 none). The calls before the cutscene that leave a known value are given at the frame 0
//   GetPosition   (0x019) and GetAngle (0x01C): the vector set by the program for the character, while it isn't
//                 attached (0x0E6) nor moved by the AUTH file during the cutscene; else unknown (e.g. the characters
//                 that follow Ryo and his shadow in the cutscene 51 of scenario/act01_jomo.scn). Of a node (argument 1
//                 other than -1): unknown
//   CharacterDisplay (0x01F, 1 shows, 2 hides) and CharacterActive (0x0A8, 1 wakes, 0 puts to sleep): the characters
//                 drawn (e.g. VM_0 hidden and JIHN shown for the cutscene 22 of scene/01/d000, JIHN put to sleep at
//                 its frame 1600). The calls before the cutscene that leave a known flag are given at the frame 0
//   Attach        (0x0E6): (character, to character, node, &position, &angles) attaches a character to a node of another
//                 one (the node whose ID has node in its low 16 bits; the character itself if it has none), at a position
//                 (3 floats) and angles (3 integers, 0x10000 = 360°) relative to it: the can CAN1 in the hand (node 18)
//                 of Ryo, then of YKHI, and the letter MALS in the hand of Ryo in the cutscene 22 of scene/01/d000
//   Detach        (0x01B): (character) detaches it: it stays where it was
//   CharacterOpacity (0x13A): (character, opacity) the opacity of a character (a float, 1 opaque, 0 invisible: the float
//                 at 0x48 of the character): the characters of the map that fade out and those of the vision that fade
//                 in (the stars HOSI to 0.8, the constellations SEZ1 and SEZ2 to 0.3) in the cutscene of seqdata0.bin of
//                 scene/02/toki
//   MapModelDisplay (0x098): (slot, 1 / 0) shows or hides the MAP model of the slot (scnDisplay.h), and MapModelOpacity
//                 (0x1A3): (slot, opacity) its opacity (a float, at most 1, at 0x58 of the 0x60 bytes of the slot): the
//                 map of scene/02/toki fades out from the frame 1800 to 1900 of that cutscene, is hidden, and fades in
//                 from 2520 to 2580. The calls that don't change the value are not kept
//   LightGroup    (0x10F): (group) lights the map with the group of lights of that number, the inner LGHT chunk of
//                 the MAPINFO file; LightControl (0x066): (light, operation, value) changes a light of the group (0 its
//                 type, 4 a float of the light, e.g. faded out from 0.97 to 0); AmbientColor (0x064): (&values) the 4
//                 floats of the ambient color, as in the header of the LGHT chunks (0.3, then red, green and blue). In
//                 the cutscene of seqdata0.bin of scene/02/toki the group 1 lights the vision from the frame 1900, the
//                 ambient color goes from blue to the brown of the shop and the light 4 fades out from 2501 to 2530,
//                 and the group 0 is back at 2640. The calls that leave the same values are not kept
//   FaceAnimation (0x075), Eyelids (0x094), EyesLook (0x099) and FaceExpression (0x113): (character, three values)
//                 the face of a character (see the MAPINFO page of the wiki); EyesLook (character, 2, &point, frames)
//                 turns the eyes to a point (3 floats, in the space of the map) in a number of frames, (character, 0)
//                 back to the front
//   HandMotion (0x0DF) and PlayHandMotion (0x0E7): the detailed hands of a character, shown or not, and the motions of
//                 the HMOT files played on them
// The packages of the characters: a function of the program that loads a package (0x13C (0, 0, folder, name)) named by
// one of its parameters until the character named by another one exists (0x166 (1, character)), called with the values
// of two variables of the program (e.g. func(var_118, var_11C) of scenario/act01_d000.scn, the can CAN1 of the drink
// chosen at the vending machine), loads that package for that character: the one of the values that the variables have
// at the start of the cutscene (SPRT, the drink set by the program before the choice of the player)

#define SCNCUTSCENE_PLAY        0x050   // (slot): plays a cutscene; (-1): 1 while it plays
#define SCNCUTSCENE_SEQUENCE    0x13E   // (0, slot, file, package): loads an AUTH file in a slot
#define SCNCUTSCENE_NODE        0x0C9   // (character, node, &position, &rotation, mode)
#define SCNCUTSCENE_SOUND       0x06C   // (command, slot, value)
#define SCNCUTSCENE_SOUND_BANK  0x16D   // (0, slot, name, 0)
#define SCNCUTSCENE_SOUND_BANKS 0x0F3   // (8 names)
#define SCNCUTSCENE_SOUND_SLOTS 8
#define SCNCUTSCENE_SOUND_MUSIC 0xA8    // Low byte of a command that plays a music sequence
#define SCNCUTSCENE_SOUND_SLOT  0xA0    // Low byte of a command that sets a value of a slot
#define SCNCUTSCENE_SLOT_VOLUME 0x04    // Bits 8 to 15 of a command of a slot: its volume
#define SCNCUTSCENE_SLOT_FADE   0x0A    // Fade out
#define SCNCUTSCENE_SET_POSITION 0x018 // (character, flags, &position)
#define SCNCUTSCENE_SET_ANGLE   0x01D   // (character, flags, &angles, value)
#define SCNCUTSCENE_GET_POSITION 0x019  // (character, node, &position, flags)
#define SCNCUTSCENE_GET_ANGLE   0x01C   // (character, &angles, flags)
#define SCNCUTSCENE_DISPLAY     0x01F   // (character, 1 shows, 2 hides; 0 reads)
#define SCNCUTSCENE_ACTIVE      0x0A8   // (character, 1 wakes, 0 puts to sleep)
#define SCNCUTSCENE_ATTACH      0x0E6   // (character, to character, node, &position, &angles)
#define SCNCUTSCENE_DETACH      0x01B   // (character)
#define SCNCUTSCENE_OPACITY     0x13A   // (character, opacity)
#define SCNCUTSCENE_MAP_DISPLAY 0x098   // (slot, 1 / 0)
#define SCNCUTSCENE_MAP_OPACITY 0x1A3   // (slot, opacity)
#define SCNCUTSCENE_LIGHT_GROUP 0x10F   // (group)
#define SCNCUTSCENE_LIGHT       0x066   // (light, operation, value)
#define SCNCUTSCENE_AMBIENT     0x064   // (&values)
#define SCNCUTSCENE_LIGHT_INTENSITY 4   // Operation of LightControl: the intensity of the light (its float at 0x18)
#define SCNCUTSCENE_FACE        0x075   // (character, 1 / 0, expression, frames): animates or freezes the face
#define SCNCUTSCENE_EYELIDS     0x094   // (character, mode, frames, lock)
#define SCNCUTSCENE_EYES        0x099   // (character, mode, &point or frames, frames)
#define SCNCUTSCENE_EXPRESSION  0x113   // (character, expression, mouth (pose within the expression, -1 rest), frames)
#define SCNCUTSCENE_EYELIDS_AUTO    0   // Modes of Eyelids: the game blinks by itself
#define SCNCUTSCENE_EYELIDS_CLOSE   1   // Closes the eyes in the frames, and keeps them closed
#define SCNCUTSCENE_EYELIDS_OPEN    2   // Opens them, and keeps them open
#define SCNCUTSCENE_EYELIDS_CLOSED  3   // Closes them (unused by the programs)
#define SCNCUTSCENE_HAND        0x0DF   // (character, on, side, motion): the detailed hand (on 1) or the hand of the body
#define SCNCUTSCENE_HAND_MOTION 0x0E7   // (character, side, motion, first, last, current, flags, speed, blend)
#define SCNCUTSCENE_HAND_LOOP   1       // Flags of the hand motions: loop
#define SCNCUTSCENE_HAND_BACK   2       // Backwards
#define SCNCUTSCENE_EYES_FRONT  0       // Modes of EyesLook: to the front, in 16 frames
#define SCNCUTSCENE_EYES_GAME   1       // To a point chosen by the game
#define SCNCUTSCENE_EYES_POINT  2       // To the point of the call
#define SCNCUTSCENE_PACKAGE     0x13C   // (0, 0, folder, name): loads a package
#define SCNCUTSCENE_EXISTS      0x166   // (1, character): the character exists
#define SCNCUTSCENE_FOLDER_SIZE 48
#define SCNCUTSCENE_NAME_SIZE   16
#define SCNCUTSCENE_RYO         0x52494B41  // 'AKIR'
#define SCNCUTSCENE_MAX_LEVELS  6       // Functions up from the one that gets to the call that plays the slot
#define SCNCUTSCENE_PLACE_SPACE 0x40000000
#define SCNCUTSCENE_PLACE_X     0x20000000
#define SCNCUTSCENE_PLACE_Y     0x10000000
#define SCNCUTSCENE_PLACE_Z     0x08000000
#define SCNCUTSCENE_PLACE_ADD   0x80000000
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

// A call that sets or adds the position or the angles of a character
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    character;
    bool        angle;      // SetAngle, else SetPosition
    uint32_t    flags;      // Argument 1
    uint32_t    values[3];  // Bits of the floats
    uint8_t     known;      // Bit i: values[i] is known and selected by the flags
} SCNCutscene_Place;

// A call that shows, hides, wakes or puts to sleep a character
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    character;
    bool        active;     // CharacterActive, else CharacterDisplay
    bool        on;         // Shown or woken
} SCNCutscene_Display;

// A call that attaches a character to another one or detaches it
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    character;
    uint32_t    parent;     // ID of the character it is attached to, 0 for Detach
    uint16_t    node;       // Low 16 bits of the ID of the node of the parent
    uint32_t    position[3]; // Bits of the floats
    int32_t     angles[3];  // 0x10000 = 360°
    uint8_t     known;      // Bit i: position[i] is known, bit 3 + i: angles[i] (an unknown value is 0)
} SCNCutscene_Attach;

// A call that sets the opacity of a character or of a MAP model, or shows or hides a MAP model
typedef enum {
    SCNCUTSCENE_FADE_CHARACTER,     // CharacterOpacity: target is the ID of the character
    SCNCUTSCENE_FADE_MAP_MODEL,     // MapModelOpacity: target is the slot
    SCNCUTSCENE_SHOW_MAP_MODEL,     // MapModelDisplay: value 1 shows it, 0 hides it
} SCNCutscene_FadeKind;

typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint8_t     kind;       // SCNCutscene_FadeKind
    uint32_t    target;
    float       value;      // Opacity, from 0 to 1
} SCNCutscene_Fade;

// A call that changes the lights: LightGroup, LightControl or AmbientColor
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    function;   // SCNCUTSCENE_LIGHT_GROUP, SCNCUTSCENE_LIGHT or SCNCUTSCENE_AMBIENT
    uint32_t    light;      // Group, or light of the group
    uint32_t    operation;  // Of LightControl
    float       values[4];  // Value of LightControl, the ambient color
    uint8_t     known;      // Bit i: values[i] is known
} SCNCutscene_Light;

// A call that changes the face of a character: FaceAnimation, Eyelids, EyesLook or FaceExpression
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    function;   // SCNCUTSCENE_FACE, SCNCUTSCENE_EYELIDS, SCNCUTSCENE_EYES or SCNCUTSCENE_EXPRESSION
    uint32_t    character;
    uint32_t    values[3];  // Arguments 1 to 3
    float       point[3];   // The point of EyesLook (mode 2)
    uint8_t     known;      // Bit i: values[i] is known; bit 3: point is known
} SCNCutscene_Face;

// A call that changes a hand of a character: HandMotion (0x0DF) shows its detailed hand or the hand of its body and
// loops a motion of the HMOT files on it, PlayHandMotion (0x0E7) plays a motion between two frames
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    function;   // SCNCUTSCENE_HAND or SCNCUTSCENE_HAND_MOTION
    uint32_t    character;
    int32_t     on;         // SCNCUTSCENE_HAND: 1 the detailed hand, 0 the hand of the body
    uint32_t    side;       // 0 left (node 0xFFBE of the body), 1 right (0xFFBF)
    uint32_t    motion;     // ID in the HMOT files
    int32_t     first;      // Frames of the motion plus 1, 0 for its first frame
    int32_t     last;       // 0 for its last frame
    int32_t     current;    // Where it starts, 0 for the first frame (the last one backwards)
    uint32_t    flags;      // SCNCUTSCENE_HAND_LOOP, SCNCUTSCENE_HAND_BACK
    float       speed;      // Frames of the motion per frame
} SCNCutscene_Hand;

// A package that the program loads for a character, with the values of its variables at the start of the cutscene
typedef struct {
    uint32_t    character;
    uint32_t    address;    // Of the call to the function that loads it
    char        folder[SCNCUTSCENE_FOLDER_SIZE]; // e.g. "/scene/01/stream/"
    char        name[SCNCUTSCENE_NAME_SIZE];     // e.g. "SPRT"
} SCNCutscene_Package;

// A sound command
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    command;
    uint32_t    slot;       // Arguments 1 and 2
    uint32_t    value;
    uint8_t     known;      // Bit 0: slot is known, bit 1: value
} SCNCutscene_Sound;

// A sound bank loaded in a slot
typedef struct {
    uint32_t    frame;
    uint32_t    address;
    uint32_t    function;   // SCNCUTSCENE_SOUND_BANK or SCNCUTSCENE_SOUND_BANKS
    uint32_t    slot;
    char        name[SCNCUTSCENE_NAME_SIZE];    // e.g. "bgm013.snd", "FREE" or "SELECT"
} SCNCutscene_Bank;

typedef struct {
    bool                found;      // The program plays the slot
    uint32_t            function;   // Start of the function that plays it, in the bytecode
    uint32_t            address;    // Of the call that plays it
    VM_TimedResult      run;
    uint32_t            nodeCount;
    SCNCutscene_Node*   nodes;      // In the order of the calls (the frames grow)
    uint32_t            soundCount;
    SCNCutscene_Sound*  sounds;
    uint32_t            bankCount;
    SCNCutscene_Bank*   banks;      // In the order of the calls
    uint32_t            placeCount;
    SCNCutscene_Place*  places;     // In the order of the calls
    uint32_t            displayCount;
    SCNCutscene_Display* displays;  // In the order of the calls
    uint32_t            attachCount;
    SCNCutscene_Attach* attaches;   // In the order of the calls
    uint32_t            packageCount;
    SCNCutscene_Package* packages;
    uint32_t            fadeCount;
    SCNCutscene_Fade*   fades;      // In the order of the calls
    uint32_t            lightCount;
    SCNCutscene_Light*  lights;     // In the order of the calls
    uint32_t            faceCount;
    SCNCutscene_Face*   faces;      // In the order of the calls
    uint32_t            handCount;
    SCNCutscene_Hand*   hands;      // In the order of the calls, those with all their arguments known
    uint32_t            levels;     // Functions run before the function of the call that plays the slot
    uint32_t            start;      // Function from which the emulation got to the call
} SCNCutscene;

// What the cutscene tells of the game when the program plays it
typedef struct {
    bool            ryo;            // Ryo (AKIR) is in the cutscene, at ryoPosition
    float           ryoPosition[3]; // At the frame 0
    float           ryoAngle[3];    // Degrees
    uint32_t        characterCount; // Characters of the AUTH file, moved by it during the cutscene
    const uint32_t* characters;
} SCNCutscene_Scene;

// The transform of a node: the values of its MT5 node
typedef struct {
    int32_t     rotation[3];
    float       position[3];
    float       scale[3];
} SCNCutscene_Transform;

// Runs the program for the cutscene of a slot of the table, of frames frames. file: the AUTH file loaded in the slot
// (e.g. "seqdata2.bin"), NULL if not known or not loaded by the program (a cutscene of the SCEX chunk). scene: NULL if
// not known. Returns false on memory error (found is false if the program doesn't play the slot)
bool SCNCutscene_Run(const BC_Program* program, uint32_t slot, const char* file, uint32_t frames,
    const SCNCutscene_Scene* scene, SCNCutscene* cutscene);
void SCNCutscene_Free(SCNCutscene* cutscene);
// The program moves nodes of the character
bool SCNCutscene_MovesCharacter(const SCNCutscene* cutscene, uint32_t character);
// The program places the character (SetPosition or SetAngle)
bool SCNCutscene_PlacesCharacter(const SCNCutscene* cutscene, uint32_t character);
// Applies to the position and the angles (in degrees) of a character the calls that place it up to a frame (included).
// Returns false if none places it
bool SCNCutscene_PlaceAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, float position[3], float angle[3]);
// The program shows, hides, wakes or puts to sleep the character
bool SCNCutscene_DisplaysCharacter(const SCNCutscene* cutscene, uint32_t character);
// Whether the character is drawn at a frame (included) by the calls up to it: shown and awake. Returns false if no call
// sets one of its flags up to it (shown is then left)
bool SCNCutscene_ShownAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, bool* shown);
// The opacity of a character at a frame (CharacterOpacity), returns false if the program doesn't set it up to the frame
bool SCNCutscene_OpacityAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, float* opacity);
// Whether the program shows the MAP model of a slot at a frame, and its opacity: returns false if it doesn't change
// them up to the frame (the values not changed are left)
bool SCNCutscene_MapModelAt(const SCNCutscene* cutscene, uint32_t slot, uint32_t frame, bool* shown, float* opacity);
// The program attaches the character to another one
bool SCNCutscene_AttachesCharacter(const SCNCutscene* cutscene, uint32_t character);
// The last call that attaches or detaches the character up to a frame (included), NULL if none
const SCNCutscene_Attach* SCNCutscene_AttachAt(const SCNCutscene* cutscene, uint32_t character, uint32_t frame);
// The package that the program loads for the character, NULL if none
const SCNCutscene_Package* SCNCutscene_PackageOf(const SCNCutscene* cutscene, uint32_t character);
// Applies to a transform (the one of the node in the model) the calls for the node of the character up to a frame
// (included). Returns false if none moves it
bool SCNCutscene_NodeAt(const SCNCutscene* cutscene, uint32_t character, uint16_t node, uint32_t frame,
    SCNCutscene_Transform* transform);

#endif

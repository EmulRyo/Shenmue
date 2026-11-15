#ifndef __AUTHVIEWINTERNAL_H__
#define __AUTHVIEWINTERNAL_H__

// The state of the view of the cutscenes, shared by its files: authView.c (opening, drawing, tree, timeline and
// properties), authMotions.c, authModels.c, authPose.c and authProgram.c

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "app/gameDB.h"
#include "core/path.h"
#include "core/text.h"
#include "formats/animation/auth.h"
#include "formats/animation/motn.h"
#include "formats/archive/ipac.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/map/mapinfo.h"
#include "formats/script/chrt.h"
#include "game/mapFiles.h"
#include "game/mapProgram.h"
#include "render/freeCamera.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"
#include "render/scene3D.h"
#include "scenario/scnCutscene.h"
#include "ui/properties.h"
#include "ui/tree.h"
#include "views/auth/authSounds.h"

#define AUTHVIEW_LABEL_SIZE     128
#define AUTHVIEW_PATH_SIZE      1024

// A model file, loaded for each character that uses it: each one is in the pose of its motion
typedef struct {
    char            path[AUTHVIEW_PATH_SIZE];
    bool            loaded;
    float           lift;       // Height to raise the model of a person, whose origin is at the hips
    float           top;        // Highest height of the model, for the labels
    MT5Render_Model model;
    bool            body;       // The body of a character, that the motions can pose
    MT5Pose_Body    bodyNodes;
    Matrix*         pose;       // World matrices of the nodes
    bool            posed;      // In the pose of the motion of its character at the current frame
    bool            programPosed; // Nodes moved by the program of the map at the current frame
} AUTHView_Model;

// A motion played by the commands, decoded from the MOTN files
typedef struct {
    uint32_t        id;
    bool            valid;      // Found and decoded
    const char*     name;       // In its bank, NULL if not found
    MOTN_Motion     motion;
} AUTHView_Motion;

// The model of a character: the file of the Image of the character with its ID in the CHRT scripts of the folder of the
// cutscene, in its folder of the scene or in the characters of the map
typedef struct {
    int             model;      // Index in the models, -1 if none
    bool            human;      // Person, whose model is raised to stand at its position
    const char*     source;     // Script where the character was found, NULL if none
} AUTHView_CharacterModel;

// A character that the AUTH file doesn't declare, placed by the program of the map before or while the cutscene plays
// (SetPosition and SetAngle, e.g. the phone books TBK1 and TBK2 of the cutscene of scene/01/d000/auth.pks, or the
// vending machine JIHN of scene/01/stream/vend.pkf of its cutscene 22) or attached to a character of the cutscene
// (Attach, e.g. the can CAN1 and the letter MALS in the hands of Ryo and YKHI in that cutscene): a character of the
// package that the program loads for it (CAN1 of scene/01/stream/sprt.pkf), of the map, of the package of the cutscene
// or of a package of the stream folder of the scene, drawn by the cutscene from the first call that places or attaches
// it while the program doesn't hide it (CharacterDisplay) nor puts it to sleep (CharacterActive)
typedef struct {
    uint32_t        idValue;
    char            id[5];
    int             model;      // Index in the models, -1 if none
    bool            human;
    const char*     source;     // Script where it was found (allocated), NULL if none
    float           position[3]; // Of its script, which the calls change
    float           angle[3];   // Degrees
    IPAC_FileList   files;      // Of its PKF/PKS pair in stream/humans if it isn't extracted, or in stream
} AUTHView_Placed;

// What the program of the map does while the cutscene plays (scnCutscene.h), in the tree: the calls that move a node of
// a character, place a character, show, hide, wake or put to sleep one, or attach or detach one, grouped while they follow each other frame
// after frame, the sound commands and the sound banks loaded. Those before the cutscene are at its frame 0
typedef enum {
    PROGRAM_NODE,
    PROGRAM_PLACE,
    PROGRAM_DISPLAY,
    PROGRAM_ATTACH,
    PROGRAM_SOUND,
    PROGRAM_BANK,
} ProgramKind;

typedef struct {
    uint8_t         kind;       // ProgramKind
    uint32_t        first;      // Index of the first call (of the nodes, the places or the sounds) and of the last one
    uint32_t        last;
    uint32_t        count;
    uint32_t        firstFrame;
    uint32_t        lastFrame;
} AUTHView_ProgramItem;

typedef struct {
    bool            open;
    char            path[1024];  // Of the open file
    AUTH_Sequence   sequence;
    uint32_t        bankCount;
    MOTN_Bank*      banks;      // MOTN files of the folder, for the names of the motions
    uint32_t        motionCount;
    AUTHView_Motion* motions;   // Of the motion commands
    char            (*labels)[AUTHVIEW_LABEL_SIZE]; // Of the commands
    char            (*characterLabels)[AUTHVIEW_LABEL_SIZE];
    bool*           shownCharacters; // Their eye in the tree
    Tree            tree;
    int             current;    // Command of the current frame in the tree, to follow it while playing
    int             selectedCharacter; // -1 if none: the properties are those of the current command
    Properties      properties;
    float           time;       // Seconds
    bool            playing;
    bool            seeking;    // Dragging on the timeline
    bool            jumped;     // The time was changed by a seek, not by playing
    Vector3         center;     // Of the scene
    float           radius;
    FreeCamera     camera;     // Free camera
    char            sceneFolder[AUTHVIEW_PATH_SIZE]; // Folder of the scene of the file (e.g. .../scene/01), empty if none
    char            mapinfoPath[AUTHVIEW_PATH_SIZE]; // mapinfo.bin of the map of the cutscene: of the parent folder of
                                                     // an AUTH file, or the open file for a cutscene of a mapinfo.bin
    bool            hasMapinfo; // mapinfoPath exists
    int             cutscene;   // TRCK chunk of the SCEX chunk of the open mapinfo.bin, -1 for an AUTH file
    MapFiles        mapFiles;   // For a cutscene of a mapinfo.bin: the CHRT, model and MOTN files of its map
    MT5Render       render;
    uint32_t        modelCount;
    AUTHView_Model* models;
    AUTHView_CharacterModel* characterModels;
    IPAC_FileList*  humanFiles; // Of each character: the files of its PKF/PKS pair in stream/humans if it isn't extracted
    MT5Render_Instance* instances; // Of the characters drawn with their model, filled before drawing them
    SCNCutscene     program;    // What the program of the map does with the cutscene (a cutscene of the table of a map)
    uint32_t        programItemCount;
    AUTHView_ProgramItem* programItems;
    char            (*programLabels)[AUTHVIEW_LABEL_SIZE];
    int             selectedProgram; // Item of the program selected, -1 if none
    uint32_t        placedCount;
    AUTHView_Placed* placed;    // Characters placed by the program that the AUTH file doesn't declare
} AUTHView_State;

// The open cutscene (authView.c)
extern AUTHView_State g_auth;

static inline uint32_t CurrentFrame(void) {
    return (uint32_t)(g_auth.time * AUTH_FRAME_RATE + 0.001f);
}

static inline uint32_t Arg(const AUTH_Command* command, uint32_t arg) {
    return arg < command->argCount ? command->args[arg] : 0;
}

// The four characters of an ID value, or its hexadecimal value if they aren't printable
static inline const char* IDText(uint32_t id) {
    char text[5];
    memcpy(text, &id, 4);
    text[4] = '\0';
    for (int i = 0; i < 4; i++) {
        if (text[i] < 0x20 || text[i] > 0x7E)
            return TextFormat("0x%08X", id);
    }
    return TextFormat("%s", text);
}

// The point of three curves (X, Y and Z) at a time
static inline Vector3 CurvePoint(const AUTH_Curve* curves, float time) {
    return (Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
        AUTH_CurveValue(&curves[2], time, 0.0f) };
}

static inline bool HasMovement(const AUTH_Character* character) {
    return character->curves[AUTH_CHARACTER_X].count > 0 || character->curves[AUTH_CHARACTER_Z].count > 0;
}

// authView.c: with movement, and shown in the tree
bool AUTHView_IsCharacterShown(uint32_t character);

// authMotions.c: the MOTN banks and the motions that the commands play
void AUTHMotions_LoadBanks(const char* filepath, const char* package);
bool AUTHMotions_Load(void);    // Returns false on memory error
const MOTN_Motion* AUTHMotions_Find(uint32_t id); // NULL if it wasn't found or decoded

// authModels.c: the models of the characters of the cutscene, and of the characters placed by the program. Return false
// on memory error
bool AUTHModels_LoadCharacters(void);
bool AUTHModels_LoadPlaced(void);

// authPose.c: the poses and the transforms of the characters at the current frame
void AUTHPose_PoseCharacter(uint32_t index);
Matrix AUTHPose_CharacterTransform(uint32_t index);
float AUTHPose_CharacterHeight(uint32_t index);
bool AUTHPose_PlacedDrawn(const AUTHView_Placed* placed, uint32_t frame);
bool AUTHPose_ProgramTransform(const AUTHView_Placed* placed, Matrix* transform);

// authProgram.c: what the program of the map does with the cutscene, and its items in the tree
bool AUTHProgram_Run(int cutscene, const char* file);
void AUTHProgram_Properties(Properties* properties, uint32_t index);

#endif

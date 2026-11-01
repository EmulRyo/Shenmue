#ifndef __CHRT_H__
#define __CHRT_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// CHRT script (magic "CHRS"): commands that place the characters and objects of a scene. Some commands open a block
// with the commands that follow them (DefImage, Character, Entry, and Image when it ends with an int)

#define CHRT_MAX_ARGS 10 // 3 bits per argument type in 32 bits
#define CHRT_OFF_SCENE 999.0f // Positions with a coordinate of 1000 or more are off the scene, where the characters wait

typedef enum {
    CHRT_ARG_END     = 0,
    CHRT_ARG_FLOAT   = 1,
    CHRT_ARG_LITERAL = 2, // 4 characters stored in place (e.g. character ID)
    CHRT_ARG_STRING  = 3, // Offset of the string, relative to the argument
    CHRT_ARG_INT     = 4,
} CHRT_ArgType;

typedef struct {
    uint8_t         type;       // CHRT_ArgType, other values are unknown types (value in i)
    float           f;
    uint32_t        i;
    char            literal[5];
    const char*     string;     // Points into the buffer of the script
} CHRT_Arg;

typedef struct {
    const char*     name;       // Points into the buffer of the script
    uint32_t        argCount;
    CHRT_Arg        args[CHRT_MAX_ARGS];
    int32_t         parent;     // Index of the command of the block that contains the command, -1 at the top level
    uint32_t        depth;      // Number of blocks that contain the command
} CHRT_Command;

// A Character block, read from its commands
typedef struct {
    char            id[5];          // "?" if the Character command has no ID
    const char*     image;          // Name of its first Image, NULL if none. Points into the buffer of the script
    bool            sleep;          // SLEEP flag
    bool            hasPosition;    // Has a Position, in its block or in its first Entry block
    bool            positioned;     // Its last Position is in the scene (see CHRT_OFF_SCENE)
    float           position[3];    // Of the last Position command, of its first Entry if the character isn't
                                    // positioned by its own commands; (0, 0, 0) if none
    float           angle[3];       // Degrees, of the last Angle command; the short form only sets the second one
} CHRT_Character;

typedef struct {
    uint8_t*        buffer;     // Whole file, uncompressed
    size_t          size;
    uint32_t        commandCount;
    CHRT_Command*   commands;
} CHRT_Script;

// Loads a CHRT file (gzipped or not). Returns false if the file is not a valid CHRT file
bool CHRT_Load(const char* filepath, CHRT_Script* script);
// Loads a script stored inside another file (e.g. the CHRD chunk of a mapinfo.bin): data starts with the CHRS block. The
// data is copied. name is used in the error messages. Returns false if it is not a valid script
bool CHRT_LoadFromMemory(const uint8_t* data, size_t size, const char* name, CHRT_Script* script);
void CHRT_Free(CHRT_Script* script);

// Command names are case-insensitive (Image, IMAGE, image), so are some string arguments (DISP, Disp)
bool CHRT_EqualsIgnoreCase(const char* a, const char* b);
bool CHRT_IsCommand(const CHRT_Command* command, const char* name);
// Model file of an image name: the file of the first Image of the DefImage with that name (case-insensitive), e.g.
// "HOS_L.MT5" for DefImage("HOS_L") { Image(1, "HOS_L.MT5") }. Returns NULL if the script has no DefImage with that name
const char* CHRT_ImageFile(const CHRT_Script* script, const char* name);
// Reads the Character block whose command is at index: its ID, image, flags, position and angle
void CHRT_ReadCharacter(const CHRT_Script* script, uint32_t index, CHRT_Character* character);

// Decodes the commands of a CHRT file (gzipped or not) and prints them as a script,
// with the commands of each block indented, e.g.:
//   Character('AKMI', 31)
//       Position(-300, 0, 0)
// Returns false if the file is not a valid CHRT file
bool CHRT_Print(const char* filepath);

#endif

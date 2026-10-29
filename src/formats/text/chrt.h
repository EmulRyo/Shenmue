#ifndef __CHRT_H__
#define __CHRT_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// CHRT script (magic "CHRS"): commands that place the characters and objects of a scene. Some commands open a block
// with the commands that follow them (DefImage, Character, Entry, and Image when it ends with an int)

#define CHRT_MAX_ARGS 10 // 3 bits per argument type in 32 bits

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

typedef struct {
    uint8_t*        buffer;     // Whole file, uncompressed
    size_t          size;
    uint32_t        commandCount;
    CHRT_Command*   commands;
} CHRT_Script;

// Loads a CHRT file (gzipped or not). Returns false if the file is not a valid CHRT file
bool CHRT_Load(const char* filepath, CHRT_Script* script);
void CHRT_Free(CHRT_Script* script);

// Command names are case-insensitive (Image, IMAGE, image), so are some string arguments (DISP, Disp)
bool CHRT_EqualsIgnoreCase(const char* a, const char* b);
bool CHRT_IsCommand(const CHRT_Command* command, const char* name);

// Decodes the commands of a CHRT file (gzipped or not) and prints them as a script,
// with the commands of each block indented, e.g.:
//   Character('AKMI', 31)
//       Position(-300, 0, 0)
// Returns false if the file is not a valid CHRT file
bool CHRT_Print(const char* filepath);

#endif

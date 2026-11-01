#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "core/file.h"
#include "formats/script/chrt.h"

// A CHRT file is a CHRS block with a list of commands followed by a STRG block with the strings.
// Each command is:
//   uint32 name      Offset of the command name, relative to this field
//   uint32 argTypes  Types of the arguments, 3 bits per argument starting at the lowest bits, 0 ends the list
//   uint32 args[]    One 4 bytes value per argument
// Some commands open a block: their last argument is the size in words (4 bytes) of the commands that follow them
typedef struct {
    char        id[4]; // "CHRS"
    uint32_t    size;  // Size of the CHRS block, the STRG block starts here
} CHRT_Header;

#define CHRT_MAX_DEPTH 8

static const char* g_blockCommands[] = { "DefImage", "Character", "Image", "Entry" };

bool CHRT_EqualsIgnoreCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

bool CHRT_IsCommand(const CHRT_Command* command, const char* name) {
    return CHRT_EqualsIgnoreCase(command->name, name);
}

// Last string argument, NULL if none
static const char* StringArg(const CHRT_Command* command) {
    for (uint32_t i = command->argCount; i > 0; i--) {
        if (command->args[i - 1].type == CHRT_ARG_STRING)
            return command->args[i - 1].string;
    }
    return NULL;
}

const char* CHRT_ImageFile(const CHRT_Script* script, const char* name) {
    for (uint32_t c = 0; c < script->commandCount; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != -1 || !CHRT_IsCommand(command, "DefImage") || command->argCount == 0 ||
            command->args[0].type != CHRT_ARG_STRING || !CHRT_EqualsIgnoreCase(command->args[0].string, name))
            continue;
        for (uint32_t i = c + 1; i < script->commandCount && script->commands[i].parent == (int32_t)c; i++) {
            const char* file = StringArg(&script->commands[i]);
            if (CHRT_IsCommand(&script->commands[i], "Image") && file != NULL)
                return file;
        }
    }
    return NULL;
}

static bool IsInScene(const float position[3]) {
    return fabsf(position[0]) < CHRT_OFF_SCENE && fabsf(position[1]) < CHRT_OFF_SCENE && fabsf(position[2]) < CHRT_OFF_SCENE;
}

// Reads the Position and Angle commands of a block. Returns true if it has a Position
static bool ReadPlacement(const CHRT_Script* script, uint32_t block, CHRT_Character* character) {
    bool hasPosition = false;
    for (uint32_t c = block + 1; c < script->commandCount && script->commands[c].depth > script->commands[block].depth; c++) {
        const CHRT_Command* command = &script->commands[c];
        if (command->parent != (int32_t)block || command->argCount == 0 || command->args[0].type != CHRT_ARG_FLOAT)
            continue;
        if (CHRT_IsCommand(command, "Position") && command->argCount == 3) {
            hasPosition = true;
            for (int i = 0; i < 3; i++)
                character->position[i] = command->args[i].f;
        }
        else if (CHRT_IsCommand(command, "Angle") && command->argCount == 1) // Only the rotation around the vertical axis
            character->angle[1] = command->args[0].f;
        else if (CHRT_IsCommand(command, "Angle") && command->argCount == 3) {
            for (int i = 0; i < 3; i++)
                character->angle[i] = command->args[i].f;
        }
    }
    return hasPosition;
}

void CHRT_ReadCharacter(const CHRT_Script* script, uint32_t index, CHRT_Character* character) {
    memset(character, 0, sizeof(*character));
    const CHRT_Command* command = &script->commands[index];
    snprintf(character->id, sizeof(character->id), "%s", command->argCount > 0 && command->args[0].type == CHRT_ARG_LITERAL ? command->args[0].literal : "?");
    int32_t entry = -1;
    bool object = false, hand = false;
    for (uint32_t c = index + 1; c < script->commandCount && script->commands[c].depth > command->depth; c++) {
        const CHRT_Command* child = &script->commands[c];
        if (child->parent != (int32_t)index)
            continue;
        if (CHRT_IsCommand(child, "Image") && character->image == NULL && child->argCount > 0 && child->args[0].type == CHRT_ARG_STRING)
            character->image = child->args[0].string;
        else if (CHRT_IsCommand(child, "Entry") && entry < 0)
            entry = (int32_t)c;
        else if (CHRT_IsCommand(child, "Human"))
            character->human = true;
        else if (CHRT_IsCommand(child, "Object"))
            object = true;
        else if (CHRT_IsCommand(child, "Hand"))
            hand = true;
        else if (CHRT_IsCommand(child, "Flags")) {
            for (uint32_t i = 0; i < child->argCount; i++)
                character->sleep |= child->args[i].type == CHRT_ARG_STRING && CHRT_EqualsIgnoreCase(child->args[i].string, "SLEEP");
        }
    }
    character->arm = object && hand;

    // The Entry blocks are only read when the character isn't placed in the scene by its own commands
    character->hasPosition = ReadPlacement(script, index, character);
    character->positioned = character->hasPosition && IsInScene(character->position);
    if (!character->positioned && entry >= 0) {
        character->hasPosition |= ReadPlacement(script, (uint32_t)entry, character);
        character->positioned = character->hasPosition && IsInScene(character->position);
    }
}

static bool IsBlockCommand(const CHRT_Command* command) {
    if (command->argCount == 0 || command->args[command->argCount - 1].type != CHRT_ARG_INT)
        return false;
    for (size_t i = 0; i < sizeof(g_blockCommands) / sizeof(g_blockCommands[0]); i++) {
        if (CHRT_IsCommand(command, g_blockCommands[i]))
            return true;
    }
    return false;
}

// Returns the null terminated string at offset, or NULL if it is out of bounds
static const char* GetString(const uint8_t* buffer, size_t size, uint64_t offset) {
    if (offset >= size || memchr(buffer + offset, '\0', size - offset) == NULL)
        return NULL;
    return (const char*)(buffer + offset);
}

// Reads the command at *pos. Returns false if it is out of bounds
static bool ReadCommand(const CHRT_Script* script, size_t end, size_t* pos, CHRT_Command* command) {
    const uint8_t* buffer = script->buffer;
    command->name = GetString(buffer, script->size, *pos + *(const uint32_t*)(buffer + *pos));
    if (command->name == NULL)
        return false;
    uint32_t argTypes = *(const uint32_t*)(buffer + *pos + 4);
    *pos += 8;

    for (command->argCount = 0; argTypes != CHRT_ARG_END; argTypes >>= 3, *pos += 4) {
        if (*pos + 4 > end || command->argCount >= CHRT_MAX_ARGS)
            return false;
        CHRT_Arg* arg = &command->args[command->argCount++];
        memset(arg, 0, sizeof(*arg));
        arg->type = argTypes & 7;
        arg->i = *(const uint32_t*)(buffer + *pos);
        memcpy(&arg->f, &arg->i, sizeof(float));
        if (arg->type == CHRT_ARG_LITERAL)
            memcpy(arg->literal, buffer + *pos, 4);
        else if (arg->type == CHRT_ARG_STRING) {
            arg->string = GetString(buffer, script->size, *pos + arg->i);
            if (arg->string == NULL)
                return false;
        }
    }
    return true;
}

// Decodes the commands of script->buffer. filepath is only used in the error messages. Frees the script on error
static bool Parse(CHRT_Script* script, const char* filepath) {
    const CHRT_Header* header = (const CHRT_Header*)script->buffer;
    if (script->size < sizeof(CHRT_Header) || memcmp(header->id, "CHRS", 4) != 0 || header->size > script->size) {
        fprintf(stderr, "[CHRT_Load]: Invalid CHRT file %s\n", filepath);
        CHRT_Free(script);
        return false;
    }

    // Each command takes 8 bytes at least
    script->commands = malloc((header->size / 8 + 1) * sizeof(CHRT_Command));
    if (script->commands == NULL) {
        fputs("[CHRT_Load]: Memory error\n", stderr);
        CHRT_Free(script);
        return false;
    }

    size_t blockEnds[CHRT_MAX_DEPTH]; // Position where each open block ends
    int32_t blocks[CHRT_MAX_DEPTH];   // Command that opens each block
    uint32_t depth = 0;
    size_t pos = sizeof(CHRT_Header);
    while (pos + 8 <= header->size) {
        while (depth > 0 && pos >= blockEnds[depth - 1])
            depth--;

        CHRT_Command* command = &script->commands[script->commandCount];
        if (!ReadCommand(script, header->size, &pos, command)) {
            fprintf(stderr, "[CHRT_Load]: Invalid command at 0x%04zX in %s\n", pos, filepath);
            CHRT_Free(script);
            return false;
        }
        command->parent = depth > 0 ? blocks[depth - 1] : -1;
        command->depth = depth;

        if (depth < CHRT_MAX_DEPTH && IsBlockCommand(command)) {
            blockEnds[depth] = pos + (size_t)command->args[command->argCount - 1].i * 4;
            blocks[depth++] = (int32_t)script->commandCount;
        }
        script->commandCount++;
    }

    if (pos != header->size) {
        fprintf(stderr, "[CHRT_Load]: Invalid command data at 0x%04zX in %s\n", pos, filepath);
        CHRT_Free(script);
        return false;
    }
    return true;
}

bool CHRT_Load(const char* filepath, CHRT_Script* script) {
    memset(script, 0, sizeof(*script));
    script->buffer = File_LoadToMemoryDecompressed(filepath, &script->size);
    if (script->buffer == NULL)
        return false;
    return Parse(script, filepath);
}

bool CHRT_LoadFromMemory(const uint8_t* data, size_t size, const char* name, CHRT_Script* script) {
    memset(script, 0, sizeof(*script));
    script->buffer = malloc(size + 1);
    if (script->buffer == NULL)
        return false;
    memcpy(script->buffer, data, size);
    script->size = size;
    return Parse(script, name);
}

void CHRT_Free(CHRT_Script* script) {
    free(script->buffer);
    free(script->commands);
    memset(script, 0, sizeof(*script));
}

bool CHRT_Print(const char* filepath) {
    printf("[CHRT_Print]: Reading file: %s\n", filepath);
    CHRT_Script script;
    if (!CHRT_Load(filepath, &script))
        return false;

    for (uint32_t c = 0; c < script.commandCount; c++) {
        const CHRT_Command* command = &script.commands[c];
        printf("  %*s%s(", command->depth * 4, "", command->name);
        for (uint32_t i = 0; i < command->argCount; i++) {
            const CHRT_Arg* arg = &command->args[i];
            if (i > 0)
                printf(", ");
            switch (arg->type) {
            case CHRT_ARG_FLOAT:
                printf("%g", arg->f);
                break;
            case CHRT_ARG_LITERAL:
                printf("'%s'", arg->literal);
                break;
            case CHRT_ARG_STRING:
                printf("\"%s\"", arg->string);
                break;
            case CHRT_ARG_INT:
                printf("%u", arg->i);
                break;
            default:
                printf("<type %u: 0x%08X>", arg->type, arg->i);
                break;
            }
        }
        printf(")\n");
    }
    printf("[CHRT_Print]: %u commands\n", script.commandCount);

    CHRT_Free(&script);
    return true;
}

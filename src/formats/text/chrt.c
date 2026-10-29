#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core/file.h"
#include "formats/text/chrt.h"

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

bool CHRT_Load(const char* filepath, CHRT_Script* script) {
    memset(script, 0, sizeof(*script));
    script->buffer = File_LoadToMemoryDecompressed(filepath, &script->size);
    if (script->buffer == NULL)
        return false;

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

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

typedef enum {
    CHRT_ARG_END     = 0,
    CHRT_ARG_FLOAT   = 1,
    CHRT_ARG_LITERAL = 2, // 4 characters stored in place (e.g. character ID)
    CHRT_ARG_STRING  = 3, // Offset of the string, relative to the argument
    CHRT_ARG_INT     = 4,
} CHRT_ArgType;

#define CHRT_MAX_DEPTH 8

static const char* g_blockCommands[] = { "DefImage", "Character", "Image", "Entry" };

static bool EqualsIgnoreCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

// Command names are case-insensitive (Image, IMAGE, image)
static bool IsBlockCommand(const char* name) {
    for (size_t i = 0; i < sizeof(g_blockCommands) / sizeof(g_blockCommands[0]); i++) {
        if (EqualsIgnoreCase(name, g_blockCommands[i]))
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

bool CHRT_Print(const char* filepath) {
    printf("[CHRT_Print]: Reading file: %s\n", filepath);
    size_t size = 0;
    uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
    if (buffer == NULL)
        return false;

    const CHRT_Header* header = (const CHRT_Header*)buffer;
    if (size < sizeof(CHRT_Header) || memcmp(header->id, "CHRS", 4) != 0 || header->size > size) {
        fprintf(stderr, "[CHRT_Print]: Invalid CHRT file %s\n", filepath);
        free(buffer);
        return false;
    }

    size_t pos = sizeof(CHRT_Header);
    uint32_t count = 0;
    bool valid = true;
    size_t blockEnds[CHRT_MAX_DEPTH]; // Position where each open block ends
    int depth = 0;
    while (valid && pos + 8 <= header->size) {
        const char* name = GetString(buffer, size, pos + *(const uint32_t*)(buffer + pos));
        uint32_t argTypes = *(const uint32_t*)(buffer + pos + 4);
        if (name == NULL) {
            fprintf(stderr, "[CHRT_Print]: Invalid command name at 0x%04zX\n", pos);
            valid = false;
            break;
        }
        while (depth > 0 && pos >= blockEnds[depth - 1])
            depth--;
        pos += 8;

        uint32_t lastArgType = CHRT_ARG_END;
        uint32_t lastArg = 0;
        printf("  %*s%s(", depth * 4, "", name);
        for (int i = 0; valid && argTypes != CHRT_ARG_END; i++, argTypes >>= 3, pos += 4) {
            if (pos + 4 > header->size) {
                valid = false;
                break;
            }

            const uint8_t* arg = buffer + pos;
            lastArgType = argTypes & 7;
            lastArg = *(const uint32_t*)arg;
            if (i > 0)
                printf(", ");

            const char* str;
            switch (argTypes & 7) {
            case CHRT_ARG_FLOAT:
                printf("%g", *(const float*)arg);
                break;
            case CHRT_ARG_LITERAL:
                printf("'%.4s'", (const char*)arg);
                break;
            case CHRT_ARG_STRING:
                str = GetString(buffer, size, pos + *(const uint32_t*)arg);
                if (str != NULL)
                    printf("\"%s\"", str);
                else
                    valid = false;
                break;
            case CHRT_ARG_INT:
                printf("%u", *(const uint32_t*)arg);
                break;
            default:
                printf("<type %u: 0x%08X>", argTypes & 7, *(const uint32_t*)arg);
                break;
            }
        }
        printf(")\n");
        count++;

        if (lastArgType == CHRT_ARG_INT && depth < CHRT_MAX_DEPTH && IsBlockCommand(name))
            blockEnds[depth++] = pos + (size_t)lastArg * 4;
    }

    if (!valid || pos != header->size)
        fprintf(stderr, "[CHRT_Print]: Invalid command data at 0x%04zX\n", pos);
    else
        printf("[CHRT_Print]: %u commands\n", count);

    free(buffer);
    return valid;
}

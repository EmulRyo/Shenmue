#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/animation/auth.h"

// The file is a TRCK chunk with the chunks ASEQ (commands), ACAM (camera), AMOV (movement of the characters) and ASTR
// (sound names, missing when there is no sound). Every chunk starts with its ID and its size, header included. The
// values are 32-bit words: the keys and the arguments are read in place
typedef struct {
    uint32_t    offset; // Of the header, in the file
    uint32_t    size;
} AUTH_Chunk;

#define AUTH_WAIT       0x00000000 // Followed by the frame of the next commands
#define AUTH_END_FRAME  0xFFFFFFFF

static bool FindChunk(const AUTH_Sequence* sequence, const char id[4], AUTH_Chunk* chunk) {
    for (uint32_t pos = 8; pos + 8 <= sequence->size;) {
        uint32_t size = Bytes_ReadU32(sequence->data + pos + 4);
        if (size < 8 || size % 4 != 0 || size > sequence->size - pos)
            return false;
        if (memcmp(sequence->data + pos, id, 4) == 0) {
            *chunk = (AUTH_Chunk) { pos, size };
            return true;
        }
        pos += size;
    }
    return false;
}

static void SetID(AUTH_Character* character, uint32_t value) {
    character->idValue = value;
    memcpy(character->id, &value, 4);
    character->id[4] = '\0';
}

// Reads the commands, or only counts them and the characters if sequence->commands is NULL
static bool ReadCommands(AUTH_Sequence* sequence, AUTH_Chunk chunk) {
    const uint8_t* data = sequence->data;
    uint32_t end = chunk.offset + chunk.size;
    uint32_t pos = chunk.offset + 0x14;
    uint32_t frame = 0;
    uint32_t commands = 0, characters = 0;
    while (pos + 4 <= end) {
        uint32_t word = Bytes_ReadU32(data + pos);
        if (word == AUTH_WAIT) {
            if (pos + 8 > end)
                return false;
            frame = Bytes_ReadU32(data + pos + 4);
            pos += 8;
            if (frame == AUTH_END_FRAME)
                break;
            continue;
        }
        uint8_t argCount = (uint8_t)(word >> 8);
        if (pos + 4 + argCount * 4 > end)
            return false;
        if (sequence->commands != NULL) {
            AUTH_Command* command = &sequence->commands[commands];
            *command = (AUTH_Command) { frame, (uint8_t)word, argCount, (const uint32_t*)(data + pos + 4) };
            if (command->opcode == AUTH_COMMAND_CHARACTER && argCount >= 1)
                SetID(&sequence->characters[characters], command->args[0]);
        }
        commands++;
        characters += (uint8_t)word == AUTH_COMMAND_CHARACTER && argCount >= 1;
        pos += 4 + argCount * 4;
    }
    if (frame != AUTH_END_FRAME)
        return false;
    sequence->commandCount = commands;
    sequence->characterCount = characters;
    return true;
}

// Reads count curves from pos. Returns the position after them, or 0 if they don't fit before end
static uint32_t ReadCurves(const AUTH_Sequence* sequence, uint32_t pos, uint32_t end, AUTH_Curve* curves, int count) {
    for (int i = 0; i < count; i++) {
        if (pos + 4 > end)
            return 0;
        uint32_t keys = Bytes_ReadU32(sequence->data + pos);
        if (keys > (end - pos - 4) / 12)
            return 0;
        const float* floats = (const float*)(sequence->data + pos + 4);
        curves[i] = (AUTH_Curve) { keys, floats, floats + keys, floats + 2 * keys };
        pos += 4 + keys * 12;
    }
    return pos;
}

static bool ReadCamera(AUTH_Sequence* sequence, AUTH_Chunk chunk) {
    if (chunk.size < 0x1C)
        return false;
    // One camera, at an offset from the start of the chunk, after an unknown word (always 0x61)
    uint32_t offset = Bytes_ReadU32(sequence->data + chunk.offset + 0x10);
    if (offset % 4 != 0 || offset + 4 > chunk.size)
        return false;
    return ReadCurves(sequence, chunk.offset + offset + 4, chunk.offset + chunk.size, sequence->camera, AUTH_CAMERA_CURVES) != 0;
}

// One entry per character, in the order of their declaration
static bool ReadMovement(AUTH_Sequence* sequence, AUTH_Chunk chunk) {
    if (chunk.size < 0x10)
        return false;
    uint32_t count = Bytes_ReadU32(sequence->data + chunk.offset + 0x0C);
    if (count > (chunk.size - 0x10) / 4)
        return false;
    for (uint32_t i = 0; i < count && i < sequence->characterCount; i++) {
        uint32_t offset = Bytes_ReadU32(sequence->data + chunk.offset + 0x10 + i * 4);
        if (offset % 4 != 0 || offset > chunk.size)
            return false;
        if (ReadCurves(sequence, chunk.offset + offset, chunk.offset + chunk.size, sequence->characters[i].curves, AUTH_CHARACTER_CURVES) == 0)
            return false;
    }
    return true;
}

// Name offsets from the start of the chunk, with an extra one at the end of the names
static bool ReadSounds(AUTH_Sequence* sequence, AUTH_Chunk chunk) {
    if (chunk.size < 0x10)
        return false;
    uint32_t count = Bytes_ReadU32(sequence->data + chunk.offset + 0x0C);
    if (count > (chunk.size - 0x10) / 4)
        return false;
    sequence->sounds = calloc(count + 1, sizeof(const char*));
    if (sequence->sounds == NULL)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = Bytes_ReadU32(sequence->data + chunk.offset + 0x10 + i * 4);
        if (offset >= chunk.size)
            return false;
        const uint8_t* name = sequence->data + chunk.offset + offset;
        if (memchr(name, '\0', chunk.size - offset) == NULL)
            return false;
        sequence->sounds[i] = (const char*)name;
    }
    sequence->soundCount = count;
    return true;
}

void AUTH_Free(AUTH_Sequence* sequence) {
    free(sequence->commands);
    free(sequence->characters);
    free(sequence->sounds);
    free(sequence->data);
    memset(sequence, 0, sizeof(AUTH_Sequence));
}

bool AUTH_Load(const char* filepath, AUTH_Sequence* sequence) {
    memset(sequence, 0, sizeof(AUTH_Sequence));
    sequence->data = File_LoadToMemoryDecompressed(filepath, &sequence->size);
    if (sequence->data == NULL)
        return false;

    AUTH_Chunk commands, camera, movement, sounds;
    bool valid = sequence->size >= 8 && memcmp(sequence->data, "TRCK", 4) == 0 &&
        Bytes_ReadU32(sequence->data + 4) <= sequence->size &&
        FindChunk(sequence, "ASEQ", &commands) && commands.size >= 0x14 &&
        FindChunk(sequence, "ACAM", &camera) && FindChunk(sequence, "AMOV", &movement) &&
        ReadCommands(sequence, commands);
    if (valid) {
        sequence->duration = Bytes_ReadU32(sequence->data + commands.offset + 0x0C);
        sequence->commands = malloc((sequence->commandCount + 1) * sizeof(AUTH_Command));
        sequence->characters = calloc(sequence->characterCount + 1, sizeof(AUTH_Character));
        valid = sequence->commands != NULL && sequence->characters != NULL && ReadCommands(sequence, commands) &&
            ReadCamera(sequence, camera) && ReadMovement(sequence, movement) &&
            (!FindChunk(sequence, "ASTR", &sounds) || ReadSounds(sequence, sounds));
    }
    if (!valid) {
        fprintf(stderr, "[AUTH_Load]: Invalid AUTH file %s\n", filepath);
        AUTH_Free(sequence);
        return false;
    }
    return true;
}

float AUTH_CurveValue(const AUTH_Curve* curve, float time, float fallback) {
    if (curve->count == 0)
        return fallback;
    if (time <= curve->times[0])
        return curve->values[0];
    // Last key at or before the time: after a jump, the value of the second key
    uint32_t low = 0, high = curve->count; // times[low] <= time < times[high]
    while (high - low > 1) {
        uint32_t middle = low + (high - low) / 2;
        if (curve->times[middle] <= time)
            low = middle;
        else
            high = middle;
    }
    uint32_t key = low;
    if (key + 1 >= curve->count)
        return curve->values[key];
    float t0 = curve->times[key], t1 = curve->times[key + 1];
    float t = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0f;
    return curve->values[key] + (curve->values[key + 1] - curve->values[key]) * t;
}

int AUTH_FindCharacter(const AUTH_Sequence* sequence, uint32_t idValue) {
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        if (sequence->characters[i].idValue == idValue)
            return (int)i;
    }
    return -1;
}

float AUTH_ArgFloat(const AUTH_Command* command, uint32_t arg) {
    float value = 0.0f;
    if (arg < command->argCount)
        memcpy(&value, &command->args[arg], sizeof(value));
    return value;
}

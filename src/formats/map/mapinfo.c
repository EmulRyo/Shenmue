#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/map/mapinfo.h"

// Every chunk starts with its ID and its size, header included. Some chunks contain inner chunks or blocks with the same
// header (LGHT, FOG, SCEX) or with a 4-digit ID and a size (ECAM, COLS). The curves and the shapes are read in place
#define MAPINFO_HEADER_SIZE     8
#define MAPINFO_END_OF_SHAPE    0xFFFFFFFF
#define MAPINFO_LIGHT_SIZE      32

static const char* g_groupNames[MAPINFO_GROUP_COUNT] = { "COLI", "HGHT", "EVNT", "UNDU", "SOND", "PROP" };
// Size of the prefix of the records of each group
static const uint32_t g_prefixSizes[MAPINFO_GROUP_COUNT] = { 4, 4, 4, 16, 4, 20 };

const char* MAPINFO_GroupName(MAPINFO_Group group) {
    return group < MAPINFO_GROUP_COUNT ? g_groupNames[group] : "?";
}

static float ReadFloat(const uint8_t* p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// IEEE 754 binary16
static float ReadHalf(const uint8_t* p) {
    uint16_t half = Bytes_ReadU16(p);
    int exponent = (half >> 10) & 0x1F;
    int mantissa = half & 0x3FF;
    float value;
    if (exponent == 0)
        value = ldexpf((float)mantissa, -24);
    else if (exponent == 31)
        value = mantissa != 0 ? NAN : INFINITY;
    else
        value = ldexpf((float)(mantissa | 0x400), exponent - 25);
    return (half & 0x8000) ? -value : value;
}

static void CopyID(char id[MAPINFO_ID_SIZE], const uint8_t* p) {
    memcpy(id, p, 4);
    id[4] = '\0';
}

static bool IsDigits(const uint8_t* p) {
    for (int i = 0; i < 4; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
    }
    return true;
}

// Reads the curves of a camera from pos to end (6 to 8 curves, see MAPINFO_Camera). Returns false if they don't fit
static bool ReadCameraCurves(const MAPINFO_File* file, uint32_t pos, uint32_t end, MAPINFO_Camera* camera) {
    static const int present[AUTH_CAMERA_CURVES] = { 1, 1, 1, 1, 1, 1, 0x20, 0x40 }; // Flag of the optional curves
    memset(camera->curves, 0, sizeof(camera->curves));
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++) {
        if (present[c] != 1 && !(camera->flags & present[c]))
            continue;
        if (pos + 4 > end || pos % 4 != 0)
            return false;
        uint32_t keys = Bytes_ReadU32(file->data + pos);
        if (keys > (end - pos - 4) / 12)
            return false;
        const float* floats = (const float*)(file->data + pos + 4);
        camera->curves[c] = (AUTH_Curve) { keys, floats, floats + keys, floats + 2 * keys };
        pos += 4 + keys * 12;
    }
    return pos == end;
}

// Reads the cameras of an ECAM chunk, or only checks and counts them if file->cameras is NULL
static bool ReadCameras(MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos < end;) {
        if (pos + 12 > end)
            return false;
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        if (size < 12 || size > end - pos)
            return false;
        MAPINFO_Camera camera;
        CopyID(camera.id, file->data + pos);
        camera.flags = Bytes_ReadU32(file->data + pos + 8);
        if (!ReadCameraCurves(file, pos + 12, pos + size, &camera))
            return false;
        if (file->cameras != NULL)
            file->cameras[file->cameraCount] = camera;
        file->cameraCount++;
        pos += size;
    }
    return true;
}

// Reads the lights of the inner LGHT chunks, or only counts them if file->lights is NULL. The space after the lights of
// an inner chunk, and after the inner chunks, is unused
static bool ReadLights(MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    uint32_t group = 0;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos + 0x18 <= end && memcmp(file->data + pos, "LGHT", 4) == 0; group++) {
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        uint32_t count = Bytes_ReadU16(file->data + pos + 0x0C);
        if (size < 0x18 || size > end - pos || count > (size - 0x18) / MAPINFO_LIGHT_SIZE)
            return false;
        for (uint32_t i = 0; i < count && file->lights != NULL; i++) {
            const uint8_t* p = file->data + pos + 0x18 + i * MAPINFO_LIGHT_SIZE;
            MAPINFO_Light* light = &file->lights[file->lightCount + i];
            light->group = group;
            light->id = Bytes_ReadU16(p);
            light->unknown = Bytes_ReadU16(p + 2);
            light->flags = Bytes_ReadU16(p + 4);
            for (int c = 0; c < 3; c++) {
                light->color[c] = ReadHalf(p + 0x06 + c * 2);
                light->position[c] = ReadHalf(p + 0x12 + c * 2);
                light->direction[c] = ReadHalf(p + 0x18 + c * 2);
            }
            light->intensity = ReadHalf(p + 0x0C);
            light->angle = Bytes_ReadU16(p + 0x1E) * 360.0f / 65536.0f;
        }
        file->lightCount += count;
        pos += size;
    }
    return true;
}

// Size of the coordinates of a shape at pos, and their count. Returns false if the type is unknown or it doesn't fit
static bool ShapeValues(const uint8_t* data, uint32_t pos, uint32_t end, uint32_t type, uint32_t* offset, uint32_t* count) {
    *offset = 4;
    switch (type) {
    case MAPINFO_SHAPE_POINT:     *count = 2; break;
    case MAPINFO_SHAPE_SEGMENT:   *count = 4; break;
    case MAPINFO_SHAPE_CIRCLE:    *count = 3; break;
    case MAPINFO_SHAPE_TRIANGLE:  *count = 6; break;
    case MAPINFO_SHAPE_RECTANGLE: *count = 6; break;
    case MAPINFO_SHAPE_POLYLINE:
    case MAPINFO_SHAPE_POLYGON:
        if (pos + 8 > end)
            return false;
        *offset = 8;
        *count = Bytes_ReadU32(data + pos + 4);
        if (*count > (end - pos) / 8)
            return false;
        *count *= 2;
        break;
    default:
        return false;
    }
    return pos + *offset + *count * 4 + 4 <= end && Bytes_ReadU32(data + pos + *offset + *count * 4) == MAPINFO_END_OF_SHAPE;
}

// Reads the records of a group, or only counts them if file->shapes is NULL
static bool ReadGroup(MAPINFO_File* file, MAPINFO_Group group, uint32_t block, uint32_t pos, uint32_t end) {
    while (pos < end) {
        uint32_t prefix = g_prefixSizes[group];
        if (pos + prefix + 4 > end)
            return false;
        const uint8_t* p = file->data + pos;
        uint32_t type = Bytes_ReadU32(p + prefix);
        uint32_t offset, count;
        if (!ShapeValues(file->data, pos + prefix, end, type, &offset, &count))
            return false;
        if (file->shapes != NULL) {
            MAPINFO_Shape* shape = &file->shapes[file->shapeCount];
            memset(shape, 0, sizeof(*shape));
            shape->group = (uint8_t)group;
            shape->type = (uint8_t)type;
            shape->block = block;
            shape->valueCount = count;
            shape->values = (const float*)(p + prefix + offset);
            if (group == MAPINFO_GROUP_PROP) {
                CopyID(shape->property, p);
                shape->value = Bytes_ReadU32(p + 4);
            }
            else if (group == MAPINFO_GROUP_HGHT)
                shape->height = ReadFloat(p);
            else if (group == MAPINFO_GROUP_UNDU) {
                for (int i = 0; i < 4; i++)
                    shape->heights[i] = ReadFloat(p + i * 4);
            }
            else
                shape->value = Bytes_ReadU32(p);
        }
        file->shapeCount++;
        pos += prefix + offset + count * 4 + 4;
    }
    return true;
}

// Reads the shapes of the blocks of a COLS chunk, or only counts them if file->shapes is NULL. Each block has the 6
// groups, whose size doesn't include their header, followed by the grids of the groups (not read)
static bool ReadCollisions(MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    uint32_t block = 0;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos + 8 <= end && IsDigits(file->data + pos); block++) {
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        if (size < 8 || size > end - pos)
            return false;
        uint32_t groupPos = pos + 8;
        for (int g = 0; g < MAPINFO_GROUP_COUNT; g++) {
            if (groupPos + 8 > pos + size || memcmp(file->data + groupPos, g_groupNames[g], 4) != 0)
                return false;
            uint32_t groupSize = Bytes_ReadU32(file->data + groupPos + 4);
            if (groupSize > pos + size - groupPos - 8)
                return false;
            if (!ReadGroup(file, (MAPINFO_Group)g, block, groupPos + 8, groupPos + 8 + groupSize))
                return false;
            groupPos += 8 + groupSize;
        }
        pos += size;
    }
    return true;
}

// Commands of the SNDD chunk: 1 loads a bank in a slot (24 bytes), 2 and 3 are unknown (16 bytes), 0 ends the list.
// Other commands are not understood, and stop the list. Only counts the banks if file->banks is NULL
static void ReadSoundBanks(MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos + 4 <= end;) {
        uint32_t command = Bytes_ReadU32(file->data + pos);
        if (command == 1 && pos + 24 <= end) {
            if (file->banks != NULL) {
                MAPINFO_SoundBank* bank = &file->banks[file->bankCount];
                bank->slot = Bytes_ReadU32(file->data + pos + 4);
                memcpy(bank->name, file->data + pos + 8, 16);
                bank->name[16] = '\0';
            }
            file->bankCount++;
            pos += 24;
        }
        else if ((command == 2 || command == 3) && pos + 16 <= end)
            pos += 16;
        else
            break;
    }
}

static void ReadFog(MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos + 8 <= end;) {
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        if (size < 8 || size > end - pos)
            return;
        if (memcmp(file->data + pos, "FOG ", 4) == 0 && size >= 0x18) {
            const uint8_t* p = file->data + pos + 8;
            file->fog = (MAPINFO_Fog) { true, ReadFloat(p), ReadFloat(p + 4), ReadFloat(p + 8), Bytes_ReadU32(p + 12) };
            return;
        }
        pos += size;
    }
}

static uint32_t CountCutscenes(const MAPINFO_File* file, const MAPINFO_Chunk* chunk) {
    uint32_t end = chunk->offset + chunk->size;
    uint32_t count = 0;
    for (uint32_t pos = chunk->offset + MAPINFO_HEADER_SIZE; pos + 8 <= end;) {
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        if (size < 8 || size > end - pos)
            break;
        count += memcmp(file->data + pos, "TRCK", 4) == 0;
        pos += size;
    }
    return count;
}

// Reads the chunks that have arrays, or only counts their items if the arrays are NULL
static void ReadArrays(MAPINFO_File* file, const char* filepath) {
    file->cameraCount = file->lightCount = file->shapeCount = file->bankCount = 0;
    for (uint32_t i = 0; i < file->chunkCount; i++) {
        const MAPINFO_Chunk* chunk = &file->chunks[i];
        uint32_t cameras = file->cameraCount, lights = file->lightCount, shapes = file->shapeCount;
        bool valid = true;
        if (strcmp(chunk->id, "ECAM") == 0)
            valid = ReadCameras(file, chunk);
        else if (strcmp(chunk->id, "LGHT") == 0)
            valid = ReadLights(file, chunk);
        else if (strcmp(chunk->id, "COLS") == 0)
            valid = ReadCollisions(file, chunk);
        else if (strcmp(chunk->id, "SNDD") == 0)
            ReadSoundBanks(file, chunk);
        if (!valid) {
            // Skips the whole chunk
            file->cameraCount = cameras;
            file->lightCount = lights;
            file->shapeCount = shapes;
            if (file->cameras == NULL)
                fprintf(stderr, "[MAPINFO_Load]: Invalid %s chunk at 0x%X in %s\n", chunk->id, chunk->offset, filepath);
        }
    }
}

void MAPINFO_Free(MAPINFO_File* file) {
    free(file->chunks);
    free(file->cameras);
    free(file->lights);
    free(file->shapes);
    free(file->banks);
    free(file->data);
    memset(file, 0, sizeof(*file));
}

bool MAPINFO_Load(const char* filepath, MAPINFO_File* file) {
    memset(file, 0, sizeof(*file));
    file->data = File_LoadToMemoryDecompressed(filepath, &file->size);
    if (file->data == NULL)
        return false;

    // Chunks up to END, which has to be the last one
    bool valid = false;
    for (int pass = 0; pass < 2 && file->size <= UINT32_MAX; pass++) {
        uint32_t count = 0;
        for (uint32_t pos = 0; pos + MAPINFO_HEADER_SIZE <= file->size;) {
            uint32_t size = Bytes_ReadU32(file->data + pos + 4);
            if (size < MAPINFO_HEADER_SIZE || size > file->size - pos)
                break;
            if (file->chunks != NULL) {
                MAPINFO_Chunk* chunk = &file->chunks[count];
                CopyID(chunk->id, file->data + pos);
                chunk->offset = pos;
                chunk->size = size;
            }
            count++;
            pos += size;
            if (memcmp(file->data + pos - size, "END\0", 4) == 0) {
                valid = pos == file->size;
                break;
            }
        }
        if (!valid)
            break;
        file->chunkCount = count;
        if (pass == 0 && (file->chunks = malloc((count + 1) * sizeof(MAPINFO_Chunk))) == NULL)
            valid = false;
    }
    if (!valid) {
        fprintf(stderr, "[MAPINFO_Load]: Invalid mapinfo file %s\n", filepath);
        MAPINFO_Free(file);
        return false;
    }

    ReadArrays(file, filepath);
    file->cameras = calloc(file->cameraCount + 1, sizeof(MAPINFO_Camera));
    file->lights = calloc(file->lightCount + 1, sizeof(MAPINFO_Light));
    file->shapes = calloc(file->shapeCount + 1, sizeof(MAPINFO_Shape));
    file->banks = calloc(file->bankCount + 1, sizeof(MAPINFO_SoundBank));
    if (file->cameras == NULL || file->lights == NULL || file->shapes == NULL || file->banks == NULL) {
        fputs("[MAPINFO_Load]: Memory error\n", stderr);
        MAPINFO_Free(file);
        return false;
    }
    ReadArrays(file, filepath);

    const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(file, "FOG ");
    if (chunk != NULL)
        ReadFog(file, chunk);
    chunk = MAPINFO_FindChunk(file, "SCN3");
    if (chunk != NULL && chunk->size >= 12)
        file->scnVersion = Bytes_ReadU32(file->data + chunk->offset + 8);
    chunk = MAPINFO_FindChunk(file, "SCEX");
    if (chunk != NULL)
        file->cutsceneCount = CountCutscenes(file, chunk);
    // The CHRS block, followed by its STRG block, fills the chunk
    chunk = MAPINFO_FindChunk(file, "CHRD");
    if (chunk != NULL && chunk->size > 16 && memcmp(file->data + chunk->offset + 8, "CHRS", 4) == 0) {
        file->characters = file->data + chunk->offset + 8;
        file->charactersSize = chunk->size - 8;
    }
    return true;
}

const MAPINFO_Chunk* MAPINFO_FindChunk(const MAPINFO_File* file, const char* id) {
    for (uint32_t i = 0; i < file->chunkCount; i++) {
        if (memcmp(file->chunks[i].id, id, 4) == 0)
            return &file->chunks[i];
    }
    return NULL;
}

bool MAPINFO_ChunkValue(const MAPINFO_File* file, const char* id, uint32_t* value) {
    const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(file, id);
    if (chunk == NULL || chunk->size != 12)
        return false;
    *value = Bytes_ReadU32(file->data + chunk->offset + 8);
    return true;
}

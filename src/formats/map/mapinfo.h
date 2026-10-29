#ifndef __MAPINFO_H__
#define __MAPINFO_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "formats/animation/auth.h"

// mapinfo.bin: the information of a map, a list of chunks (ID and size, header included) that ends with an END chunk.
// The chunks read here are the cameras (ECAM), the lights (LGHT), the collisions and areas (COLS), the sound banks
// (SNDD), the fog (FOG), the characters (CHRD, a CHRT script), the program of the scene (SCN3), the cutscenes (SCEX) and
// the chunks of a single value. See the MAPINFO page of the wiki

#define MAPINFO_ID_SIZE 5

typedef struct {
    char        id[MAPINFO_ID_SIZE]; // e.g. "ECAM", "FOG "
    uint32_t    offset;              // Of the header, in the file
    uint32_t    size;                // Header included
} MAPINFO_Chunk;

// Camera with a 4-digit ID. The curves are the same as the camera of the AUTH files; the roll and the field of view are
// optional (count 0 if missing)
typedef struct {
    char        id[MAPINFO_ID_SIZE]; // e.g. "0101"
    uint32_t    flags;               // 0x20: roll curve, 0x40: field of view curve, the rest unknown
    AUTH_Curve  curves[AUTH_CAMERA_CURVES]; // AUTH_CameraCurve order
} MAPINFO_Camera;

typedef struct {
    uint32_t    group;      // Index of the inner LGHT chunk
    uint16_t    id;
    uint16_t    unknown;    // 0 or 1
    uint16_t    flags;      // Unknown: 11, 3, 2, 4, 12, 10
    float       color[3];   // RGB, 0 to 1
    float       intensity;  // Probably
    float       position[3];
    float       direction[3];
    float       angle;      // Degrees
} MAPINFO_Light;

typedef enum {
    MAPINFO_GROUP_COLI,     // Collision walls and obstacles
    MAPINFO_GROUP_HGHT,     // Probably areas with another floor height
    MAPINFO_GROUP_EVNT,     // Probably event areas
    MAPINFO_GROUP_UNDU,     // Sloped floor
    MAPINFO_GROUP_SOND,     // Probably sound areas
    MAPINFO_GROUP_PROP,     // Areas with a property (CAM2, STIR, WALK...)
    MAPINFO_GROUP_COUNT,
} MAPINFO_Group;

typedef enum {
    MAPINFO_SHAPE_POINT     = 0, // x, z
    MAPINFO_SHAPE_SEGMENT   = 1, // x1, z1, x2, z2
    MAPINFO_SHAPE_POLYLINE  = 2, // n points
    MAPINFO_SHAPE_CIRCLE    = 3, // radius, x, z
    MAPINFO_SHAPE_TRIANGLE  = 4, // 3 points
    MAPINFO_SHAPE_RECTANGLE = 5, // Probably x, z of the center and two half axes ax, az, bx, bz
    MAPINFO_SHAPE_POLYGON   = 6, // n points
} MAPINFO_ShapeType;

// A record of a COLS group: a prefix that depends on the group and a shape in the horizontal plane (x, z)
typedef struct {
    uint8_t         group;      // MAPINFO_Group
    uint8_t         type;       // MAPINFO_ShapeType
    uint32_t        block;      // Index of the COLS block
    uint32_t        value;      // COLI type, EVNT and SOND value, first word of the PROP prefix
    float           height;     // HGHT height
    float           heights[4]; // UNDU heights
    char            property[MAPINFO_ID_SIZE]; // PROP type, e.g. "CAM2"
    uint32_t        valueCount;
    const float*    values;     // Points into the file: the coordinates of the shape (the points of the polylines and
                                // polygons, without their count)
} MAPINFO_Shape;

typedef struct {
    uint32_t    slot;
    char        name[17]; // e.g. "bgm087.snd", "FREE" for an empty slot
} MAPINFO_SoundBank;

typedef struct {
    bool        present;
    float       nearDistance;
    float       farDistance;
    float       density;    // Probably
    uint32_t    color;      // ARGB
} MAPINFO_Fog;

typedef struct {
    uint8_t*            data;   // The whole file: the curves and the shapes point into it
    size_t              size;
    uint32_t            chunkCount;
    MAPINFO_Chunk*      chunks;
    uint32_t            cameraCount;
    MAPINFO_Camera*     cameras;
    uint32_t            lightCount;
    MAPINFO_Light*      lights;
    uint32_t            shapeCount;
    MAPINFO_Shape*      shapes;
    uint32_t            bankCount;
    MAPINFO_SoundBank*  banks;
    MAPINFO_Fog         fog;    // The first set of the FOG chunk
    uint32_t            scnVersion;     // 0x20000: SH-4 code, 0x100: bytecode, 0 if there is no SCN3 chunk
    uint32_t            cutsceneCount;  // TRCK chunks of the SCEX chunk
    const uint8_t*      characters;     // CHRS block of the CHRD chunk, NULL if none
    uint32_t            charactersSize;
} MAPINFO_File;

// Loads a mapinfo.bin file. Returns false if it is not a valid list of chunks. The chunks that can't be decoded are
// skipped (the error is printed)
bool MAPINFO_Load(const char* filepath, MAPINFO_File* file);
void MAPINFO_Free(MAPINFO_File* file);
// First chunk with this ID, NULL if none
const MAPINFO_Chunk* MAPINFO_FindChunk(const MAPINFO_File* file, const char* id);
// Value of a chunk of a single 4-byte value (MAPR, MAPT, DIRT, SCRL, SCOF, BLKS). Returns false if there is none
bool MAPINFO_ChunkValue(const MAPINFO_File* file, const char* id, uint32_t* value);
// Name of a COLS group, e.g. "COLI"
const char* MAPINFO_GroupName(MAPINFO_Group group);

#endif

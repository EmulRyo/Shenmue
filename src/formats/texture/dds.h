#ifndef __DDS_H__
#define __DDS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// DirectDraw Surface textures of the PC version: a DDS header (and an optional DX10 one) followed by the data of each
// face (6 in the cube maps), each with its chain of mipmaps. Some of them are also stored without any header, only the
// BC1/BC3 blocks with their full chain of mipmaps: their format and size are guessed from the size of the data

#define DDS_HEADER_SIZE         0x80    // With the "DDS " magic
#define DDS_DX10_HEADER_SIZE    0x14

typedef enum {
    DDS_FORMAT_UNKNOWN,
    DDS_FORMAT_BC1,     // DXT1, 8 bytes per block of 4x4 pixels
    DDS_FORMAT_BC2,     // DXT2/DXT3, 16 bytes per block
    DDS_FORMAT_BC3,     // DXT4/DXT5, 16 bytes per block
    DDS_FORMAT_RGB,     // Uncompressed, with the masks of the channels
} DDS_Format;

typedef struct {
    bool        raw;            // Without header, guessed from the size of the data
    DDS_Format  format;
    char        fourCC[5];      // Of the pixel format, empty if the format is uncompressed
    uint32_t    dxgiFormat;     // Of the DX10 header, 0 if it hasn't one
    uint32_t    width;
    uint32_t    height;
    uint32_t    mipmaps;        // Levels of each face, 1 without mipmaps
    uint32_t    faces;          // 1, or the faces of a cube map
    uint32_t    cubeFaces;      // Mask of the faces of a cube map (+X, -X, +Y, -Y, +Z, -Z), 0 if it isn't one
    uint32_t    bitCount;       // Of the uncompressed formats
    uint32_t    masks[4];       // R, G, B, A of the uncompressed formats
    uint32_t    flags;          // Of the DDS header
    uint32_t    caps[2];
    size_t      dataOffset;     // Of the first face in the file
    size_t      dataSize;       // Of all the faces with their mipmaps, as the header describes them
} DDS_Info;

// Reads the header of a DDS file. Returns false if it isn't one, if its format isn't supported or if the data of its
// first level is missing
bool DDS_ReadInfo(const uint8_t* data, size_t size, DDS_Info* info);
// Fills candidates (best first) with the formats and sizes whose BC blocks with a full chain of mipmaps (or a single
// level if none fits) have exactly size bytes, ordered by how continuous the image is across the blocks. Returns how
// many there are, at most max
uint32_t DDS_GuessRaw(const uint8_t* data, size_t size, DDS_Info* candidates, uint32_t max);
// Decodes the first level of the texture to RGBA8888 (R in the lowest byte). The faces of a cube map are drawn as a
// horizontal cross, 4x3 faces, with transparent pixels where there is no face. Returns false on error
bool DDS_Decode(const uint8_t* data, size_t size, const DDS_Info* info, uint32_t* width, uint32_t* height, uint32_t** pixels);
// Decodes a level of BC1/BC2/BC3 blocks to RGBA8888 (pixels has width * height elements)
void DDS_DecodeBlocks(const uint8_t* data, DDS_Format format, uint32_t width, uint32_t height, uint32_t* pixels);
// Size of a level, and of a level with its mipmaps
size_t DDS_LevelSize(const DDS_Info* info, uint32_t width, uint32_t height);
size_t DDS_ChainSize(const DDS_Info* info, uint32_t width, uint32_t height, uint32_t mipmaps);
const char* DDS_FormatToString(DDS_Format format);

#endif

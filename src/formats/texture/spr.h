#ifndef __SPR_H__
#define __SPR_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SPR_Header {
    char        id[4]; // "TEXN"
    uint32_t    size;
    char        filename[8];
    uint8_t     pvrData[];
} SPR_Header;

// A TEXN block of an SPR file
typedef struct {
    char        name[9];        // Its filename, with the characters that can't be printed replaced by '?'
    uint32_t    offset;         // Of the TEXN header in the file
    uint32_t    size;           // Of the block, with its header
    bool        pvr;            // Its data has a PVRT header (after an optional GBIX one)
    uint8_t     pixelFormat;    // PVR_PixelFormat
    uint8_t     dataFormat;     // PVR_DataFormat
    uint16_t    width;
    uint16_t    height;
} SPR_Texture;

// An SPR file in memory: one or more TEXN blocks one after another, inside SCLn blocks in the scroll*.spr files (gzipped
// in the .gz files)
typedef struct {
    uint8_t*        data;
    size_t          size;
    uint32_t        count;
    SPR_Texture*    textures;
} SPR_File;

// Loads the file (decompressed if it is gzipped) and lists its TEXN blocks. Returns false if it has none
bool SPR_Open(const char* filepath, SPR_File* file);
// Decodes the texture index. Returns false if it isn't a valid PVR texture
bool SPR_Decode(const SPR_File* file, uint32_t index, uint16_t* width, uint16_t* height, uint32_t** pixels);
void SPR_Close(SPR_File* file);

#endif

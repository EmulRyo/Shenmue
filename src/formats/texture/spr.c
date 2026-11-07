#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/texture/pvr.h"
#include "formats/texture/spr.h"

#define SPR_HEADER_SIZE     16  // Of the TEXN header
#define SPR_SCROLL_HEADER_SIZE 8 // Of the SCRL header and of the header of the SCLn blocks
#define SPR_PVRT_SIZE       16  // Of the PVRT header

// Fills the format and the size of the PVR texture of a block
static void ReadPVRInfo(const uint8_t* data, size_t size, SPR_Texture* texture) {
    size_t pos = 0;
    if (size >= 8 && memcmp(data, "GBIX", 4) == 0)
        pos = 8 + (size_t)Bytes_ReadU32(data + 4);
    if (pos + SPR_PVRT_SIZE > size || memcmp(data + pos, "PVRT", 4) != 0)
        return;
    texture->pvr = true;
    texture->pixelFormat = data[pos + 8];
    texture->dataFormat = data[pos + 9];
    texture->width = Bytes_ReadU16(data + pos + 12);
    texture->height = Bytes_ReadU16(data + pos + 14);
}

bool SPR_Open(const char* filepath, SPR_File* file) {
    *file = (SPR_File) { 0 };
    printf("[SPR_Open]: Loading SPR file: %s\n", filepath);
    // The .gz files of the game are gzipped SPR files
    file->data = File_LoadToMemoryDecompressed(filepath, &file->size);
    if (file->data == NULL)
        return false;

    // The blocks follow each other until the end of the file, or a few bytes of padding. The scroll*.spr files start
    // with an SCRL header, and each TEXN block is inside an SCL0, SCL1... block: their headers are skipped
    size_t pos = 0;
    uint32_t capacity = 0;
    while (pos + SPR_HEADER_SIZE <= file->size) {
        const uint8_t* id = file->data + pos;
        if (memcmp(id, "SCRL", 4) == 0 || (memcmp(id, "SCL", 3) == 0 && id[3] >= '0' && id[3] <= '9')) {
            pos += SPR_SCROLL_HEADER_SIZE;
            continue;
        }
        if (memcmp(id, "TEXN", 4) != 0)
            break;
        uint32_t size = Bytes_ReadU32(file->data + pos + 4);
        if (size < SPR_HEADER_SIZE || size > file->size - pos) {
            fprintf(stderr, "[SPR_Open]: Block %u at 0x%zX has an invalid size 0x%X\n", file->count, pos, size);
            break;
        }
        if (file->count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            SPR_Texture* textures = (SPR_Texture*)realloc(file->textures, capacity * sizeof(SPR_Texture));
            if (textures == NULL) {
                SPR_Close(file);
                return false;
            }
            file->textures = textures;
        }
        SPR_Texture* texture = &file->textures[file->count++];
        *texture = (SPR_Texture) { 0 };
        texture->offset = (uint32_t)pos;
        texture->size = size;
        for (int i = 0; i < 8; i++) {
            char c = (char)file->data[pos + 8 + i];
            if (c == '\0')
                break;
            texture->name[i] = c >= 0x20 && c < 0x7F ? c : '?';
        }
        ReadPVRInfo(file->data + pos + SPR_HEADER_SIZE, size - SPR_HEADER_SIZE, texture);
        pos += size;
    }

    if (file->count == 0) {
        fprintf(stderr, "[SPR_Open]: No TEXN block in %s\n", filepath);
        SPR_Close(file);
        return false;
    }
    printf("[SPR_Open]: %u textures, 0x%zX bytes after the last one\n", file->count, file->size - pos);
    return true;
}

bool SPR_Decode(const SPR_File* file, uint32_t index, uint16_t* width, uint16_t* height, uint32_t** pixels) {
    if (index >= file->count || !file->textures[index].pvr)
        return false;
    const SPR_Texture* texture = &file->textures[index];
    printf("[SPR_Decode]: Texture %u: %s, offset 0x%X, size 0x%X\n", index, texture->name, texture->offset, texture->size);
    return PVR_LoadFromMemory(file->data + texture->offset + SPR_HEADER_SIZE, width, height, pixels);
}

void SPR_Close(SPR_File* file) {
    free(file->data);
    free(file->textures);
    *file = (SPR_File) { 0 };
}

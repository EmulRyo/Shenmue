#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <raylib.h>
#include "file.h"

uint8_t* File_LoadToMemoryEx(const char* filepath, size_t* outSize) {
    FILE* file = fopen(filepath, "rb");
    if (file == NULL) {
        fprintf(stderr, "[File_LoadToMemory] Error: Open file %s\n", filepath);
        return NULL;
    }

    // obtain file size:
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    // allocate memory to contain the whole file:
    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * size);
    if (buffer == NULL) {
        fprintf(stderr, "[File_LoadToMemory] Error: Memory allocation\n");
        fclose(file);
        return NULL;
    }

    // copy the file into the buffer:
    size_t result = fread(buffer, size, 1, file);

    fclose(file);

    if (outSize != NULL)
        *outSize = (size_t)size;

    return buffer;
}

uint8_t* File_LoadToMemory(const char* filepath) {
    return File_LoadToMemoryEx(filepath, NULL);
}

const char* File_ExtensionFromMagic(const uint8_t* buffer, size_t size) {
    if (size >= 12) {
        if (memcmp(buffer, "RIFF", 4) == 0 && memcmp(buffer + 8, "WAVE", 4) == 0) return "wav";
        if (memcmp(buffer, "RIFF", 4) == 0 && memcmp(buffer + 8, "XWMA", 4) == 0) return "xwm";
    }
    if (size >= 4) {
        if (memcmp(buffer, "DXBC", 4) == 0) return "hlsl";
        if (memcmp(buffer, "CHRS", 4) == 0) return "chrt";
        if (memcmp(buffer, "PAKF", 4) == 0) return "pkf";
        if (memcmp(buffer, "PAKS", 4) == 0) return "pks";
        if (memcmp(buffer, "TEXN", 4) == 0) return "spr";
        if (memcmp(buffer, "GBIX", 4) == 0) return "pvr";
        if (memcmp(buffer, "PVRT", 4) == 0) return "pvr";
    }
    if (size >= 3) {
        if (memcmp(buffer, "DDS", 3) == 0) return "dds";
        if (memcmp(buffer, "AFS", 3) == 0) return "afs";
        if (memcmp(buffer, "IDX", 3) == 0) return "idx";
    }
    return NULL;
}

bool File_IsGzip(const uint8_t* buffer, size_t size) {
    return size >= 18 && buffer[0] == 0x1F && buffer[1] == 0x8B && buffer[2] == 0x08;
}

uint8_t* File_GzipDecompress(const uint8_t* buffer, size_t size, size_t* outSize) {
    if (!File_IsGzip(buffer, size))
        return NULL;

    // Skip the optional fields of the gzip header
    uint8_t flags = buffer[3];
    size_t pos = 10;
    if (flags & 0x04) { // FEXTRA
        if (pos + 2 > size) return NULL;
        pos += 2 + (buffer[pos] | (buffer[pos + 1] << 8));
    }
    if (flags & 0x08) { // FNAME
        while (pos < size && buffer[pos] != '\0') pos++;
        pos++;
    }
    if (flags & 0x10) { // FCOMMENT
        while (pos < size && buffer[pos] != '\0') pos++;
        pos++;
    }
    if (flags & 0x02) // FHCRC
        pos += 2;
    if (pos + 8 > size) {
        fprintf(stderr, "[File_GzipDecompress] Error: Invalid gzip header\n");
        return NULL;
    }

    // The trailer contains the CRC32 and the uncompressed size (modulo 2^32)
    const uint8_t* trailer = buffer + size - 8;
    uint32_t expectedSize = trailer[4] | (trailer[5] << 8) | (trailer[6] << 16) | ((uint32_t)trailer[7] << 24);

    int decompressedSize = 0;
    uint8_t* data = DecompressData(buffer + pos, (int)(size - 8 - pos), &decompressedSize);
    if (data == NULL || decompressedSize <= 0 || (uint32_t)decompressedSize != expectedSize) {
        fprintf(stderr, "[File_GzipDecompress] Error: Decompressed %d bytes, expected %u\n",decompressedSize, expectedSize);
        MemFree(data);
        return NULL;
    }

    if (outSize != NULL)
        *outSize = (size_t)decompressedSize;
    return data;
}

uint8_t* File_LoadToMemoryDecompressed(const char* filepath, size_t* outSize) {
    size_t size = 0;
    uint8_t* buffer = File_LoadToMemoryEx(filepath, &size);
    if (buffer == NULL || !File_IsGzip(buffer, size)) {
        if (outSize != NULL)
            *outSize = size;
        return buffer;
    }

    uint8_t* data = File_GzipDecompress(buffer, size, outSize);
    free(buffer);
    return data;
}

bool File_Save(const char* filepath, const uint8_t* buffer, size_t size) {
    FILE* file = fopen(filepath, "wb");
    if (file == NULL) {
        fprintf(stderr, "[File_Save] Error: Creating file %s\n",filepath);
        return false;
    }
    size_t written = fwrite(buffer, 1, size, file);
    fclose(file);
    return written == size;
}

const char* File_GetFilename(const char* filepath) {
    const char* lastSlash = strrchr(filepath, '/');
    const char* lastBackslash = strrchr(filepath, '\\');
    if (lastBackslash != NULL && (lastSlash == NULL || lastBackslash > lastSlash))
        lastSlash = lastBackslash;
    return lastSlash != NULL ? lastSlash + 1 : filepath;
}

void File_GetOutputFolder(const char* filepath, const char* outputPath, char* outputFolder, size_t size) {
    const char* filename = File_GetFilename(filepath);
    const char* lastDot = strrchr(filename, '.');
    int basenameLength = (int)(lastDot != NULL ? lastDot - filename : strlen(filename));

    if (outputPath != NULL && strlen(outputPath) > 0)
        snprintf(outputFolder, size, "%s/%.*s", outputPath, basenameLength, filename);
    else
        snprintf(outputFolder, size, "%.*s%.*s", (int)(filename - filepath), filepath, basenameLength, filename);
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
        if (memcmp(buffer, "PAKF", 4) == 0) return "pkf";
        if (memcmp(buffer, "PAKS", 4) == 0) return "pks";
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

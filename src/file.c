#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "file.h"

uint8_t* File_LoadToMemory(const char* filepath) {
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
        return NULL;
    }

    // copy the file into the buffer:
    size_t result = fread(buffer, size, 1, file);

    fclose(file);

    return buffer;
}

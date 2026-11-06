#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "core/hashTable.h"
#include "core/stringBuilder.h"
#include "core/file.h"
#include "formats/archive/tad.h"
#include "formats/archive/tac.h"

FILE* TAC_Open(const char* filepath) {
    FILE* tacFile = fopen(filepath, "rb");
    if (tacFile == NULL) {
        fputs("File error", stderr);
        exit(1);
    }

    return tacFile;
}

void TAC_Close(FILE* tacFile) {
    if (tacFile != NULL) {
        fclose(tacFile);
    }
}

bool TAC_ReadFile(FILE* tacFile, TADFileEntry* entry, uint8_t* buffer) {
    // The offsets of the biggest TAC files don't fit in a 32-bit long (the size of long in Windows)
#ifdef _WIN32
    if (_fseeki64(tacFile, (int64_t)entry->offset, SEEK_SET) != 0)
#else
    if (fseeko(tacFile, (off_t)entry->offset, SEEK_SET) != 0)
#endif
        return false;
    return fread(buffer, 1, entry->size, tacFile) == entry->size;
}

uint8_t* TAC_GetFile(FILE* tacFile, TADFileEntry* entry) {
    if (entry->size == 0) {
        return NULL;
    }
    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * entry->size);
    if (buffer == NULL) {
        fputs("[SaveTADFileEntry]: Memory error", stderr);
        exit(2);
    }
    TAC_ReadFile(tacFile, entry, buffer);

    return buffer;
}

bool TAC_WriteFile(const uint8_t* data, uint32_t size, const char* outputFilename) {
    if (size == 0) {
        return false;
    }

    // Without extension (the name of the file is unknown), the one of its magic
    char withExtension[1024];
    const char* lastSlash = strrchr(outputFilename, '/');
    const char* lastDot = strrchr(lastSlash != NULL ? lastSlash : outputFilename, '.');
    if (lastDot == NULL) {
        const char* ext = File_ExtensionFromMagic(data, size);
        if (ext != NULL && snprintf(withExtension, sizeof(withExtension), "%s.%s", outputFilename, ext) < (int)sizeof(withExtension))
            outputFilename = withExtension;
    }

    FILE* file = fopen(outputFilename, "wb");
    if (file == NULL) {
        return false;
    }
    bool written = fwrite(data, 1, size, file) == size;
    fclose(file);
    return written;
}

bool TAC_SaveFile(FILE* tacFile, TADFileEntry* entry, const char* outputFilename) {
    if (entry->size == 0) {
        return false;
    }
    uint8_t* buffer = TAC_GetFile(tacFile, entry);
    bool saved = TAC_WriteFile(buffer, entry->size, outputFilename);
    free(buffer);
    return saved;
}

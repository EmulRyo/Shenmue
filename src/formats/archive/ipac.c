#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include "core/file.h"
#include "formats/archive/ipac.h"

#define IPAC_PATH_SIZE 1024

typedef struct {
    char        id[4]; // "IPAC"
    uint32_t    dictionaryOffset; // Relative to the start of the IPAC block
    uint32_t    count;
    uint32_t    size;
} IPAC_Header;

typedef struct {
    char        filename[8];
    char        extension[4];
    uint32_t    offset; // Relative to the start of the IPAC block
    uint32_t    size;
} IPAC_Entry;

// Copies a fixed size string, stopping at the first null or space character
static void CopyTrimmed(char* dst, const char* src, size_t srcSize, bool lowercase) {
    size_t i = 0;
    for (; i < srcSize && src[i] != '\0' && src[i] != ' '; i++)
        dst[i] = lowercase ? (char)tolower((unsigned char)src[i]) : src[i];
    dst[i] = '\0';
}

static void GetEntryFilename(char (*filenames)[IPAC_FILENAME_SIZE], uint32_t entryID, const IPAC_Entry* entry, const uint8_t* data, size_t size) {
    char name[9];
    char ext[5];
    CopyTrimmed(name, entry->filename, sizeof(entry->filename), false);
    CopyTrimmed(ext, entry->extension, sizeof(entry->extension), true);

    // The magic is more accurate than the IPAC extension (e.g. textures are stored as "BIN")
    // except for CHRM files, MT5 models whose textures are stored outside the model
    const char* magicExt = File_ExtensionFromMagic(data, size);
    if (magicExt != NULL && !(strcmp(magicExt, "mt5") == 0 && strcmp(ext, "chrm") == 0))
        snprintf(ext, sizeof(ext), "%s", magicExt);
    else if (ext[0] == '\0')
        snprintf(ext, sizeof(ext), "bin");

    char* filename = filenames[entryID];
    if (name[0] != '\0')
        snprintf(filename, IPAC_FILENAME_SIZE, "%s.%s", name, ext);
    else
        snprintf(filename, IPAC_FILENAME_SIZE, "%04u.%s", entryID, ext);

    // Avoid overwriting a previous entry with the same name
    for (uint32_t i = 0; i < entryID; i++) {
        if (strcmp(filenames[i], filename) == 0) {
            snprintf(filename, IPAC_FILENAME_SIZE, "%s_%04u.%s", name[0] != '\0' ? name : "file", entryID, ext);
            break;
        }
    }
}

// Called with each file of an IPAC block, decompressed. Returns false if the file couldn't be handled
typedef bool (*IPAC_FileCallback)(const char* filename, const uint8_t* data, size_t size, void* context);

// Reads the files of an IPAC block, printing the header and the entries if verbose. Returns the number of files
// handled by the callback
static uint32_t ReadFiles(const uint8_t* buffer, size_t size, bool verbose, IPAC_FileCallback callback, void* context) {
    if (size < sizeof(IPAC_Header) || memcmp(buffer, "IPAC", 4) != 0) {
        fprintf(stderr, "[IPAC_ReadFiles]: Invalid IPAC block\n");
        return 0;
    }

    const IPAC_Header* header = (const IPAC_Header*)buffer;
    if (verbose) {
        printf("IPAC Header:\n");
        printf("  Dictionary Offset: 0x%04X\n", header->dictionaryOffset);
        printf("  File Count: %u\n", header->count);
        printf("  Size: 0x%04X\n", header->size);
    }

    if ((uint64_t)header->dictionaryOffset + (uint64_t)header->count * sizeof(IPAC_Entry) > size) {
        fprintf(stderr, "[IPAC_ReadFiles]: Dictionary out of bounds\n");
        return 0;
    }
    if (header->count == 0)
        return 0;

    char (*filenames)[IPAC_FILENAME_SIZE] = calloc(header->count, IPAC_FILENAME_SIZE);
    if (filenames == NULL) {
        fputs("[IPAC_ReadFiles]: Memory error", stderr);
        return 0;
    }

    const IPAC_Entry* entries = (const IPAC_Entry*)(buffer + header->dictionaryOffset);
    uint32_t handled = 0;
    for (uint32_t i = 0; i < header->count; i++) {
        const IPAC_Entry* entry = &entries[i];
        if ((uint64_t)entry->offset + entry->size > size) {
            if (verbose)
                printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X (out of bounds)\n", i, entry->filename, entry->extension, entry->offset, entry->size);
            continue;
        }

        // Decompress before printing the entry, raylib logs the decompression
        const uint8_t* data = buffer + entry->offset;
        size_t dataSize = entry->size;
        uint8_t* decompressed = NULL;
        if (File_IsGzip(data, dataSize)) {
            decompressed = File_GzipDecompress(data, dataSize, &dataSize);
            if (decompressed == NULL) {
                if (verbose)
                    printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X (decompression error)\n", i, entry->filename, entry->extension, entry->offset, entry->size);
                continue;
            }
            data = decompressed;
        }

        GetEntryFilename(filenames, i, entry, data, dataSize);
        if (verbose) {
            printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X", i, entry->filename, entry->extension, entry->offset, entry->size);
            if (decompressed != NULL)
                printf(" (gzip, 0x%04zX bytes)", dataSize);
            printf(" -> %s\n", filenames[i]);
        }
        if (callback(filenames[i], data, dataSize, context))
            handled++;

        MemFree(decompressed);
    }

    free(filenames);
    return handled;
}

static bool SaveFile(const char* filename, const uint8_t* data, size_t size, void* context) {
    char outputFilename[IPAC_PATH_SIZE];
    snprintf(outputFilename, sizeof(outputFilename), "%s/%s", (const char*)context, filename);
    return File_Save(outputFilename, data, size);
}

uint32_t IPAC_ExtractAllFiles(const uint8_t* buffer, size_t size, const char* outputFolder) {
    return ReadFiles(buffer, size, true, SaveFile, (void*)outputFolder);
}

static bool AddFile(const char* filename, const uint8_t* data, size_t size, void* context) {
    return IPAC_AddFile((IPAC_FileList*)context, filename, data, size);
}

uint32_t IPAC_LoadFiles(const uint8_t* buffer, size_t size, IPAC_FileList* list) {
    return ReadFiles(buffer, size, false, AddFile, list);
}

bool IPAC_AddFile(IPAC_FileList* list, const char* filename, const uint8_t* data, size_t size) {
    if (list->count == list->capacity) {
        uint32_t capacity = list->capacity > 0 ? list->capacity * 2 : 64;
        IPAC_File* files = realloc(list->files, capacity * sizeof(IPAC_File));
        if (files == NULL)
            return false;
        list->files = files;
        list->capacity = capacity;
    }
    IPAC_File* file = &list->files[list->count];
    file->data = malloc(size > 0 ? size : 1);
    if (file->data == NULL)
        return false;
    memcpy(file->data, data, size);
    file->size = size;
    snprintf(file->filename, sizeof(file->filename), "%s", filename);
    list->count++;
    return true;
}

void IPAC_FreeFiles(IPAC_FileList* list) {
    for (uint32_t i = 0; i < list->count; i++)
        free(list->files[i].data);
    free(list->files);
    memset(list, 0, sizeof(*list));
}

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "core/file.h"
#include "formats/archive/afs.h"
#include "formats/archive/idx.h"

typedef struct {
    char        filename[0x0C];
    uint16_t    afsIndex;
    uint16_t    afsLastIndex;
    uint32_t    unknown;
} IDX0_Entry;

typedef struct {
    char        id[4]; // "IDX0"
    uint16_t    count1;
    uint16_t    count;
    uint8_t     padding[12];
    IDX0_Entry  entries[];
} IDX0_Header;

typedef struct {
    char        filename[4];
} IDX_EntryHumans;

// Used by humans.idx, it has no identifier. After the filenames there is a table with "count" uint32 values
typedef struct {
    uint32_t        count;
    IDX_EntryHumans entries[];
} IDX_HeaderHumans;

static bool IsIDX0(const uint8_t* idx, size_t size) {
    if (size < sizeof(IDX0_Header) || strncmp((char*)idx, "IDX0", 4) != 0)
        return false;
    const IDX0_Header* header = (const IDX0_Header*)idx;
    return sizeof(IDX0_Header) + header->count * sizeof(IDX0_Entry) <= size;
}

static bool IsIDXHumans(const uint8_t* idx, size_t size) {
    if (size < sizeof(IDX_HeaderHumans))
        return false;
    const IDX_HeaderHumans* header = (const IDX_HeaderHumans*)idx;
    return header->count > 0 && sizeof(IDX_HeaderHumans) + (size_t)header->count * sizeof(IDX_EntryHumans) <= size;
}

static void PrintIDX0Header(IDX0_Header* header) {
    assert(strncmp(header->id, "IDX0", 4) == 0);

    printf("IDX0 Header:\n");
    printf("  ID: %.4s\n", header->id);
    printf("  Count1: %u\n", header->count1);
    printf("  Count: %u\n", header->count);
    for (uint32_t i = 0; i < header->count; i++) {
        IDX0_Entry* entry = &(header->entries[i]);
        printf("  Entry %03u: %.12s, AFS Index: %03u, AFS Last Index: %u, Unknown: %u\n", i, entry->filename, entry->afsIndex, entry->afsLastIndex, entry->unknown);
    }
}

static void PrintIDXHumansHeader(IDX_HeaderHumans* header) {
    printf("IDX Humans Header:\n");
    printf("  Count: %u\n", header->count);
    for (uint32_t i = 0; i < header->count; i++)
        printf("  Entry %03u: %.4s\n", i, header->entries[i].filename);
}

bool IDX_LoadEntryNames(const char* idxPath, char (*names)[IDX_NAME_SIZE], uint32_t afsCount) {
    size_t size = 0;
    uint8_t* idx = File_LoadToMemoryEx(idxPath, &size);
    if (idx == NULL)
        return false;

    bool loaded = false;
    if (IsIDX0(idx, size)) {
        IDX0_Header* header = (IDX0_Header*)idx;
        for (uint32_t i = 0; i < header->count; i++) {
            IDX0_Entry* entry = &(header->entries[i]);
            if (entry->afsIndex < afsCount)
                snprintf(names[entry->afsIndex], IDX_NAME_SIZE, "%.12s", entry->filename);
        }
        loaded = true;
    }
    else if (IsIDXHumans(idx, size)) {
        IDX_HeaderHumans* header = (IDX_HeaderHumans*)idx;
        // Each name can be used by several consecutive AFS entries (e.g. PAKF + PAKS for each human)
        uint32_t entriesPerName = afsCount % header->count == 0 ? afsCount / header->count : 1;
        for (uint32_t i = 0; i < afsCount && i / entriesPerName < header->count; i++)
            snprintf(names[i], IDX_NAME_SIZE, "%.4s", header->entries[i / entriesPerName].filename);
        loaded = true;
    }
    else
        fprintf(stderr, "[IDX_LoadEntryNames]: Unknown IDX file format: %s\n", idxPath);

    free(idx);
    return loaded;
}

void IDX_ExtractAllFiles(const char* filepath, const char* outputPath) {
    printf("[IDX_ExtractAllFiles]: Extracting file: %s\n", filepath);
    size_t size = 0;
    uint8_t* idx = File_LoadToMemoryEx(filepath, &size);
    if (idx == NULL)
        return;

    if (IsIDX0(idx, size))
        PrintIDX0Header((IDX0_Header*)idx);
    else if (IsIDXHumans(idx, size))
        PrintIDXHumansHeader((IDX_HeaderHumans*)idx);
    else
        fprintf(stderr, "Unknown IDX file format: %.4s\n", (char*)idx);
    free(idx);

    // The files are stored in the AFS container with the same name
    const char* lastDot = strrchr(filepath, '.');
    size_t baseLength = lastDot != NULL ? (size_t)(lastDot - filepath) : strlen(filepath);
    char* afsPath = (char*)malloc(baseLength + 5);
    if (afsPath == NULL) {
        fputs("[IDX_ExtractAllFiles]: Memory error", stderr);
        return;
    }
    memcpy(afsPath, filepath, baseLength);
    memcpy(afsPath + baseLength, ".afs", 5);
    AFS_ExtractAllFiles(afsPath, outputPath);
    free(afsPath);
}

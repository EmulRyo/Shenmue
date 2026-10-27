#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "core/dir.h"
#include "core/file.h"
#include "formats/archive/idx.h"
#include "formats/archive/afs.h"

#define AFS_PATH_SIZE 1024
#define AFS_FILENAME_SIZE 64

typedef struct {
    uint32_t    offset;
    uint32_t    size;
} AFS_Entry;

typedef struct {
    char        id[4]; // "AFS\0"
    uint32_t    count;
    AFS_Entry   entries[];
} AFS_Header;

// Loads the entry names from the IDX file with the same name as the AFS file, if it exists
static void LoadEntryNames(const char* filepath, char (*names)[IDX_NAME_SIZE], uint32_t count) {
    char idxPath[AFS_PATH_SIZE];
    const char* lastDot = strrchr(filepath, '.');
    int baseLength = (int)(lastDot != NULL && lastDot > File_GetFilename(filepath) ? lastDot - filepath : strlen(filepath));
    snprintf(idxPath, sizeof(idxPath), "%.*s.idx", baseLength, filepath);

    FILE* idxFile = fopen(idxPath, "rb");
    if (idxFile == NULL) {
        printf("  No IDX file found, the files will be named by index\n");
        return;
    }
    fclose(idxFile);

    if (IDX_LoadEntryNames(idxPath, names, count))
        printf("  Filenames loaded from: %s\n", idxPath);
}

static void GetEntryFilename(char (*filenames)[AFS_FILENAME_SIZE], uint32_t entryID, const char* name, const char* ext) {
    char* filename = filenames[entryID];
    if (name[0] != '\0')
        snprintf(filename, AFS_FILENAME_SIZE, "%s.%s", name, ext);
    else
        snprintf(filename, AFS_FILENAME_SIZE, "%04u.%s", entryID, ext);

    // Avoid overwriting a previous entry with the same name
    for (uint32_t i = 0; i < entryID; i++) {
        if (strcmp(filenames[i], filename) == 0) {
            snprintf(filename, AFS_FILENAME_SIZE, "%s_%04u.%s", name[0] != '\0' ? name : "file", entryID, ext);
            break;
        }
    }
}

static bool SaveEntry(FILE* afsFile, AFS_Entry* entry, const char* outputFilename) {
    uint8_t* buffer = (uint8_t*)malloc(entry->size);
    if (buffer == NULL) {
        fputs("[AFS_SaveEntry]: Memory error", stderr);
        return false;
    }
    fseek(afsFile, entry->offset, SEEK_SET);
    fread(buffer, 1, entry->size, afsFile);

    FILE* file = fopen(outputFilename, "wb");
    if (file == NULL) {
        fprintf(stderr, "[AFS_SaveEntry]: Error creating file %s\n", outputFilename);
        free(buffer);
        return false;
    }
    fwrite(buffer, 1, entry->size, file);
    fclose(file);
    free(buffer);
    return true;
}

static const char* GetEntryExtension(FILE* afsFile, AFS_Entry* entry) {
    uint8_t magic[12] = { 0 };
    fseek(afsFile, entry->offset, SEEK_SET);
    size_t read = fread(magic, 1, entry->size < sizeof(magic) ? entry->size : sizeof(magic), afsFile);
    const char* ext = File_ExtensionFromMagic(magic, read);
    return ext != NULL ? ext : "bin";
}

void AFS_ExtractAllFiles(const char* filepath, const char* outputPath) {
    printf("[AFS_ExtractAllFiles]: Extracting file: %s\n", filepath);
    FILE* afsFile = fopen(filepath, "rb");
    if (afsFile == NULL) {
        fprintf(stderr, "[AFS_ExtractAllFiles]: Error opening file %s\n", filepath);
        return;
    }
    fseek(afsFile, 0, SEEK_END);
    long fileSize = ftell(afsFile);
    rewind(afsFile);

    AFS_Header header;
    if (fread(&header, sizeof(AFS_Header), 1, afsFile) != 1 || memcmp(header.id, "AFS\0", 4) != 0) {
        fprintf(stderr, "[AFS_ExtractAllFiles]: Invalid AFS file %s\n", filepath);
        fclose(afsFile);
        return;
    }
    printf("AFS Header:\n");
    printf("  ID: %.4s\n", header.id);
    printf("  File Count: %u\n", header.count);

    AFS_Entry* entries = (AFS_Entry*)malloc(sizeof(AFS_Entry) * header.count);
    char (*names)[IDX_NAME_SIZE] = calloc(header.count, IDX_NAME_SIZE);
    char (*filenames)[AFS_FILENAME_SIZE] = calloc(header.count, AFS_FILENAME_SIZE);
    if (entries == NULL || names == NULL || filenames == NULL) {
        fputs("[AFS_ExtractAllFiles]: Memory error", stderr);
        free(entries);
        free(names);
        free(filenames);
        fclose(afsFile);
        return;
    }
    fread(entries, sizeof(AFS_Entry), header.count, afsFile);

    LoadEntryNames(filepath, names, header.count);

    char outputFolder[AFS_PATH_SIZE];
    File_GetOutputFolder(filepath, outputPath, outputFolder, sizeof(outputFolder));
    MakeDirs(outputFolder);
    printf("  Output folder: %s\n", outputFolder);

    uint32_t extracted = 0;
    for (uint32_t i = 0; i < header.count; i++) {
        AFS_Entry* entry = &entries[i];
        printf("  Entry %u: offset = 0x%04X, size = 0x%04X", i, entry->offset, entry->size);
        if (entry->size == 0) {
            printf(" (empty)\n");
            continue;
        }
        if ((uint64_t)entry->offset + entry->size > (uint64_t)fileSize) {
            printf(" (out of bounds)\n");
            continue;
        }

        GetEntryFilename(filenames, i, names[i], GetEntryExtension(afsFile, entry));
        printf(" -> %s\n", filenames[i]);

        char outputFilename[AFS_PATH_SIZE];
        snprintf(outputFilename, sizeof(outputFilename), "%s/%s", outputFolder, filenames[i]);
        if (SaveEntry(afsFile, entry, outputFilename))
            extracted++;
    }
    printf("[AFS_ExtractAllFiles]: Extracted %u/%u files to %s\n", extracted, header.count, outputFolder);

    free(entries);
    free(names);
    free(filenames);
    fclose(afsFile);
}

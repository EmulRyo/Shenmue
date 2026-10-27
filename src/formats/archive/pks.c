#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "core/dir.h"
#include "core/file.h"
#include "formats/archive/ipac.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"

#define PKS_PATH_SIZE 1024

typedef struct {
    char        id[4]; // "PAKS"
    uint32_t    ipacOffset;
    uint32_t    unknown1;
    uint32_t    unknown2;
} PKS_Header;

void PKS_ExtractAllFiles(const char* filepath, const char* outputPath) {
    printf("[PKS_ExtractAllFiles]: Extracting file: %s\n", filepath);
    size_t size = 0;
    uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
    if (buffer == NULL)
        return;

    // Some files have the wrong extension
    if (size >= 4 && memcmp(buffer, "PAKF", 4) == 0) {
        printf("[PKS_ExtractAllFiles]: The file is a PKF file\n");
        free(buffer);
        PKF_ExtractAllFiles(filepath, outputPath);
        return;
    }

    const PKS_Header* header = (const PKS_Header*)buffer;
    if (size < sizeof(PKS_Header) || memcmp(header->id, "PAKS", 4) != 0 || header->ipacOffset > size) {
        fprintf(stderr, "[PKS_ExtractAllFiles]: Invalid PKS file %s\n", filepath);
        free(buffer);
        return;
    }
    printf("PKS Header:\n");
    printf("  ID: %.4s\n", header->id);
    printf("  IPAC Offset: 0x%04X\n", header->ipacOffset);
    printf("  Unknown1: 0x%08X\n", header->unknown1);
    printf("  Unknown2: 0x%08X\n", header->unknown2);

    char outputFolder[PKS_PATH_SIZE];
    File_GetOutputFolder(filepath, outputPath, outputFolder, sizeof(outputFolder));
    MakeDirs(outputFolder);
    printf("  Output folder: %s\n", outputFolder);

    uint32_t extracted = IPAC_ExtractAllFiles(buffer + header->ipacOffset, size - header->ipacOffset, outputFolder);
    printf("[PKS_ExtractAllFiles]: Extracted %u files to %s\n", extracted, outputFolder);

    free(buffer);
}

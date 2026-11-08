#ifndef __IPAC_H__
#define __IPAC_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define IPAC_FILENAME_SIZE 32
#define IPAC_TYPE_SIZE     5

// File of an IPAC block (or of the PKF/PKS file that contains it) loaded in memory, decompressed and named as when it
// is extracted
typedef struct {
    char        filename[IPAC_FILENAME_SIZE];
    char        type[IPAC_TYPE_SIZE]; // Extension in the IPAC dictionary, in uppercase (e.g. "MAPM", "CHRM"), empty if
                                      // the file isn't from the dictionary (the textures of the PKF files)
    uint8_t*    data;
    size_t      size;
} IPAC_File;

typedef struct {
    uint32_t    count;
    uint32_t    capacity;
    IPAC_File*  files;
} IPAC_FileList;

// Extracts the files of an IPAC block (contained in PKF/PKS files) into outputFolder, gzipped files are decompressed.
// Returns the number of extracted files
uint32_t IPAC_ExtractAllFiles(const uint8_t* buffer, size_t size, const char* outputFolder);
// Adds the files of an IPAC block to list. Returns the number of added files
uint32_t IPAC_LoadFiles(const uint8_t* buffer, size_t size, IPAC_FileList* list);
// Adds a copy of data to list. Returns false on memory error
bool IPAC_AddFile(IPAC_FileList* list, const char* filename, const uint8_t* data, size_t size);
void IPAC_FreeFiles(IPAC_FileList* list);

#endif

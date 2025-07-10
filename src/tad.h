#ifndef __TAD_H__
#define __TAD_H__

typedef struct StringBuilderInstance* StringBuilder;

typedef struct TADHeader {
    uint32_t    identifier[4];
    __time64_t  timestamp;
    char        graphicsAPI[8];
    uint32_t    checksum;
    uint32_t    unknown;
    uint64_t    tacFileSize;
    uint64_t    fileCount;
    uint32_t    fileCountCopy;
} TADHeader;

typedef struct TADFileEntry {
    uint32_t    hash[3];
    uint32_t    empty;
    uint32_t    offset;
    uint32_t    unknow1;
    uint32_t    size;
    uint32_t    unknow2;
} TADFileEntry;

uint8_t*      TAD_Load(const char* filepath);
void          TAD_ExtractAllFiles(const char* tadFilePath, HashTable filenameDB, const char* outputPath, bool appendBasename);
size_t        TAD_GetBasename(const char* filepath, StringBuilder basename);
TADFileEntry* TAD_GetEntry(uint8_t* buffer, size_t entryID);
void          TAD_PrintEntry(TADFileEntry* entry, size_t entryID, size_t totalEntries, const char* startBuffer);
size_t        TAD_GetTACFilename(const char* tadFilename, StringBuilder tacFilename);
size_t        TAD_GetEntryRelPath(TADFileEntry* entry, HashTable filenameDB, StringBuilder sb);
void          TAD_PrintHeader(TADHeader* header);

#endif
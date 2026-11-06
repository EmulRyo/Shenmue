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

typedef struct TAD_Context {
    // Used as input
    const char* tadFilepath;
    HashTable filenameDB;
    const char* outputPath; // The entries are extracted to outputPath followed by their path in the game

    // Used internally
    size_t entryID;
    StringBuilder sb;
    FILE* tacFile;
    uint8_t* buffer;
}TAD_Context;

void          TAD_ExtractAllFiles(const char* tadFilePath, HashTable filenameDB, const char* outputPath);
TADFileEntry* TAD_GetEntry(uint8_t* buffer, size_t entryID);
void          TAD_PrintEntry(TADFileEntry* entry, size_t entryID, size_t totalEntries, const char* startBuffer);
size_t        TAD_GetTACFilename(const char* tadFilename, StringBuilder tacFilename);
size_t        TAD_GetEntryRelPath(TADFileEntry* entry, HashTable filenameDB, StringBuilder sb);
void          TAD_PrintHeader(TADHeader* header);
// Writes data, the data of the entry, to outputPath followed by its path in the game, creating its folder unless it is in
// dirCache (see DirCache_Create; it may be NULL). sb is cleared. False if the entry is empty or can't be written.
// Thread-safe as long as each thread has its own sb and dirCache
bool          TAD_SaveEntry(TADFileEntry* entry, const uint8_t* data, HashTable filenameDB, StringBuilder sb,
                  const char* outputPath, HashTable dirCache);
// Extracts the entry from the TAC file to outputPath followed by its path in the game. sb is cleared
void          TAD_ExtractFile(TADFileEntry* entry, HashTable filenameDB, StringBuilder sb, FILE* tacFile, const char* outputPath);

void          TAD_ContextInit(TAD_Context* ctx,
                  const char* tadFilepath, HashTable filenameDB, const char* outputPath);
size_t        TAD_ExtractAllFilesStart(TAD_Context* ctx);
size_t        TAD_ExtractAllFilesNext(TAD_Context* ctx);
void          TAD_ExtractAllFilesEnd(TAD_Context* ctx);

#endif
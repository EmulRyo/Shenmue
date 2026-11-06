#ifndef __TAC_H__
#define __TAC_H__

typedef struct StringBuilderInstance* StringBuilder;

struct TADFileEntry;

FILE*    TAC_Open(const char* filepath);
void     TAC_Close(FILE* tacFile);
// Reads the data of the entry into buffer, of at least entry->size bytes
bool     TAC_ReadFile(FILE* tacFile, struct TADFileEntry* entry, uint8_t* buffer);
// The data of the entry in a new buffer, NULL if it is empty
uint8_t* TAC_GetFile(FILE* tacFile, struct TADFileEntry* entry);
// Writes the data to outputFilename, adding the extension of its magic if the name has none. False if size is 0 or it
// can't be written
bool     TAC_WriteFile(const uint8_t* data, uint32_t size, const char* outputFilename);
bool     TAC_SaveFile(FILE* tacFile, struct TADFileEntry* entry, const char* outputFilename);

#endif
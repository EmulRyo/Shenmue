#ifndef __TAC_H__
#define __TAC_H__

typedef struct StringBuilderInstance* StringBuilder;

struct TADFileEntry;

FILE*    TAC_Open(const char* filepath);
void     TAC_Close(FILE* tacFile);
uint8_t* TAC_GetFile(FILE* tacFile, struct TADFileEntry* entry);
bool     TAC_SaveFile(FILE* tacFile, struct TADFileEntry* entry, const char* outputFilename, StringBuilder sb);

#endif
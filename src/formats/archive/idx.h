#ifndef __IDX_H__
#define __IDX_H__

#include <stdbool.h>
#include <stdint.h>

#define IDX_NAME_SIZE 16

// Fills "names" (an array of "afsCount" elements) with the filename of each AFS entry, unnamed entries are not modified
bool IDX_LoadEntryNames(const char* idxPath, char (*names)[IDX_NAME_SIZE], uint32_t afsCount);
// Prints the IDX contents and extracts the files of the AFS container with the same name
void IDX_ExtractAllFiles(const char* filepath, const char* outputPath);

#endif

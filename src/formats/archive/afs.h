#ifndef __AFS_H__
#define __AFS_H__

#include <stddef.h>
#include <stdint.h>

// Extracts the files into a folder named as the container, inside outputPath or next to the AFS file if outputPath is empty
void AFS_ExtractAllFiles(const char* filepath, const char* outputPath);
// Loads the entry with this name (case-insensitive, without extension) of the IDX file with the same name as the AFS
// file. Returns the data (to be freed with free), or NULL if there is no such entry
uint8_t* AFS_LoadNamedEntry(const char* filepath, const char* name, size_t* size);
// Loads the entry with this index. count is the number of entries, 0 if the file isn't a valid AFS file. Returns the
// data (to be freed with free), or NULL if the entry doesn't exist or is empty
uint8_t* AFS_LoadEntry(const char* filepath, uint32_t index, size_t* size, uint32_t* count);

#endif
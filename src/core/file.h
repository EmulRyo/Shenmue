#ifndef __FILE_H__
#define __FILE_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint8_t*    File_LoadToMemory(const char* filepath);
uint8_t*    File_LoadToMemoryEx(const char* filepath, size_t* outSize);
const char* File_ExtensionFromMagic(const uint8_t* buffer, size_t size); // Returns NULL if the format is unknown
bool        File_IsGzip(const uint8_t* buffer, size_t size);
uint8_t*    File_GzipDecompress(const uint8_t* buffer, size_t size, size_t* outSize); // Returns NULL on error
uint8_t*    File_LoadToMemoryDecompressed(const char* filepath, size_t* outSize); // Decompresses the file if it is gzipped
bool        File_Save(const char* filepath, const uint8_t* buffer, size_t size);
const char* File_GetFilename(const char* filepath); // Returns the position after the last path separator
// Output folder: "outputPath/<file basename>" or "<file folder>/<file basename>" if no outputPath is given
void        File_GetOutputFolder(const char* filepath, const char* outputPath, char* outputFolder, size_t size);

#endif

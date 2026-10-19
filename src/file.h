#ifndef __FILE_H__
#define __FILE_H__

#include <stdint.h>
#include <stddef.h>

uint8_t*    File_LoadToMemory(const char* filepath);
uint8_t*    File_LoadToMemoryEx(const char* filepath, size_t* outSize);
const char* File_ExtensionFromMagic(const uint8_t* buffer, size_t size); // Returns NULL if the format is unknown

#endif

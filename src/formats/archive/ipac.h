#ifndef __IPAC_H__
#define __IPAC_H__

#include <stddef.h>
#include <stdint.h>

// Extracts the files of an IPAC block (contained in PKF/PKS files) into outputFolder, gzipped files are decompressed.
// Returns the number of extracted files
uint32_t IPAC_ExtractAllFiles(const uint8_t* buffer, size_t size, const char* outputFolder);

#endif

#ifndef __PKF_H__
#define __PKF_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/archive/ipac.h"

// Extracts the textures (TEXN) and the IPAC files of a PKF file (gzipped or not) into a folder named as the file,
// inside outputPath or next to the PKF file if outputPath is empty
void PKF_ExtractAllFiles(const char* filepath, const char* outputPath);
// Adds the textures and the IPAC files of a PKF file (gzipped or not) to list, named as when they are extracted (the
// textures as tex_<index>_<texture ID>.<ext>). Returns false if the file is not valid
bool PKF_LoadFiles(const char* filepath, IPAC_FileList* list);
// The same for a PKF file already in memory (decompressed, e.g. an entry of an AFS file). name is printed in the errors
bool PKF_LoadFilesFromMemory(const uint8_t* buffer, size_t size, const char* name, IPAC_FileList* list);

#endif

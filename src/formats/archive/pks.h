#ifndef __PKS_H__
#define __PKS_H__

#include <stdbool.h>
#include "formats/archive/ipac.h"

// Extracts the IPAC files of a PKS file (gzipped or not) into a folder named as the file,
// inside outputPath or next to the PKS file if outputPath is empty
void PKS_ExtractAllFiles(const char* filepath, const char* outputPath);
// Adds the files of a PKS file (gzipped or not) to list, named as when they are extracted. Returns false if
// the file is not valid
bool PKS_LoadFiles(const char* filepath, IPAC_FileList* list);

#endif

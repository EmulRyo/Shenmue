#ifndef __PKF_H__
#define __PKF_H__

// Extracts the textures (TEXN) and the IPAC files of a PKF file (gzipped or not) into a folder named as the file,
// inside outputPath or next to the PKF file if outputPath is empty
void PKF_ExtractAllFiles(const char* filepath, const char* outputPath);

#endif

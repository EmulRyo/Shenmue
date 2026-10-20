#ifndef __PKS_H__
#define __PKS_H__

// Extracts the IPAC files of a PKS file (gzipped or not) into a folder named as the file,
// inside outputPath or next to the PKS file if outputPath is empty
void PKS_ExtractAllFiles(const char* filepath, const char* outputPath);

#endif

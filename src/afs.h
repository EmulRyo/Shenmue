#ifndef __AFS_H__
#define __AFS_H__

// Extracts the files into a folder named as the container, inside outputPath or next to the AFS file if outputPath is empty
void AFS_ExtractAllFiles(const char* filepath, const char* outputPath);

#endif
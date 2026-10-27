#ifndef __TADBATCH_H__
#define __TADBATCH_H__

#include <stddef.h>
#include "core/hashTable.h"

// Extraction of all the TAD files of a folder into "content", one entry per frame so the window keeps responding

typedef struct {
    unsigned int    file;       // Index of the TAD file being extracted
    unsigned int    fileCount;
    size_t          entry;      // Index of the entry being extracted
    size_t          entryCount;
} TADBatch_Progress;

// Starts extracting the TAD files of folder, naming the entries with filenameDB (see FilenameDB_Load)
void TADBatch_Start(const char* folder, HashTable filenameDB);
// Shows the folder dialog and starts extracting the TAD files of the selected folder
void TADBatch_Dialog(HashTable filenameDB);
// Extracts the next entry, if an extraction is running
void TADBatch_Update(void);
TADBatch_Progress TADBatch_GetProgress(void);

#endif

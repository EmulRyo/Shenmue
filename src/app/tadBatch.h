#ifndef __TADBATCH_H__
#define __TADBATCH_H__

#include <stdbool.h>
#include <stddef.h>
#include "core/hashTable.h"

// Extraction of a list of TAD files into "content", one entry per frame so the window keeps responding

typedef struct {
    bool            running;    // An extraction is running
    unsigned int    file;       // Index of the TAD file being extracted
    unsigned int    fileCount;
    size_t          entry;      // Index of the entry being extracted
    size_t          entryCount;
} TADBatch_Progress;

// Starts extracting the count TAD files, naming the entries with filenameDB (see FilenameDB_Load). Does nothing if an
// extraction is already running. The paths are copied
void TADBatch_Start(const char* const* files, unsigned int count, HashTable filenameDB);
// Shows a dialog to select one or several TAD files and starts extracting them
void TADBatch_Dialog(HashTable filenameDB);
// Extracts the next entry, if an extraction is running
void TADBatch_Update(void);
TADBatch_Progress TADBatch_GetProgress(void);

#endif

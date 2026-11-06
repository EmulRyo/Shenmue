#ifndef __TADBATCH_H__
#define __TADBATCH_H__

#include <stdbool.h>
#include <stddef.h>
#include "core/hashTable.h"

// Extraction of a list of TAD files into a folder, by worker threads so the window keeps responding. The entries of all
// the files are extracted to the same tree, at their path in the game

typedef struct {
    bool            running;    // An extraction is running
    bool            finished;   // The last extraction ended and its result was not dismissed
    unsigned int    file;       // Index of the TAD file being extracted
    unsigned int    fileCount;
    size_t          extracted;  // Entries extracted, from all the files
    size_t          total;      // Entries of all the files
    double          seconds;    // Since the extraction started, or its duration if it ended
    const char*     outputPath; // Folder of the running or last extraction, NULL before the first one
} TADBatch_Progress;

// Starts extracting the count TAD files to outputPath, naming the entries with filenameDB (see FilenameDB_Load). Does
// nothing if an extraction is already running. The paths are copied
void TADBatch_Start(const char* const* files, unsigned int count, const char* outputPath, HashTable filenameDB);
// Shows a dialog to select one or several TAD files, then one to pick the output folder, and starts extracting them
void TADBatch_Dialog(HashTable filenameDB);
// Ends the extraction when all the entries are extracted. Called every frame
void TADBatch_Update(void);
// Cancels the extraction, if one is running, and waits for the threads to end
void TADBatch_Stop(void);
TADBatch_Progress TADBatch_GetProgress(void);
// Opens the output folder in the file explorer
void TADBatch_OpenOutput(void);
// Hides the result of the last extraction
void TADBatch_Dismiss(void);

#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nfd.h>
#include <raylib.h>
#include "core/dir.h"
#include "core/file.h"
#include "core/hashTable.h"
#include "core/stringBuilder.h"
#include "core/thread.h"
#include "formats/archive/tac.h"
#include "formats/archive/tad.h"
#include "app/tadBatch.h"

// The cost of extracting a file is mostly creating it, not reading or writing its data, so several threads extracting at
// the same time are faster than one. More than 4 don't make it faster
#define WORKER_COUNT 4

// Each job is the data of one or several entries of a TAD file: the entries that share it (same offset and size) are
// read once and written once each. The jobs of a TAD go in the order of their data in the TAC file, and those of all the
// TAD files one after another. Each worker takes the next job until there are no more
typedef struct {
    uint32_t    offset;
    uint32_t    size;
    uint32_t    entry;      // Index in the TAD file
} BatchEntry;

typedef struct {
    unsigned int    tad;
    size_t          first;  // Of its entries in g_entries
    size_t          count;
} BatchJob;

typedef struct {
    uint8_t*    buffer;     // The TAD file, NULL if it couldn't be loaded
    char*       tacPath;
} BatchTAD;

static HashTable g_filenameDB;
static BatchTAD* g_tads;
static unsigned int g_tadCount;
static BatchEntry* g_entries;   // Of all the TAD files
static size_t g_entryCount;
static BatchJob* g_jobs;
static size_t g_jobCount;
static char* g_outputPath;      // Folder of the running or last extraction
static Thread g_workers[WORKER_COUNT];
static AtomicInt g_nextJob;
static AtomicInt g_done;        // Entries finished
static AtomicInt g_cancel;
static bool g_running;
static bool g_finished;         // The last extraction ended and its result was not dismissed
static size_t g_extracted;      // Entries extracted by the last extraction
static double g_startTime;
static double g_seconds;        // Duration of the last extraction

static void Worker(void* arg) {
    (void)arg;
    StringBuilder sb = SB_Create(512);
    HashTable dirCache = DirCache_Create();
    uint8_t* data = NULL;
    uint32_t capacity = 0;
    FILE* tacFile = NULL;
    unsigned int tacID = UINT32_MAX;
    while (!Atomic_Load(&g_cancel)) {
        size_t jobID = (size_t)Atomic_FetchIncrement(&g_nextJob);
        if (jobID >= g_jobCount)
            break;
        BatchJob* job = &g_jobs[jobID];
        BatchTAD* tad = &g_tads[job->tad];
        if (job->tad != tacID) {
            if (tacFile != NULL)
                fclose(tacFile);
            tacFile = fopen(tad->tacPath, "rb");
            if (tacFile == NULL)
                fprintf(stderr, "Error: can't open %s\n", tad->tacPath);
            tacID = job->tad;
        }
        TADFileEntry* first = TAD_GetEntry(tad->buffer, g_entries[job->first].entry);
        if (tacFile != NULL && first->size > 0) {
            // One buffer for all the jobs, grown when needed
            if (first->size > capacity) {
                free(data);
                capacity = first->size;
                data = malloc(capacity);
            }
            if (data != NULL && TAC_ReadFile(tacFile, first, data)) {
                for (size_t i = 0; i < job->count; i++) {
                    TADFileEntry* entry = TAD_GetEntry(tad->buffer, g_entries[job->first + i].entry);
                    TAD_SaveEntry(entry, data, g_filenameDB, sb, g_outputPath, dirCache);
                }
            }
            else
                fprintf(stderr, "Error: can't read entry %u of %s\n", g_entries[job->first].entry, tad->tacPath);
        }
        Atomic_FetchAdd(&g_done, (long)job->count);
    }
    if (tacFile != NULL)
        fclose(tacFile);
    free(data);
    HT_Destroy(dirCache);
    SB_Destroy(sb);
}

static void FreeTADs(void) {
    for (unsigned int i = 0; i < g_tadCount; i++) {
        free(g_tads[i].buffer);
        free(g_tads[i].tacPath);
    }
    free(g_tads);
    free(g_entries);
    free(g_jobs);
    g_tads = NULL;
    g_entries = NULL;
    g_jobs = NULL;
    g_tadCount = 0;
    g_entryCount = 0;
    g_jobCount = 0;
}

static void JoinWorkers(void) {
    for (int i = 0; i < WORKER_COUNT; i++) {
        Thread_Join(g_workers[i]);
        g_workers[i] = NULL;
    }
}

static int CompareEntries(const void* a, const void* b) {
    const BatchEntry* entryA = a;
    const BatchEntry* entryB = b;
    if (entryA->offset != entryB->offset)
        return entryA->offset < entryB->offset ? -1 : 1;
    if (entryA->size != entryB->size)
        return entryA->size < entryB->size ? -1 : 1;
    return entryA->entry < entryB->entry ? -1 : (entryA->entry > entryB->entry);
}

// Loads the TAD file into tad and returns its number of entries, 0 if it can't be loaded
static size_t LoadTAD(BatchTAD* tad, const char* path, StringBuilder sb) {
    size_t size = 0;
    tad->buffer = File_LoadToMemoryEx(path, &size);
    if (tad->buffer == NULL || size < sizeof(TADHeader))
        return 0;
    size_t entryCount = (size_t)TAD_GetFileCount(tad->buffer);
    if (0x3C + entryCount * 0x20 > size) {
        fprintf(stderr, "Error: %s is too small for its %zu entries\n", path, entryCount);
        return 0;
    }
    size_t tacOffset = TAD_GetTACFilename(path, sb);
    if (tacOffset == SIZE_MAX)
        return 0;
    const char* tacPath = SB_CStrOffset(sb, tacOffset);
    size_t length = strlen(tacPath) + 1;
    tad->tacPath = memcpy(malloc(length), tacPath, length);
    SB_Clear(sb);
    return entryCount;
}

void TADBatch_Start(const char* const* files, unsigned int count, const char* outputPath, HashTable filenameDB) {
    if (g_running || count == 0)
        return;
    g_filenameDB = filenameDB;
    // Without the separator at the end, as the paths of the entries start with one
    size_t pathLength = strlen(outputPath);
    while (pathLength > 0 && (outputPath[pathLength - 1] == '/' || outputPath[pathLength - 1] == '\\'))
        pathLength--;
    free(g_outputPath);
    g_outputPath = malloc(pathLength + 1);
    memcpy(g_outputPath, outputPath, pathLength);
    g_outputPath[pathLength] = '\0';

    // The TAD files are small: all of them are loaded before starting
    g_tads = calloc(count, sizeof(BatchTAD));
    g_tadCount = count;
    size_t* entryCounts = calloc(count, sizeof(size_t));
    StringBuilder sb = SB_Create(512);
    g_entryCount = 0;
    for (unsigned int i = 0; i < count; i++) {
        entryCounts[i] = LoadTAD(&g_tads[i], files[i], sb);
        g_entryCount += entryCounts[i];
    }
    SB_Destroy(sb);

    // The entries of each TAD sorted by their data, and a job for each run of entries with the same data
    g_entries = malloc((g_entryCount > 0 ? g_entryCount : 1) * sizeof(BatchEntry));
    g_jobs = malloc((g_entryCount > 0 ? g_entryCount : 1) * sizeof(BatchJob));
    g_jobCount = 0;
    size_t first = 0;
    for (unsigned int i = 0; i < count; i++) {
        BatchEntry* entries = &g_entries[first];
        for (size_t e = 0; e < entryCounts[i]; e++) {
            TADFileEntry* entry = TAD_GetEntry(g_tads[i].buffer, e);
            entries[e] = (BatchEntry) { entry->offset, entry->size, (uint32_t)e };
        }
        qsort(entries, entryCounts[i], sizeof(BatchEntry), CompareEntries);
        for (size_t e = 0; e < entryCounts[i]; e++) {
            BatchJob* last = g_jobCount > 0 ? &g_jobs[g_jobCount - 1] : NULL;
            BatchEntry* lastEntry = last != NULL ? &g_entries[last->first] : NULL;
            if (e > 0 && lastEntry->offset == entries[e].offset && lastEntry->size == entries[e].size)
                last->count++;
            else
                g_jobs[g_jobCount++] = (BatchJob) { i, first + e, 1 };
        }
        first += entryCounts[i];
    }
    free(entryCounts);

    printf("Extracting %zu files (%zu different) from %u TAD files to %s\n", g_entryCount, g_jobCount, count, g_outputPath);
    Atomic_Store(&g_nextJob, 0);
    Atomic_Store(&g_done, 0);
    Atomic_Store(&g_cancel, 0);
    g_finished = false;
    g_running = true;
    g_startTime = GetTime();
    int created = 0;
    for (int i = 0; i < WORKER_COUNT; i++)
        if ((g_workers[i] = Thread_Create(Worker, NULL)) != NULL)
            created++;
    if (created == 0)
        Worker(NULL); // No threads: all of it now, with the window frozen
}

void TADBatch_Dialog(HashTable filenameDB) {
    if (g_running)
        return;
    const nfdpathset_t* pathSet;
    nfdu8filteritem_t filters[1] = { { "TAD archives", "tad" } };
    nfdresult_t result = NFD_OpenDialogMultipleU8(&pathSet, filters, 1, NULL);
    if (result == NFD_OKAY) {
        nfdpathsetsize_t count = 0;
        NFD_PathSet_GetCount(pathSet, &count);
        nfdu8char_t** paths = calloc(count, sizeof(nfdu8char_t*));
        unsigned int valid = 0;
        for (nfdpathsetsize_t i = 0; i < count; i++)
            if (NFD_PathSet_GetPathU8(pathSet, i, &paths[valid]) == NFD_OKAY)
                valid++;
        // The output folder, starting at the one of the last extraction
        nfdu8char_t* outputPath = NULL;
        result = NFD_PickFolderU8(&outputPath, g_outputPath);
        if (result == NFD_OKAY) {
            TADBatch_Start((const char* const*)paths, valid, outputPath, filenameDB);
            NFD_FreePathU8(outputPath);
        }
        else if (result == NFD_ERROR) {
            printf("Error: %s\n", NFD_GetError());
        }
        for (unsigned int i = 0; i < valid; i++)
            NFD_PathSet_FreePathU8(paths[i]);
        free(paths);
        NFD_PathSet_Free(pathSet);
    }
    else if (result == NFD_ERROR) {
        printf("Error: %s\n", NFD_GetError());
    }
}

void TADBatch_Update(void) {
    if (!g_running || (size_t)Atomic_Load(&g_done) < g_entryCount)
        return;
    JoinWorkers();
    g_running = false;
    g_finished = true;
    g_extracted = g_entryCount;
    g_seconds = GetTime() - g_startTime;
    printf("Extracted %zu files from %u TAD files in %.1f s to %s\n", g_extracted, g_tadCount, g_seconds, g_outputPath);
    FreeTADs();
}

void TADBatch_Stop(void) {
    if (!g_running)
        return;
    Atomic_Store(&g_cancel, 1);
    JoinWorkers();
    g_running = false;
    printf("TAD extraction cancelled after %ld files\n", Atomic_Load(&g_done));
    FreeTADs();
}

TADBatch_Progress TADBatch_GetProgress(void) {
    TADBatch_Progress progress = { 0 };
    progress.running = g_running;
    progress.finished = g_finished;
    progress.outputPath = g_outputPath;
    if (g_running) {
        progress.extracted = (size_t)Atomic_Load(&g_done);
        progress.total = g_entryCount;
        progress.fileCount = g_tadCount;
        size_t next = (size_t)Atomic_Load(&g_nextJob);
        if (g_jobCount > 0)
            progress.file = g_jobs[next < g_jobCount ? next : g_jobCount - 1].tad;
        progress.seconds = GetTime() - g_startTime;
    }
    else {
        progress.extracted = g_extracted;
        progress.total = g_extracted;
        progress.seconds = g_seconds;
    }
    return progress;
}

void TADBatch_OpenOutput(void) {
    if (g_outputPath != NULL && OpenDir(g_outputPath) != 0)
        printf("Error: can't open %s\n", g_outputPath);
}

void TADBatch_Dismiss(void) {
    g_finished = false;
}

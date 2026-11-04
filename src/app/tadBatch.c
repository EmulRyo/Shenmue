#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nfd.h>
#include <raylib.h>
#include "core/hashTable.h"
#include "formats/archive/tad.h"
#include "app/tadBatch.h"

static HashTable g_filenameDB;
static TAD_Context g_TADCtx;
static size_t g_tadEntriesCount;
static char** g_files;
static unsigned int g_fileCount;
static unsigned int g_fileID;

static void FreeFiles(void) {
    for (unsigned int i = 0; i < g_fileCount; i++)
        free(g_files[i]);
    free(g_files);
    g_files = NULL;
    g_fileCount = 0;
}

static void ExtractStart(void) {
    const char* filename = g_files[g_fileID];
    printf("Loading TAD file: %s\n", filename);
    TAD_ContextInit(&g_TADCtx, filename, g_filenameDB, "content", true);
    g_tadEntriesCount = TAD_ExtractAllFilesStart(&g_TADCtx);
}

void TADBatch_Start(const char* const* files, unsigned int count, HashTable filenameDB) {
    if (g_TADCtx.buffer != NULL || count == 0)
        return;
    g_filenameDB = filenameDB;
    g_files = malloc(count * sizeof(char*));
    for (unsigned int i = 0; i < count; i++) {
        size_t length = strlen(files[i]) + 1;
        g_files[i] = memcpy(malloc(length), files[i], length);
    }
    g_fileCount = count;
    g_fileID = 0;
    ExtractStart();
}

void TADBatch_Dialog(HashTable filenameDB) {
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
        TADBatch_Start((const char* const*)paths, valid, filenameDB);
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
    if (g_TADCtx.buffer == NULL)
        return;

    size_t entryID = TAD_ExtractAllFilesNext(&g_TADCtx);
    if (entryID == SIZE_MAX) {
        TAD_ExtractAllFilesEnd(&g_TADCtx);
        g_fileID++;
        if (g_fileID < g_fileCount)
            ExtractStart();
        else {
            printf("All TAD files extracted.\n");
            FreeFiles();
        }
    }
}

TADBatch_Progress TADBatch_GetProgress(void) {
    return (TADBatch_Progress) { g_TADCtx.buffer != NULL, g_fileID, g_fileCount, g_TADCtx.entryID, g_tadEntriesCount };
}

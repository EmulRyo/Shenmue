#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <nfd.h>
#include <raylib.h>
#include "core/hashTable.h"
#include "formats/archive/tad.h"
#include "app/tadBatch.h"

static HashTable g_filenameDB;
static TAD_Context g_TADCtx;
static size_t g_tadEntriesCount;
static FilePathList g_fileList;
static unsigned int g_fileID;

void TADBatch_Start(const char* folder, HashTable filenameDB) {
    g_filenameDB = filenameDB;
    g_fileList = LoadDirectoryFilesEx(folder, ".tad", false);
    if (g_fileList.count > 0) {
        const char* filename = g_fileList.paths[0];
        printf("Loading TAD file: %s\n", filename);
        TAD_ContextInit(&g_TADCtx, filename, g_filenameDB, "content", true);
        g_tadEntriesCount = TAD_ExtractAllFilesStart(&g_TADCtx);
        g_fileID = 0;
    }
    else
        UnloadDirectoryFiles(g_fileList);
}

void TADBatch_Dialog(HashTable filenameDB) {
    nfdu8char_t* outPath;
    nfdresult_t result = NFD_PickFolder(&outPath, NULL);
    if (result == NFD_OKAY) {
        TADBatch_Start(outPath, filenameDB);
        NFD_FreePathU8(outPath);
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
        if (g_fileID < g_fileList.count) {
            const char* filename = g_fileList.paths[g_fileID];
            printf("Loading TAD file: %s\n", filename);
            TAD_ContextInit(&g_TADCtx, filename, g_filenameDB, "content", true);
            g_tadEntriesCount = TAD_ExtractAllFilesStart(&g_TADCtx);
        }
        else {
            printf("All TAD files extracted.\n");
            UnloadDirectoryFiles(g_fileList);
        }
    }
}

TADBatch_Progress TADBatch_GetProgress(void) {
    return (TADBatch_Progress) { g_fileID, g_fileList.count, g_TADCtx.entryID, g_tadEntriesCount };
}

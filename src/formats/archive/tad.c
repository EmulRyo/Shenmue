#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/dir.h"
#include "core/hashTable.h"
#include "core/stringBuilder.h"
#include "formats/archive/tac.h"
#include "core/file.h"
#include "formats/archive/tad.h"

size_t TAD_GetBasename(const char* filepath, StringBuilder sb) {
    char* lastSlash = strrchr(filepath, '\\');
    size_t length = (filepath + strlen(filepath)) - lastSlash;
	size_t offset = SB_Count(sb);
	SB_AppendSubstring(sb, filepath, (lastSlash + 1) - filepath, length - 5);
	SB_AppendChar(sb, '\0');
	return offset;
}

TADFileEntry* TAD_GetEntry(uint8_t* buffer, size_t entryID) {
    TADFileEntry* entry = (TADFileEntry*)(buffer + 0x3C + entryID * 0x20);
	return entry;
}

void TAD_PrintEntry(TADFileEntry* entry, size_t entryID, size_t totalEntries, const char* startBuffer) {
    printf("  File %llu (%.2f%%) (0x%04X):\n", entryID, (float)(entryID + 1) / totalEntries * 100.0f, (int)((char*)entry - startBuffer));
    printf("    Hash1: %lu (0x%08X)\n", entry->hash[0], entry->hash[0]);
    //printf("    Hash2: %lu (0x%08X)\n", entry->hash[1], entry->hash[1]);
    //printf("    Hash3: %lu (0x%08X)\n", entry->hash[2], entry->hash[2]);
    printf("    Offset: %u\n", entry->offset);
    uint32_t bytes = entry->size;
	float size = (float)bytes;
	const char* units[] = { "bytes", "KB", "MB", "GB" };
	int unitID = 0;
    while (size >= 1024 && unitID < 3) {
        size /= 1024;
        unitID++;
	}
    if (bytes > 1024)
        printf("    Size: %.2f %s (%u bytes)\n", size, units[unitID], bytes);
    else
        printf("    Size: %u bytes\n", bytes);
}

size_t TAD_GetTACFilename(const char* tadFilename, StringBuilder sb) {
    const char* lastDot = strrchr(tadFilename, '.');
    if (lastDot == NULL)
        return SIZE_MAX;

	size_t offset = SB_Count(sb);
	SB_AppendSubstring(sb, tadFilename, 0, lastDot - tadFilename + 1);
	SB_AppendStr(sb, "tac");
	SB_AppendChar(sb, '\0');

    return offset;
}

static size_t LastIndexOfChar(const char* str, const char* end, char c) {
    const char* last = end;
    while (last >= str) {
        if (*last == c)
            return (ptrdiff_t)(last - str);
        last--;
    }
	return SIZE_MAX; // Char not found
}

size_t TAD_GetEntryRelPath(TADFileEntry* entry, HashTable filenameDB, StringBuilder sb) {
    size_t relPathOffset = SB_Count(sb);
    
    if (SB_Capacity(sb)-SB_Count(sb) < 128)
		SB_Resize(sb, SB_Capacity(sb) + 128);

    bool found = HT_Get(filenameDB, &entry->hash[0], (void *)SB_CStrOffset(sb, relPathOffset));

    if (found) {
		const char* relPath = SB_CStrOffset(sb, relPathOffset);
		size_t len = strlen(relPath);
		SB_Resize(sb, relPathOffset + len); // Resize to fit the string
		SB_AppendChar(sb, '\0'); // Ensure null termination

        size_t dot1 = LastIndexOfChar(relPath, relPath + len, '.');
        size_t dot2 = SIZE_MAX;
        if (dot1 != SIZE_MAX)
            dot2 = LastIndexOfChar(relPath, relPath + dot1 - 1, '.');
        if (dot2 != SIZE_MAX) { // Swap extension
			size_t offset = SB_Count(sb);
			SB_AppendSubstring(sb, relPath, 0, dot2);
			SB_AppendChar(sb, '.');
            SB_AppendSubstring(sb, relPath, dot1 + 1, len-dot1-1);
            SB_AppendChar(sb, '.');
            SB_AppendSubstring(sb, relPath, dot2 + 1, dot1-dot2-1);
			SB_AppendChar(sb, '\0'); // Ensure null termination
			return offset;
        }
    }
    else {
        size_t offset = SB_Count(sb);
		SB_AppendFormat(sb, "/%s/%u", "unknown", entry->hash[0]);
		SB_AppendChar(sb, '\0'); // Ensure null termination
		return offset;
    }
	return relPathOffset;
}

uint64_t TAD_GetFileCount(uint8_t* buffer) {
    TADHeader* header = (TADHeader*)buffer;
	return header->fileCount;
}

void TAD_ExtractFile(TADFileEntry* entry, HashTable filenameDB, StringBuilder sb, FILE* tacFile, const char* outputPath, const char* basename) {
    size_t relPathOffset = TAD_GetEntryRelPath(entry, filenameDB, sb);
    printf("    Path: %s\n", SB_CStrOffset(sb, relPathOffset));

	size_t outputFilenameOffset = SB_Count(sb);
    if (outputPath != NULL && strlen(outputPath) > 0) {
		SB_AppendStr(sb, outputPath);
    }
    if (basename)
        SB_AppendFormat(sb, "/%s", basename);

    SB_AppendStr(sb, SB_CStrOffset(sb, relPathOffset));
	SB_AppendChar(sb, '\0');
	const char* outputFilename = SB_CStrOffset(sb, outputFilenameOffset);

    // Ensure the directory exists
    const char* lastSlash = strrchr(outputFilename, '/');
    if (lastSlash != NULL) {
		size_t dirOffset = SB_Count(sb);
        SB_AppendSubstringFromSB(sb, outputFilenameOffset, lastSlash - outputFilename);
		SB_AppendChar(sb, '\0');
		const char* dir = SB_CStrOffset(sb, dirOffset);
        int success = MakeDirs(dir);
    }
    outputFilename = SB_CStrOffset(sb, outputFilenameOffset);
    TAC_SaveFile(tacFile, entry, outputFilename, sb);
    SB_Clear(sb);
}

void TAD_ContextInit(TAD_Context* ctx, const char* tadFilepath, HashTable filenameDB, const char* outputPath, bool appendBasename) {
	memset(ctx, 0, sizeof(TAD_Context));
    ctx->tadFilepath = tadFilepath;
    ctx->filenameDB = filenameDB;
    ctx->outputPath = outputPath;
    ctx->appendBasename = appendBasename;
}

size_t TAD_ExtractAllFilesStart(TAD_Context* ctx) {
    ctx->sb = SB_Create(512);

    ctx->buffer = File_LoadToMemory(ctx->tadFilepath);
    TADHeader* header = (TADHeader*)ctx->buffer;

    TAD_PrintHeader(header);

    size_t tacFilenameOffet = TAD_GetTACFilename(ctx->tadFilepath, ctx->sb);
    const char* tacFilename = SB_CStrOffset(ctx->sb, tacFilenameOffet);

    // Open the TAC file
    ctx->tacFile = TAC_Open(tacFilename);

	ctx->entryID = 0;

	return header->fileCount; // Return the total number of entries
}

size_t TAD_ExtractAllFilesNext(TAD_Context* ctx) {
    TADHeader* header = (TADHeader*)ctx->buffer;
    if (ctx->entryID >= header->fileCount) {
        return SIZE_MAX; // No more entries to process
	}

    TADFileEntry* entry = (TADFileEntry*)(ctx->buffer + 0x3C + ctx->entryID * 0x20);

    TAD_PrintEntry(entry, ctx->entryID, header->fileCount, ctx->buffer);

    const char* basename = NULL;
    if (ctx->appendBasename) {
        size_t basenameOffset = TAD_GetBasename(ctx->tadFilepath, ctx->sb);
        basename = SB_CStrOffset(ctx->sb, basenameOffset);
    }
    TAD_ExtractFile(entry, ctx->filenameDB, ctx->sb, ctx->tacFile, ctx->outputPath, basename);

	ctx->entryID++;
	return ctx->entryID; // Successfully processed the entry
}

void TAD_ExtractAllFilesEnd(TAD_Context* ctx) {
    SB_Destroy(ctx->sb);
    TAC_Close(ctx->tacFile);
    free(ctx->buffer);
	ctx->buffer = NULL;
    ctx->tacFile = NULL;
	ctx->sb = NULL;
}

void TAD_ExtractAllFiles(const char* tadFilepath, HashTable filenameDB, const char* outputPath, bool appendBasename) {
	TAD_Context ctx;
    TAD_ContextInit(&ctx, tadFilepath, filenameDB, outputPath, appendBasename);

	TAD_ExtractAllFilesStart(&ctx);
    while (TAD_ExtractAllFilesNext(&ctx) != SIZE_MAX) {	}
	TAD_ExtractAllFilesEnd(&ctx);
}

int asctime_safe(char* dest, size_t size, const struct tm* timeptr) {
    if (!dest || !timeptr || size == 0) return -1;

#ifdef _MSC_VER
    return asctime_s(dest, size, timeptr);
#elif defined(__STDC_LIB_EXT1__) && __STDC_LIB_EXT1__ >= 1
    return asctime_s(dest, size, timeptr); // Annex K
#elif defined(__unix__) || defined(__APPLE__) || defined(__MINGW64__)
    char* res = asctime_r(timeptr, dest);
    if (!res) return -1;
    return 0;
#else
    // Insecure fallback: uses asctime (no reentrant)
    const char* temp = asctime(timeptr);
    if (!temp || strlen(temp) + 1 > size) return -1;
    strncpy(dest, temp, size);
    dest[size - 1] = '\0';
    return 0;
#endif
}

int localtime_safe(const time_t* timep, struct tm* result) {
    if (!timep || !result) return -1;

#ifdef _MSC_VER
    return localtime_s(result, timep);  // MSVC uses (out, in)
#elif defined(__STDC_LIB_EXT1__) && __STDC_LIB_EXT1__ >= 1
    return localtime_s(timep, result);  // Annex K uses (in, out)
#elif defined(__unix__) || defined(__APPLE__) || defined(__MINGW64__)
    return (localtime_r(timep, result) == NULL) ? -1 : 0;
#else
    // Insecure fallback
    struct tm* tmp = localtime(timep);
    if (!tmp) return -1;
    *result = *tmp;
    return 0;
#endif
}

void TAD_PrintHeader(TADHeader* header) {
	struct tm localTime;
	localtime_safe(&header->timestamp, &localTime);
	char timeBuffer[32];
	asctime_safe(timeBuffer, sizeof(timeBuffer), &localTime);
    printf("TAD Header:\n");
    printf("  Identifier: %u.%u.%u.%u\n", header->identifier[0], header->identifier[1], header->identifier[2], header->identifier[3]);
    printf("  DateTime: %s", timeBuffer);
    printf("  Graphics API: %s\n", header->graphicsAPI);
    printf("  Checksum: 0x%08X\n", header->checksum);
    printf("  TAC File Size: %llu MB (%llu bytes)\n", header->tacFileSize / (1024 * 1024), header->tacFileSize);
    printf("  File Count: %llu\n", header->fileCount);
}
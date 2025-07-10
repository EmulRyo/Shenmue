#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dir.h"
#include "hashTable.h"
#include "stringBuilder.h"
#include "tac.h"
#include "tad.h"

uint8_t* TAD_Load(const char* filepath) {
    FILE* tadFile = fopen(filepath, "rb");
    if (tadFile == NULL) {
        fputs("[TAD_Load] Open file error", stderr);
        exit(1);
    }

    // obtain file size:
    fseek(tadFile, 0, SEEK_END);
    long size = ftell(tadFile);
    rewind(tadFile);

    // allocate memory to contain the whole file:
    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * size);
    if (buffer == NULL) {
        fputs("[TAD_Load]: Memory allocation error", stderr);
        exit(2);
    }

    // copy the file into the buffer:
    size_t result = fread(buffer, 1, size, tadFile);

    fclose(tadFile);

    return buffer;
}

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

void TAD_ExtractAllFiles(const char* tadFilepath, HashTable filenameDB, const char* outputPath, bool appendBasename) {
	StringBuilder sb = SB_Create(32);

	uint8_t* buffer = TAD_Load(tadFilepath);
    TADHeader* header = (TADHeader*)buffer;
    
    TAD_PrintHeader(header);

    size_t tacFilenameOffet = TAD_GetTACFilename(tadFilepath, sb);
	const char* tacFilename = SB_CStrOffset(sb, tacFilenameOffet);

    // Open the TAC file
	FILE* tacFile = TAC_Open(tacFilename);

    for (int i = 0; i < header->fileCount; i++) {
        TADFileEntry* entry = (TADFileEntry*)(buffer + 0x3C + i * 0x20);
        
        TAD_PrintEntry(entry, i, header->fileCount, buffer);

        size_t relPathOffset = TAD_GetEntryRelPath(entry, filenameDB, sb);
        printf("    Path: %s\n", SB_CStrOffset(sb, relPathOffset));

        char outputFilename[256] = { 0 };
        if (outputPath != NULL && strlen(outputPath) > 0) {
            snprintf(outputFilename, sizeof(outputFilename), "%s", outputPath);
        }
        if (appendBasename) {
            size_t basenameOffset = TAD_GetBasename(tadFilepath, sb);
			const char* basename = SB_CStrOffset(sb, basenameOffset);
            snprintf(outputFilename, sizeof(outputFilename), "%s/%s", outputFilename, basename);
        }
        snprintf(outputFilename, sizeof(outputFilename), "%s%s", outputFilename, SB_CStrOffset(sb, relPathOffset));

        // Ensure the directory exists
        const char* lastSlash = strrchr(outputFilename, '/');
        if (lastSlash != NULL) {
            char dir[256];
            strncpy_s(dir, 256, outputFilename, lastSlash - outputFilename);
            dir[lastSlash - outputFilename] = '\0';
            int success = MakeDirs(dir);
        }
        TAC_SaveFile(tacFile, entry, outputFilename, sb);
		SB_Clear(sb);
    }
    printf("SB capacity: %zu", SB_Capacity(sb));
	SB_Destroy(sb);
	TAC_Close(tacFile);
    free(buffer);
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
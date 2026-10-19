#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "file.h"
#include "idx.h"

typedef struct {
	char		filename[0x0C];
	uint16_t	afsIndex;
	uint16_t	afsLastIndex;
	uint32_t	unknown;
} IDX0_Entry;

typedef struct {
	char		id[4]; // "IDX0"
	uint16_t	count1;
	uint16_t	count;
	uint8_t     padding[12];
	IDX0_Entry	entries[];
} IDX0_Header;

typedef struct {
	char		filename[4];
} IDX_EntryHumans;

typedef struct {
	uint32_t		count;
	IDX_EntryHumans	entries[];
} IDX_HeaderHumans;

static void PrintIDX0Header(IDX0_Header* header) {
	assert(strncmp(header->id, "IDX0", 4) == 0);

	printf("IDX0 Header:\n");
	printf("  ID: %.4s\n", header->id);
	printf("  Count1: %u\n", header->count1);
	printf("  Count: %u\n", header->count);
	for (uint32_t i = 0; i < header->count; i++) {
		IDX0_Entry* entry = &(header->entries[i]);
		printf("  Entry %03u: %.12s, AFS Index: %03u, AFS Last Index: %u, Unknown: %u\n", i, entry->filename, entry->afsIndex, entry->afsLastIndex, entry->unknown);
	}
}

void IDX_ExtractAllFiles(const char* filepath, const char* outputPath) {
	printf("[IDX_ExtractAllFiles]: Extracting file: %s\n", filepath);
	uint8_t* idx = File_LoadToMemory(filepath);
	if (strncmp((char*)idx, "IDX0", 4) == 0)
		PrintIDX0Header((IDX0_Header*)idx);
	else
		fprintf(stderr, "Unknown IDX file format: %.4s\n", (char*)idx);
}
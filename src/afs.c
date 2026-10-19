#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "afs.h"

typedef struct {
	uint32_t	offset;
	uint32_t	size;
} AFS_Entry;

typedef struct {
	char		id[4]; // "AFS\0"
	uint32_t	count;
	AFS_Entry	entries[];
} AFS_Header;

void AFS_ExtractAllFiles(const char* filepath, const char* outputPath) {
	printf("[AFS_ExtractAllFiles]: Extracting file: %s\n", filepath);
	uint8_t* afs = File_LoadToMemory(filepath);
	AFS_Header* header = (AFS_Header*)afs;
	printf("AFS Header:\n");
	printf("  ID: %.4s\n", header->id);
	printf("  File Count: %u\n", header->count);
	for (uint32_t i = 0; i < header->count; i++) {
		AFS_Entry* entry = &(header->entries[i]);
		printf("  Entry %u: offset = 0x%04X, size = 0x%04X\n", i, entry->offset, entry->size);
	}
}
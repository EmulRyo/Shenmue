#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include "file.h"
#include "ipac.h"

#define IPAC_PATH_SIZE 1024
#define IPAC_FILENAME_SIZE 32

typedef struct {
	char		id[4]; // "IPAC"
	uint32_t	dictionaryOffset; // Relative to the start of the IPAC block
	uint32_t	count;
	uint32_t	size;
} IPAC_Header;

typedef struct {
	char		filename[8];
	char		extension[4];
	uint32_t	offset; // Relative to the start of the IPAC block
	uint32_t	size;
} IPAC_Entry;

// Copies a fixed size string, stopping at the first null or space character
static void CopyTrimmed(char* dst, const char* src, size_t srcSize, bool lowercase) {
	size_t i = 0;
	for (; i < srcSize && src[i] != '\0' && src[i] != ' '; i++)
		dst[i] = lowercase ? (char)tolower((unsigned char)src[i]) : src[i];
	dst[i] = '\0';
}

static void GetEntryFilename(char (*filenames)[IPAC_FILENAME_SIZE], uint32_t entryID, const IPAC_Entry* entry, const uint8_t* data, size_t size) {
	char name[9];
	char ext[5];
	CopyTrimmed(name, entry->filename, sizeof(entry->filename), false);
	CopyTrimmed(ext, entry->extension, sizeof(entry->extension), true);

	// The magic is more accurate than the IPAC extension (e.g. textures are stored as "BIN")
	// except for CHRM files, MT5 models whose textures are stored outside the model
	const char* magicExt = File_ExtensionFromMagic(data, size);
	if (magicExt != NULL && !(strcmp(magicExt, "mt5") == 0 && strcmp(ext, "chrm") == 0))
		snprintf(ext, sizeof(ext), "%s", magicExt);
	else if (ext[0] == '\0')
		snprintf(ext, sizeof(ext), "bin");

	char* filename = filenames[entryID];
	if (name[0] != '\0')
		snprintf(filename, IPAC_FILENAME_SIZE, "%s.%s", name, ext);
	else
		snprintf(filename, IPAC_FILENAME_SIZE, "%04u.%s", entryID, ext);

	// Avoid overwriting a previous entry with the same name
	for (uint32_t i = 0; i < entryID; i++) {
		if (strcmp(filenames[i], filename) == 0) {
			snprintf(filename, IPAC_FILENAME_SIZE, "%s_%04u.%s", name[0] != '\0' ? name : "file", entryID, ext);
			break;
		}
	}
}

uint32_t IPAC_ExtractAllFiles(const uint8_t* buffer, size_t size, const char* outputFolder) {
	if (size < sizeof(IPAC_Header) || memcmp(buffer, "IPAC", 4) != 0) {
		fprintf(stderr, "[IPAC_ExtractAllFiles]: Invalid IPAC block\n");
		return 0;
	}

	const IPAC_Header* header = (const IPAC_Header*)buffer;
	printf("IPAC Header:\n");
	printf("  Dictionary Offset: 0x%04X\n", header->dictionaryOffset);
	printf("  File Count: %u\n", header->count);
	printf("  Size: 0x%04X\n", header->size);

	if ((uint64_t)header->dictionaryOffset + (uint64_t)header->count * sizeof(IPAC_Entry) > size) {
		fprintf(stderr, "[IPAC_ExtractAllFiles]: Dictionary out of bounds\n");
		return 0;
	}
	if (header->count == 0)
		return 0;

	char (*filenames)[IPAC_FILENAME_SIZE] = calloc(header->count, IPAC_FILENAME_SIZE);
	if (filenames == NULL) {
		fputs("[IPAC_ExtractAllFiles]: Memory error", stderr);
		return 0;
	}

	const IPAC_Entry* entries = (const IPAC_Entry*)(buffer + header->dictionaryOffset);
	uint32_t extracted = 0;
	for (uint32_t i = 0; i < header->count; i++) {
		const IPAC_Entry* entry = &entries[i];
		if ((uint64_t)entry->offset + entry->size > size) {
			printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X (out of bounds)\n", i, entry->filename, entry->extension, entry->offset, entry->size);
			continue;
		}

		// Decompress before printing the entry, raylib logs the decompression
		const uint8_t* data = buffer + entry->offset;
		size_t dataSize = entry->size;
		uint8_t* decompressed = NULL;
		if (File_IsGzip(data, dataSize)) {
			decompressed = File_GzipDecompress(data, dataSize, &dataSize);
			if (decompressed == NULL) {
				printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X (decompression error)\n", i, entry->filename, entry->extension, entry->offset, entry->size);
				continue;
			}
			data = decompressed;
		}

		printf("  Entry %u: %.8s.%.4s, offset = 0x%04X, size = 0x%04X", i, entry->filename, entry->extension, entry->offset, entry->size);
		if (decompressed != NULL)
			printf(" (gzip, 0x%04zX bytes)", dataSize);
		GetEntryFilename(filenames, i, entry, data, dataSize);
		printf(" -> %s\n", filenames[i]);

		char outputFilename[IPAC_PATH_SIZE];
		snprintf(outputFilename, sizeof(outputFilename), "%s/%s", outputFolder, filenames[i]);
		if (File_Save(outputFilename, data, dataSize))
			extracted++;

		MemFree(decompressed);
	}

	free(filenames);
	return extracted;
}

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "core/dir.h"
#include "core/file.h"
#include "formats/archive/ipac.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"

#define PKF_PATH_SIZE 1024

typedef struct {
	char		id[4]; // "PAKF"
	uint32_t	ipacOffset; // The textures are stored between the header and the IPAC block
	uint32_t	unknown;
	uint32_t	textureCount;
} PKF_Header;

// Generic block header used by the TEXN and DUMY blocks
typedef struct {
	char		id[4];
	uint32_t	size; // Includes the header
} PKF_Block;

typedef struct {
	char		id[4]; // "TEXN"
	uint32_t	size; // Includes the header
	uint8_t		textureID[8];
	uint8_t		data[]; // PVR (GBIX/PVRT) or DDS texture
} PKF_Texture;

static bool SaveTexture(const PKF_Texture* texture, uint32_t textureID, const char* outputFolder) {
	size_t dataSize = texture->size - sizeof(PKF_Texture);
	const char* ext = File_ExtensionFromMagic(texture->data, dataSize);

	char outputFilename[PKF_PATH_SIZE];
	int length = snprintf(outputFilename, sizeof(outputFilename), "%s/tex_%03u_", outputFolder, textureID);
	for (int i = 0; i < 8 && length < (int)sizeof(outputFilename); i++)
		length += snprintf(outputFilename + length, sizeof(outputFilename) - length, "%02X", texture->textureID[i]);
	snprintf(outputFilename + length, sizeof(outputFilename) - length, ".%s", ext != NULL ? ext : "bin");

	printf(" -> %s\n", File_GetFilename(outputFilename));
	return File_Save(outputFilename, texture->data, dataSize);
}

static uint32_t ExtractTextures(const uint8_t* buffer, size_t size, const char* outputFolder) {
	uint32_t textureID = 0;
	uint32_t extracted = 0;
	size_t pos = sizeof(PKF_Header);
	while (pos + sizeof(PKF_Block) <= size) {
		const PKF_Block* block = (const PKF_Block*)(buffer + pos);
		// The texture list ends with an empty block (or with the end of the textures area)
		if (block->size < sizeof(PKF_Block) || pos + block->size > size)
			break;

		if (memcmp(block->id, "TEXN", 4) == 0 && block->size >= sizeof(PKF_Texture)) {
			printf("  Texture %u: offset = 0x%04zX, size = 0x%04X", textureID, pos, block->size);
			if (SaveTexture((const PKF_Texture*)block, textureID, outputFolder))
				extracted++;
			textureID++;
		}
		else
			printf("  Block %.4s: offset = 0x%04zX, size = 0x%04X\n", block->id, pos, block->size);

		pos += block->size;
	}
	return extracted;
}

void PKF_ExtractAllFiles(const char* filepath, const char* outputPath) {
	printf("[PKF_ExtractAllFiles]: Extracting file: %s\n", filepath);
	size_t size = 0;
	uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
	if (buffer == NULL)
		return;

	// Some files have the wrong extension
	if (size >= 4 && memcmp(buffer, "PAKS", 4) == 0) {
		printf("[PKF_ExtractAllFiles]: The file is a PKS file\n");
		free(buffer);
		PKS_ExtractAllFiles(filepath, outputPath);
		return;
	}

	const PKF_Header* header = (const PKF_Header*)buffer;
	if (size < sizeof(PKF_Header) || memcmp(header->id, "PAKF", 4) != 0) {
		fprintf(stderr, "[PKF_ExtractAllFiles]: Invalid PKF file %s\n", filepath);
		free(buffer);
		return;
	}
	printf("PKF Header:\n");
	printf("  ID: %.4s\n", header->id);
	printf("  IPAC Offset: 0x%04X\n", header->ipacOffset);
	printf("  Unknown: 0x%08X\n", header->unknown);
	printf("  Texture Count: %u\n", header->textureCount);

	char outputFolder[PKF_PATH_SIZE];
	File_GetOutputFolder(filepath, outputPath, outputFolder, sizeof(outputFolder));
	MakeDirs(outputFolder);
	printf("  Output folder: %s\n", outputFolder);

	size_t texturesEnd = header->ipacOffset < size ? header->ipacOffset : size;
	uint32_t extracted = ExtractTextures(buffer, texturesEnd, outputFolder);
	if (header->ipacOffset < size)
		extracted += IPAC_ExtractAllFiles(buffer + header->ipacOffset, size - header->ipacOffset, outputFolder);
	printf("[PKF_ExtractAllFiles]: Extracted %u files to %s\n", extracted, outputFolder);

	free(buffer);
}

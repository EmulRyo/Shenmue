#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pvr.h"
#include "spr.h"

bool SPR_Load(const char* filepath, uint16_t* width, uint16_t* height, uint32_t** pixels) {
	FILE* file = fopen(filepath, "rb");
	if (file == NULL) {
		fputs("[SPR_Load] Open file error", stderr);
		exit(1);
	}
	// obtain file size:
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	// allocate memory to contain the whole file:
	uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * size);
	if (buffer == NULL) {
		fputs("[SPR_Load]: Memory allocation error", stderr);
		exit(2);
	}
	// copy the file into the buffer:
	size_t result = fread(buffer, sizeof(uint8_t), size, file);
	fclose(file);

	SPR_Header* sprHeader = (SPR_Header*)buffer;
	printf("SPR Header ID: %.4s, Size: %u, Filename: %.8s\n", sprHeader->id, sprHeader->size, sprHeader->filename);

	bool success = PVR_LoadFromMemory(sprHeader->pvrData, width, height, pixels);

	free(buffer);

	if (success) {
		return true;
	}
	else {
		free(*pixels);
		return false;
	}
}
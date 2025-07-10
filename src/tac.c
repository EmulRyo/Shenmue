#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashTable.h"
#include "stringBuilder.h"
#include "tad.h"
#include "tac.h"

FILE* TAC_Open(const char* filepath) {
    FILE* tacFile = fopen(filepath, "rb");
    if (tacFile == NULL) {
        fputs("File error", stderr);
        exit(1);
    }

	return tacFile;
}

void TAC_Close(FILE* tacFile) {
    if (tacFile != NULL) {
        fclose(tacFile);
    }
}

uint8_t* TAC_GetFile(FILE* tacFile, TADFileEntry* entry) {
    if (entry->size == 0) {
        return NULL;
    }
    fseek(tacFile, entry->offset, SEEK_SET);
    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * entry->size);
    if (buffer == NULL) {
        fputs("[SaveTADFileEntry]: Memory error", stderr);
        exit(2);
    }
    fread(buffer, 1, entry->size, tacFile);

    return buffer;
}

static const char* ExtensionFromMagic(const uint8_t* buffer, size_t size) {
    if (size >= 4) {
        if (buffer[0] == 'D' && buffer[1] == 'X' && buffer[2] == 'B' && buffer[3] == 'C') return "hlsl";
    }
    if (size >= 3) {
        if (buffer[0] == 'D' && buffer[1] == 'D' && buffer[2] == 'S') return "dds";
        if (buffer[0] == 'A' && buffer[1] == 'F' && buffer[2] == 'S') return "afs";
        if (buffer[0] == 'I' && buffer[1] == 'D' && buffer[2] == 'X') return "idx";
    }
    return NULL;
}

bool TAC_SaveFile(FILE* tacFile, TADFileEntry* entry, const char* outputFilename, StringBuilder sb) {
    if (entry->size == 0) {
        return false;
    }

	uint8_t* buffer = TAC_GetFile(tacFile, entry);

    const char* lastDot = strrchr(outputFilename, '.');
    if (lastDot == NULL) {
		const char* ext = ExtensionFromMagic(buffer, entry->size);
        if (ext != NULL) {
			size_t outputOffset = SB_Count(sb);
			SB_AppendFormat(sb, "%s.%s", outputFilename, ext);
			outputFilename = SB_CStrOffset(sb, outputOffset);
        }
    }

    FILE* file = fopen(outputFilename, "wb");
    if (file == NULL) {
		free(buffer);
        return false;
    }

    // Write the file
    fwrite(buffer, 1, entry->size, file);
    fclose(file);

    free(buffer);

	return true;
}

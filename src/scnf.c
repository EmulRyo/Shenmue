#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "file.h"
#include "scnf.h"

// A SCNF file is a header followed by blocks, one per character (or group of lines).
// Each block has a script, a table of lines and the strings: voice names and subtitles in EUC-JP.
typedef struct {
	char		id[4]; // "SCNF"
	uint32_t	size;
	uint32_t	blockCount;
	uint32_t	unknown; // Always 0x900
} SCNF_Header;

// All the offsets are relative to the start of the block
typedef struct {
	char		id[4]; // Character ID (e.g. "AKMI")
	uint32_t	size;
	uint16_t	characterCount;
	uint16_t	positionCount;
	uint32_t	unknown0;
	uint32_t	voicePathOffset; // e.g. "/p38/prj38sc/Msg/voice/F1015"
	uint32_t	scriptOffset;
	uint32_t	linesOffset;     // The table of lines ends where the strings start
	uint32_t	unknown1;
	uint32_t	charactersOffset; // char[4] per character, the first one is "AKIR" (Ryo)
	uint32_t	unknown2;
	uint32_t	positionsOffset; // float[3] per position
	uint32_t	unknown3;
} SCNF_Block;

typedef struct {
	uint32_t	textOffset;  // Speaker name and subtitle: 名前「text」
	uint32_t	voiceOffset; // Voice name (e.g. "F1015B001")
	char		voiceID[4];  // Last 4 characters of the voice name, the letter is the speaker (A = first character)
	float		duration;    // Duration of the voice in seconds, 0 if there is no voice
} SCNF_Line;

#define SCNF_INDENT "                               "

// Returns the null terminated string at offset, or NULL if it is out of bounds
static const char* GetString(const uint8_t* buffer, size_t size, uint64_t offset) {
	if (offset >= size || memchr(buffer + offset, '\0', size - offset) == NULL)
		return NULL;
	return (const char*)(buffer + offset);
}

// Prints an EUC-JP subtitle as UTF-8. "＆" is a line break and "=@" an ellipsis
static void PrintText(const char* text) {
	size_t length = strlen(text);
	char* eucjp = malloc(length * (sizeof(SCNF_INDENT) + 2) + 1);
	if (eucjp == NULL)
		return;

	size_t pos = 0;
	for (size_t i = 0; i < length;) {
		uint8_t c = (uint8_t)text[i];
		if (c == 0xA1 && (uint8_t)text[i + 1] == 0xF5) {
			pos += sprintf(eucjp + pos, "\n%s", SCNF_INDENT);
			i += 2;
		}
		else if (c == '=' && text[i + 1] == '@') {
			pos += sprintf(eucjp + pos, "...");
			i += 2;
		}
		else {
			// Multibyte characters: 3 bytes for JIS X 0212 (0x8F), 2 bytes for the rest
			size_t charSize = c < 0x80 ? 1 : c == 0x8F ? 3 : 2;
			for (size_t j = 0; j < charSize && i < length; j++)
				eucjp[pos++] = text[i++];
		}
	}
	eucjp[pos] = '\0';

#ifdef _WIN32
	// 20932 is the EUC-JP code page of Windows
	int wideLength = MultiByteToWideChar(20932, 0, eucjp, -1, NULL, 0);
	wchar_t* wide = wideLength > 0 ? malloc(wideLength * sizeof(wchar_t)) : NULL;
	char* utf8 = NULL;
	if (wide != NULL && MultiByteToWideChar(20932, 0, eucjp, -1, wide, wideLength) > 0) {
		int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
		utf8 = utf8Length > 0 ? malloc(utf8Length) : NULL;
		if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8Length, NULL, NULL) > 0)
			fputs(utf8, stdout);
		else
			fputs(eucjp, stdout);
	}
	else
		fputs(eucjp, stdout);
	free(utf8);
	free(wide);
#else
	fputs(eucjp, stdout);
#endif
	free(eucjp);
}

static bool PrintBlock(const uint8_t* block, size_t size) {
	const SCNF_Block* header = (const SCNF_Block*)block;
	const char* voicePath = GetString(block, size, header->voicePathOffset);
	if (voicePath == NULL ||
		(uint64_t)header->charactersOffset + header->characterCount * 4ull > size ||
		(uint64_t)header->positionsOffset + header->positionCount * 12ull > size ||
		header->scriptOffset > header->linesOffset || header->linesOffset > size) {
		fprintf(stderr, "[SCNF_Print]: Invalid block header\n");
		return false;
	}

	const char* characters = (const char*)(block + header->charactersOffset);
	printf("Block '%.4s': size = 0x%04X, script = 0x%04X bytes, voices = %s\n", header->id, header->size, header->linesOffset - header->scriptOffset, voicePath);
	printf("  Characters:");
	for (uint16_t i = 0; i < header->characterCount; i++)
		printf(" %c = '%.4s'", 'A' + i, characters + i * 4);
	printf("\n");

	const float* positions = (const float*)(block + header->positionsOffset);
	for (uint16_t i = 0; i < header->positionCount; i++)
		printf("  Position %u: (%g, %g, %g)\n", i, positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);

	// The table has no count: it ends at the first string
	size_t stringsOffset = size;
	uint32_t count = 0;
	for (size_t pos = header->linesOffset; pos + sizeof(SCNF_Line) <= stringsOffset; pos += sizeof(SCNF_Line), count++) {
		const SCNF_Line* line = (const SCNF_Line*)(block + pos);
		const char* voice = GetString(block, size, line->voiceOffset);
		const char* text = GetString(block, size, line->textOffset);
		if (voice == NULL || text == NULL) {
			fprintf(stderr, "[SCNF_Print]: Invalid line at 0x%04zX\n", pos);
			return false;
		}
		if (line->voiceOffset < stringsOffset)
			stringsOffset = line->voiceOffset;
		if (line->textOffset < stringsOffset)
			stringsOffset = line->textOffset;

		int speaker = line->voiceID[0] - 'A';
		const char* speakerID = speaker >= 0 && speaker < header->characterCount ? characters + speaker * 4 : "????";
		printf("  %-12s %c %.4s %5.2f s  ", voice, line->voiceID[0], speakerID, line->duration);
		PrintText(text);
		printf("\n");
	}
	printf("  %u lines\n", count);
	return true;
}

bool SCNF_Print(const char* filepath) {
	printf("[SCNF_Print]: Reading file: %s\n", filepath);
	size_t size = 0;
	uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
	if (buffer == NULL)
		return false;

	const SCNF_Header* header = (const SCNF_Header*)buffer;
	if (size < sizeof(SCNF_Header) || memcmp(header->id, "SCNF", 4) != 0 || header->size > size) {
		fprintf(stderr, "[SCNF_Print]: Invalid SCNF file %s\n", filepath);
		free(buffer);
		return false;
	}

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	printf("SCNF Header:\n");
	printf("  Size: 0x%04X\n", header->size);
	printf("  Block Count: %u\n", header->blockCount);

	size_t pos = sizeof(SCNF_Header);
	bool valid = true;
	for (uint32_t i = 0; valid && i < header->blockCount; i++) {
		const SCNF_Block* block = (const SCNF_Block*)(buffer + pos);
		if (pos + sizeof(SCNF_Block) > header->size || block->size < sizeof(SCNF_Block) || pos + block->size > header->size) {
			fprintf(stderr, "[SCNF_Print]: Block %u out of bounds\n", i);
			valid = false;
			break;
		}
		valid = PrintBlock(buffer + pos, block->size);
		pos += block->size;
	}

	free(buffer);
	return valid;
}

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

// Columns before the subtitle in the table of lines, continuation lines are indented to them
#define SCNF_LINES_INDENT 31

// The script is a list of instructions. The highest 4 bits of the first byte are the opcode and the
// rest of the instruction the argument (big-endian): 1 byte for 0x8X and 0xFX, 3 bytes for 0xDX,
// 4 bytes for 0xF2 and 0xF5 (24 bits) and 2 bytes for the rest (12 bits).
// Jumps are relative to the end of the instruction.
// Conditions are expressions in postfix notation between 0xF0 and 0x80, followed by a jump if false.
typedef struct {
	uint8_t		op;   // First byte
	uint32_t	arg;
	uint32_t	size;
} SCNF_Instruction;

#define SCNF_EXPR_STACK 16
#define SCNF_EXPR_SIZE  256
#define SCNF_SCRIPT_INDENT 4

// Returns the null terminated string at offset, or NULL if it is out of bounds
static const char* GetString(const uint8_t* buffer, size_t size, uint64_t offset) {
	if (offset >= size || memchr(buffer + offset, '\0', size - offset) == NULL)
		return NULL;
	return (const char*)(buffer + offset);
}

// Prints an EUC-JP subtitle as UTF-8, indenting the continuation lines to the given column.
// "＆" is a line break and "=@" an ellipsis
static void PrintText(const char* text, int indent) {
	size_t length = strlen(text);
	char* eucjp = malloc(length * (indent + 2) + 1);
	if (eucjp == NULL)
		return;

	size_t pos = 0;
	for (size_t i = 0; i < length;) {
		uint8_t c = (uint8_t)text[i];
		if (c == 0xA1 && (uint8_t)text[i + 1] == 0xF5) {
			pos += sprintf(eucjp + pos, "\n%*s", indent, "");
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

// Reads the instruction at pos. Returns false if it doesn't fit in the script
static bool ReadInstruction(const uint8_t* script, size_t size, size_t pos, SCNF_Instruction* ins) {
	if (pos >= size)
		return false;
	uint8_t op = script[pos];
	uint8_t opcode = op >> 4;
	ins->op = op;
	ins->size = (opcode == 0x8 || opcode == 0xF) ? 1 : opcode == 0xD ? 3 : 2;
	if (op == 0xF2 || op == 0xF5)
		ins->size = 4;
	if (pos + ins->size > size)
		return false;

	const uint8_t* p = script + pos;
	switch (ins->size) {
	case 1:  ins->arg = op & 0xF; break;
	case 2:  ins->arg = ((op & 0xF) << 8) | p[1]; break;
	case 3:  ins->arg = ((op & 0xF) << 16) | (p[1] << 8) | p[2]; break;
	default: ins->arg = (p[1] << 16) | (p[2] << 8) | p[3]; break;
	}
	return true;
}

// Writes the operand of an expression. Returns false if the instruction isn't an operand
static bool FormatOperand(const SCNF_Instruction* ins, char* out, size_t outSize) {
	static const char* dateNames[] = { "year", "month", "day", "date3", "hour", "minute" };
	switch (ins->op >> 4) {
	case 0x0: snprintf(out, outSize, "flag[0x%03X]", ins->arg); break;
	case 0x4: snprintf(out, outSize, "var4[0x%03X]", ins->arg); break;
	case 0x5:
	case 0x6: snprintf(out, outSize, "%d", (int)ins->arg - (ins->op >> 4 == 0x5 ? 0x1000 : 0)); break; // 0x5FFF = -1
	case 0xA:
		if (ins->arg < sizeof(dateNames) / sizeof(dateNames[0]))
			snprintf(out, outSize, "%s", dateNames[ins->arg]);
		else
			snprintf(out, outSize, "date[0x%03X]", ins->arg);
		break;
	case 0xC: snprintf(out, outSize, "var[0x%03X]", ins->arg); break;
	case 0xE: snprintf(out, outSize, "varE[0x%03X]", ins->arg); break;
	default:  return false;
	}
	return true;
}

// Converts the postfix expression that starts after the 0xF0 at pos to infix.
// Returns the position of the 0x80 that ends it, or 0 if it can't be decoded
static size_t FormatExpression(const uint8_t* script, size_t size, size_t pos, char* out, size_t outSize) {
	static const char* binary[16] = { [5] = "&&", [6] = "||", [7] = "==", [8] = "!=", [9] = ">", [0xA] = "<", [0xC] = ">=" };
	char stack[SCNF_EXPR_STACK][SCNF_EXPR_SIZE];
	int count = 0;
	SCNF_Instruction ins;
	while (ReadInstruction(script, size, pos, &ins)) {
		if (ins.op == 0x80) {
			if (count != 1)
				return 0;
			snprintf(out, outSize, "%s", stack[0]);
			return pos;
		}

		char operand[SCNF_EXPR_SIZE];
		if ((ins.op >> 4) == 0x8) {
			uint8_t op = ins.op & 0xF;
			if (op == 0x4 || op == 0x2) { // 0x84 = not, 0x82 = unknown
				if (count < 1)
					return 0;
				snprintf(operand, sizeof(operand), op == 0x4 ? "!%s" : "op2(%s)", stack[count - 1]);
				snprintf(stack[count - 1], SCNF_EXPR_SIZE, "%s", operand);
			}
			else if (binary[op] != NULL) {
				if (count < 2)
					return 0;
				snprintf(operand, sizeof(operand), "(%s %s %s)", stack[count - 2], binary[op], stack[count - 1]);
				count--;
				snprintf(stack[count - 1], SCNF_EXPR_SIZE, "%s", operand);
			}
			else
				return 0;
		}
		else {
			if (count == SCNF_EXPR_STACK || !FormatOperand(&ins, stack[count], SCNF_EXPR_SIZE))
				return 0;
			count++;
		}
		pos += ins.size;
	}
	return 0;
}

static void PrintScriptLine(const uint8_t* block, size_t size, const uint32_t* lines, uint32_t lineCount, uint32_t index) {
	if (index >= lineCount) {
		printf("LINE %u (out of range)\n", index);
		return;
	}
	const SCNF_Line* line = (const SCNF_Line*)(block + lines[index]);
	const char* voice = GetString(block, size, line->voiceOffset);
	const char* text = GetString(block, size, line->textOffset);
	int column = printf("LINE %-4u %-12s ", index, voice != NULL ? voice : "?");
	if (text != NULL)
		PrintText(text, SCNF_SCRIPT_INDENT + 6 + column);
	printf("\n");
}

// Prints the script one instruction per line: offset, bytes and decoded instruction
static void PrintScript(const uint8_t* block, size_t size, const SCNF_Block* header, const uint32_t* lines, uint32_t lineCount) {
	const uint8_t* script = block + header->scriptOffset;
	size_t scriptSize = header->linesOffset - header->scriptOffset;
	printf("  Script:\n");

	SCNF_Instruction ins;
	size_t pos = 0;
	while (ReadInstruction(script, scriptSize, pos, &ins)) {
		size_t next = pos + ins.size;
		size_t target = next + ins.arg;
		printf("%*s%04zX  ", SCNF_SCRIPT_INDENT, "", pos);

		if (ins.op == 0xF0) {
			char expression[SCNF_EXPR_SIZE];
			size_t end = FormatExpression(script, scriptSize, next, expression, sizeof(expression));
			SCNF_Instruction jump;
			if (end != 0 && ReadInstruction(script, scriptSize, end + 1, &jump) && (jump.op >> 4) == 0x4) {
				printf("IF %s ELSE -> %04zX\n", expression, end + 1 + jump.size + jump.arg);
				pos = end + 1 + jump.size;
				continue;
			}
			printf("EXPR\n"); // Not a condition, print the instructions one by one
			pos = next;
			continue;
		}

		switch (ins.op >> 4) {
		case 0x0: printf("OPTION 0x%X bytes\n", ins.arg); break; // Size of an alternative of RANDOM
		case 0x1: printf("BLOCK -> %04zX\n", target); break;     // Subroutine, ends at the target
		case 0x2: PrintScriptLine(block, size, lines, lineCount, ins.arg); break;
		case 0x3: printf("PROC 0x%03X\n", ins.arg); break;
		case 0x4: printf("IF NOT -> %04zX\n", target); break;
		case 0x5: printf("JUMP -> %04zX\n", target); break;
		case 0x6: printf("SET flag[0x%03X]\n", ins.arg); break;
		case 0x9: printf("SKIP -> %04zX\n", target); break;
		case 0xC: printf("ACTION 0x%03X\n", ins.arg); break;
		case 0xD: printf("CALL -> %04zX\n", target); break;
		case 0xE: printf("RANDOM %u\n", ins.arg); break;
		case 0x8: printf("OP 0x%02X\n", ins.op); break;
		case 0xF:
			if (ins.op == 0xF2 || ins.op == 0xF5)
				printf("F%X -> %04zX\n", ins.op & 0xF, target);
			else if (ins.op == 0xFF)
				printf("END\n");
			else
				printf("F%X\n", ins.op & 0xF);
			break;
		default:  printf("OP%X 0x%03X\n", ins.op >> 4, ins.arg); break;
		}
		pos = next;
	}
	if (pos < scriptSize)
		printf("%*s%04zX  Truncated instruction\n", SCNF_SCRIPT_INDENT, "", pos);
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
	uint32_t* lines = malloc((size - header->linesOffset) / sizeof(SCNF_Line) * sizeof(uint32_t) + sizeof(uint32_t));
	if (lines == NULL) {
		fputs("[SCNF_Print]: Memory error", stderr);
		return false;
	}
	size_t stringsOffset = size;
	uint32_t count = 0;
	for (size_t pos = header->linesOffset; pos + sizeof(SCNF_Line) <= stringsOffset; pos += sizeof(SCNF_Line), count++) {
		const SCNF_Line* line = (const SCNF_Line*)(block + pos);
		const char* voice = GetString(block, size, line->voiceOffset);
		const char* text = GetString(block, size, line->textOffset);
		if (voice == NULL || text == NULL) {
			fprintf(stderr, "[SCNF_Print]: Invalid line at 0x%04zX\n", pos);
			free(lines);
			return false;
		}
		if (line->voiceOffset < stringsOffset)
			stringsOffset = line->voiceOffset;
		if (line->textOffset < stringsOffset)
			stringsOffset = line->textOffset;
		lines[count] = (uint32_t)pos;

		int speaker = line->voiceID[0] - 'A';
		const char* speakerID = speaker >= 0 && speaker < header->characterCount ? characters + speaker * 4 : "????";
		printf("  %-12s %c %.4s %5.2f s  ", voice, line->voiceID[0], speakerID, line->duration);
		PrintText(text, SCNF_LINES_INDENT);
		printf("\n");
	}
	printf("  %u lines\n", count);

	PrintScript(block, size, header, lines, count);
	free(lines);
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

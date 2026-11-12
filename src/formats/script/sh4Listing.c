#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/script/sh4Listing.h"

#define MAX_STRING 48   // Characters of a string shown in a comment

const char* SH4_DataString(const SH4_Program* program, uint32_t offset) {
    uint32_t size = program->dataEnd - program->dataStart;
    if (offset >= size)
        return NULL;
    const uint8_t* text = program->code.data + program->dataStart + offset;
    if (offset > 0 && text[-1] != '\0')
        return NULL;
    uint32_t length = 0;
    while (offset + length < size && text[length] != '\0') {
        uint8_t c = text[length];
        if (c < 0x20 && c != '\n' && c != '\t' && c != '\r')
            return NULL;
        length++;
    }
    return length > 0 && offset + length < size ? (const char*)text : NULL;
}

// The string between quotes, with the control and non-ASCII characters escaped, cut after MAX_STRING characters
void SH4_QuoteString(const char* string, char* text, size_t size) {
    size_t length = 0;
    if (size < 8)
        return;
    text[length++] = '"';
    for (int i = 0; string[i] != '\0' && length + 6 < size; i++) {
        if (i == MAX_STRING) {
            length += (size_t)snprintf(text + length, size - length, "...");
            break;
        }
        uint8_t c = (uint8_t)string[i];
        if (c == '\n')
            length += (size_t)snprintf(text + length, size - length, "\\n");
        else if (c == '"' || c == '\\')
            length += (size_t)snprintf(text + length, size - length, "\\%c", c);
        else if (c < 0x20 || c >= 0x7F)
            length += (size_t)snprintf(text + length, size - length, "\\x%02X", c);
        else
            text[length++] = (char)c;
    }
    if (length + 2 <= size)
        text[length++] = '"';
    text[length < size ? length : size - 1] = '\0';
}

// Four characters of an ID (letters, digits, underscores), as stored in memory, e.g. 'AKIR'
static bool IsFourCC(uint32_t value) {
    for (int i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)(value >> (i * 8));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z')))
            return false;
    }
    return true;
}

// A float that is probably a float: a normal number from about 1e-6 to 1e7 with its low mantissa bits zero
static bool LooksLikeFloat(uint32_t value) {
    uint32_t exponent = (value >> 23) & 0xFF;
    return exponent >= 107 && exponent <= 150 && (value & 0xFF) == 0;
}

void SH4_DescribeNumber(uint32_t value, char* text, size_t size) {
    int32_t number = (int32_t)value;
    if (IsFourCC(value)) {
        snprintf(text, size, "'%c%c%c%c'", (char)value, (char)(value >> 8), (char)(value >> 16), (char)(value >> 24));
    } else if (number > -10000 && number < 10000) {
        snprintf(text, size, "%d", number);
    } else if (LooksLikeFloat(value)) {
        float f;
        memcpy(&f, &value, sizeof(f));
        snprintf(text, size, "0x%X (%g)", value, f);
    } else {
        snprintf(text, size, "0x%X", value);
    }
}

static void DescribeData(const SH4_Program* program, uint32_t offset, char* text, size_t size) {
    const char* string = SH4_DataString(program, offset);
    if (string != NULL)
        SH4_QuoteString(string, text, size);
    else
        snprintf(text, size, "data+0x%X", offset);
}

void SH4_DescribeValue(const SH4_Program* program, const SH4_Value* value, char* text, size_t size) {
    switch (value->kind) {
    case SH4_VALUE_CONSTANT:
        SH4_DescribeNumber(value->value, text, size);
        break;
    case SH4_VALUE_DATA:
        DescribeData(program, value->value, text, size);
        break;
    case SH4_VALUE_FIELD:
        snprintf(text, size, "ctx[0x%X]", value->value);
        break;
    case SH4_VALUE_CONTEXT:
        snprintf(text, size, "ctx");
        break;
    case SH4_VALUE_VARIABLE:
        snprintf(text, size, "&var_%X", value->value);
        break;
    case SH4_VALUE_PARAMETER:
        snprintf(text, size, "arg%u", value->value);
        break;
    case SH4_VALUE_STACK:
        snprintf(text, size, value->value ? "r13%+d" : "r13", (int32_t)value->value);
        break;
    default:
        snprintf(text, size, "?");
        break;
    }
}

const SH4_Call* SH4_CallAt(const SH4_Program* program, uint32_t address) {
    uint32_t low = 0, high = program->callCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (program->calls[middle].address < address)
            low = middle + 1;
        else
            high = middle;
    }
    return low < program->callCount && program->calls[low].address == address ? &program->calls[low] : NULL;
}

static SH4_GameNamer s_gameNamer;

void SH4_SetGameNamer(SH4_GameNamer namer) {
    s_gameNamer = namer;
}

const char* SH4_GameName(int id) {
    return s_gameNamer != NULL ? s_gameNamer(id) : NULL;
}

void SH4_DescribeCall(const SH4_Program* program, const SH4_Call* call, char* text, size_t size) {
    size_t length;
    const char* name;
    switch (call->kind) {
    case SH4_CALL_LOCAL:
        length = (size_t)snprintf(text, size, "call 0x%X", call->target);
        break;
    case SH4_CALL_GAME:
        name = s_gameNamer != NULL ? s_gameNamer(call->id) : NULL;
        length = (size_t)snprintf(text, size, name != NULL ? "game 0x%X %s" : "game 0x%X", (unsigned)call->id, name);
        break;
    case SH4_CALL_YIELD:
        snprintf(text, size, "yield");
        return;
    case SH4_CALL_UNKNOWN:
        snprintf(text, size, "call ?");
        return;
    default:
        length = (size_t)snprintf(text, size, call->id >= 0 ? "ctx[0x%X] 0x%X" : "ctx[0x%X]", call->context,
            (unsigned)call->id);
        break;
    }
    if (call->argumentBytes < 0 || length >= size)
        return;
    length += (size_t)snprintf(text + length, size - length, "(");
    for (uint32_t i = 0; i < call->argumentCount && length < size; i++) {
        char value[96];
        SH4_DescribeValue(program, &call->arguments[i], value, sizeof(value));
        length += (size_t)snprintf(text + length, size - length, i > 0 ? ", %s" : "%s", value);
    }
    if (length < size)
        snprintf(text + length, size - length, (uint32_t)call->argumentBytes / 4 > call->argumentCount ? ", ...)" : ")");
}

// Comment of a literal: what its value is used for
static void DescribeLiteral(const SH4_Program* program, const SH4_Literal* literal, char* text, size_t size) {
    switch (literal->kind) {
    case SH4_LITERAL_CODE_OFFSET:
        snprintf(text, size, "offset 0x%X", literal->value);
        break;
    case SH4_LITERAL_DATA_OFFSET:
        DescribeData(program, literal->value, text, size);
        break;
    case SH4_LITERAL_CALL_ID:
        snprintf(text, size, "ID 0x%X", literal->value);
        break;
    default:
        SH4_DescribeNumber(literal->size == 2 ? (uint32_t)(int32_t)(int16_t)literal->value : literal->value, text, size);
        break;
    }
}

uint32_t SH4_MaxFunctionLines(const SH4_Program* program, uint32_t function) {
    if (function >= program->functionCount)
        return 0;
    return (program->functions[function].end - program->functions[function].start) / 2 + 1;
}

static void InstructionLine(const SH4_Program* program, uint32_t pc, SH4_Line* line) {
    SH4_Instruction ins;
    SH4_Decode(Bytes_ReadU16(program->code.data + pc), pc, 0, &ins);
    uint8_t kind = SH4_WordKind(&program->code, pc);
    line->kind = SH4_LINE_INSTRUCTION;
    line->size = 2;
    line->slot = (kind & SH4_WORD_SLOT) != 0;
    SH4_Format(&ins, line->text, sizeof(line->text));
    if (ins.flags & SH4_LITERAL) {
        const SH4_Literal* literal = SH4_LiteralAt(program, ins.target);
        if (literal != NULL)
            DescribeLiteral(program, literal, line->comment, sizeof(line->comment));
        line->jump = ins.target;
    } else if (ins.flags & SH4_ADDRESS) {
        snprintf(line->comment, sizeof(line->comment), "-> 0x%X", ins.target);
        line->jump = ins.target;
    } else if (ins.flags & SH4_BRANCH) {
        const SH4_Call* call = (ins.flags & SH4_CALL) ? SH4_CallAt(program, pc) : NULL;
        uint32_t target = 0;
        if (call != NULL)
            SH4_DescribeCall(program, call, line->comment, sizeof(line->comment));
        if (call != NULL && call->kind == SH4_CALL_LOCAL)
            line->jump = call->target;
        else if ((ins.opcode & 0xF0DF) == 0x0003 && SH4_RelativeTarget(&program->code, &ins, &target)) {
            line->jump = target;
            if (call == NULL)
                snprintf(line->comment, sizeof(line->comment), "-> 0x%X", target);
        } else if (!(ins.flags & (SH4_INDIRECT | SH4_RETURN)))
            line->jump = ins.target;
    }
}

uint32_t SH4_FunctionLines(const SH4_Program* program, uint32_t function, SH4_Line* lines) {
    if (function >= program->functionCount)
        return 0;
    const SH4_Function* f = &program->functions[function];
    uint32_t count = 0;
    SH4_Line* header = &lines[count++];
    memset(header, 0, sizeof(*header));
    header->kind = SH4_LINE_FUNCTION;
    header->address = f->start;
    snprintf(header->text, sizeof(header->text), "function 0x%X", f->start);
    snprintf(header->comment, sizeof(header->comment), "%u blocks, %u local callers", f->blockCount, f->callers);
    for (uint32_t pc = f->start; pc < f->end;) {
        SH4_Line* line = &lines[count++];
        memset(line, 0, sizeof(*line));
        line->address = pc;
        uint8_t kind = SH4_WordKind(&program->code, pc);
        line->label = (kind & SH4_WORD_TARGET) && !(kind & SH4_WORD_FUNCTION);
        const SH4_Literal* literal = (kind & SH4_WORD_LITERAL) ? SH4_LiteralAt(program, pc) : NULL;
        if (kind & SH4_WORD_CODE) {
            InstructionLine(program, pc, line);
        } else if (literal != NULL && pc + literal->size <= f->end) {
            line->kind = SH4_LINE_LITERAL;
            line->size = literal->size;
            snprintf(line->text, sizeof(line->text), literal->size == 4 ? ".long 0x%08X" : ".word 0x%04X", literal->value);
            DescribeLiteral(program, literal, line->comment, sizeof(line->comment));
        } else {
            // Not reached, or the second half of a literal read from its first half
            line->kind = (kind & SH4_WORD_LITERAL) ? SH4_LINE_LITERAL : SH4_LINE_UNKNOWN;
            line->size = 2;
            snprintf(line->text, sizeof(line->text), ".word 0x%04X", Bytes_ReadU16(program->code.data + pc));
        }
        pc += line->size;
    }
    return count;
}

int SH4_LineAt(const SH4_Line* lines, uint32_t count, uint32_t address) {
    for (uint32_t i = 0; i < count; i++) {
        if (lines[i].kind != SH4_LINE_FUNCTION && address >= lines[i].address && address < lines[i].address + lines[i].size)
            return (int)i;
    }
    return -1;
}

void SH4_WriteListing(const SH4_Program* program, FILE* file) {
    uint32_t max = 0;
    for (uint32_t f = 0; f < program->functionCount; f++) {
        uint32_t lines = SH4_MaxFunctionLines(program, f);
        max = lines > max ? lines : max;
    }
    SH4_Line* lines = malloc((max + 1) * sizeof(SH4_Line));
    if (lines == NULL)
        return;
    for (uint32_t f = 0; f < program->functionCount; f++) {
        uint32_t count = SH4_FunctionLines(program, f, lines);
        for (uint32_t i = 0; i < count; i++) {
            const SH4_Line* line = &lines[i];
            if (line->kind == SH4_LINE_FUNCTION) {
                fprintf(file, "\n; %s: %s\n", line->text, line->comment);
                continue;
            }
            char code[16] = "";
            if (line->kind == SH4_LINE_INSTRUCTION)
                snprintf(code, sizeof(code), "%04X", Bytes_ReadU16(program->code.data + line->address));
            fprintf(file, "%08X  %-4s %c %-28s%s%s\n", line->address, code, line->label ? '>' : ' ', line->text,
                line->comment[0] != '\0' ? "  ; " : "", line->comment);
        }
    }
    free(lines);
}

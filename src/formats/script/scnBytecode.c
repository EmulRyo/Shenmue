#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/script/scnBytecode.h"

#define OPERATOR_COUNT  0x27    // Operators 0x80 to 0xA6
#define MAX_STACK       256     // Values followed by the analysis
#define PADDING         0xFF    // Bytes after the instructions, up to dataStart

#define START_INSTRUCTION   1
#define START_TARGET        2

static const char* const s_operators[OPERATOR_COUNT] = {
    "pop result", "push result", "load.b", "load.w", "load.l", "store.b", "store.w", "store.l",
    "==", "!=", "<", "<=", ">", ">=", "~", "&", "|", "^", "+", "-", "*", "/", "%", "<<", ">>",
    "nop", "nop", "nop", "nop", "itof", "ftoi", "f<", "f<=", "f>", "f>=", "f+", "f-", "f*", "f/",
};

// Values popped and pushed by the operators: binary ones pop 2 and push 1, the unary ones (loads, ~, conversions) pop 1
// and push 1
static int OperatorPops(uint8_t byte) {
    switch (byte) {
    case 0x80: return 1;
    case 0x81: case 0x99: case 0x9A: case 0x9B: case 0x9C: return 0;
    case 0x82: case 0x83: case 0x84: case 0x8E: case 0x9D: case 0x9E: return 1;
    default: return 2;
    }
}

static int OperatorPushes(uint8_t byte) {
    return byte == 0x80 || (byte >= 0x99 && byte <= 0x9C) ? 0 : 1;
}

const char* BC_OperatorName(uint8_t byte) {
    return byte >= 0x80 && byte < 0x80 + OPERATOR_COUNT ? s_operators[byte - 0x80] : NULL;
}

bool BC_Decode(const uint8_t* data, uint32_t size, uint32_t pc, BC_Instruction* ins) {
    memset(ins, 0, sizeof(*ins));
    ins->address = pc;
    ins->kind = BC_INVALID;
    if (pc >= size)
        return false;
    uint8_t byte = data[pc];
    ins->byte = byte;
    ins->size = 1;
    uint8_t group = byte >> 6;
    if (group == 3)
        return false;
    if (group == 2) {
        if ((byte & 0x3F) >= OPERATOR_COUNT)
            return false;
        ins->kind = BC_OPERATOR;
        return true;
    }
    static const uint8_t operandSizes[4] = { 0, 1, 2, 4 };
    uint8_t operandSize = operandSizes[(byte >> 4) & 3];
    if (pc + 1 + operandSize > size)
        return false;
    ins->size = (uint8_t)(1 + operandSize);
    const uint8_t* operand = data + pc + 1;
    ins->operand = operandSize == 1 ? (int8_t)operand[0] : operandSize == 2 ? (int16_t)Bytes_ReadU16(operand) :
        operandSize == 4 ? (int32_t)Bytes_ReadU32(operand) : 0;
    uint8_t op = byte & 0xF;
    if (group == 1) {
        ins->kind = BC_PUSH;
        if (op == 0) {
            ins->area = BC_AREA_CONSTANT;
        } else if (op <= 3) {
            ins->area = BC_AREA_PROGRAM;
            ins->target = (uint32_t)ins->operand;
        } else {
            static const uint8_t widths[4] = { 1, 2, 4, 0 };
            ins->area = op < 8 ? BC_AREA_GLOBAL : op < 12 ? BC_AREA_DATA : BC_AREA_LOCAL;
            ins->width = widths[op & 3];
        }
        return true;
    }
    static const uint8_t kinds[16] = {
        BC_RET, BC_CALL, BC_CALL_POP, BC_STACK, BC_JUMP, BC_BRANCH, BC_BRANCH_ZERO, BC_ARGUMENT_COUNT, BC_DATA_SERVICE,
        BC_NATIVE, BC_NATIVE4, BC_GAME, BC_SERVICE2C, BC_SERVICE30, BC_INVALID, BC_INVALID };
    ins->kind = kinds[op];
    if (ins->kind == BC_CALL || ins->kind == BC_JUMP || ins->kind == BC_DATA_SERVICE)
        ins->target = (uint32_t)ins->operand;
    else if (ins->kind == BC_BRANCH || ins->kind == BC_BRANCH_ZERO)
        ins->target = pc + ins->size + (uint32_t)ins->operand;
    return ins->kind != BC_INVALID;
}

static int FormatVariable(const BC_Instruction* ins, char* text, size_t size) {
    static const char* const areas[] = { "", "", "global", "data", "fp" };
    static const char suffixes[5] = { 0, 'b', 'w', 0, 'l' };
    char where[32];
    if (ins->area == BC_AREA_LOCAL)
        snprintf(where, sizeof(where), ins->operand != 0 ? "fp%+d" : "fp", ins->operand);
    else
        snprintf(where, sizeof(where), "%s+0x%X", areas[ins->area], (unsigned)ins->operand);
    if (ins->width == 0)
        return snprintf(text, size, "push &%s", where);
    return snprintf(text, size, "push.%c [%s]", suffixes[ins->width], where);
}

int BC_Format(const BC_Instruction* ins, char* text, size_t size) {
    switch (ins->kind) {
    case BC_RET:            return snprintf(text, size, "ret");
    case BC_CALL:           return snprintf(text, size, "call 0x%X", ins->target);
    case BC_CALL_POP:       return snprintf(text, size, "call pop");
    case BC_STACK:          return snprintf(text, size, ins->operand >= 0 ? "sp += %d" : "sp -= %d",
                                ins->operand >= 0 ? ins->operand : -ins->operand);
    case BC_JUMP:           return snprintf(text, size, "jmp 0x%X", ins->target);
    case BC_BRANCH:         return snprintf(text, size, "bra 0x%X", ins->target);
    case BC_BRANCH_ZERO:    return snprintf(text, size, "bz 0x%X", ins->target);
    case BC_ARGUMENT_COUNT: return snprintf(text, size, "args %d", ins->operand);
    case BC_DATA_SERVICE:   return snprintf(text, size, "service 1 &0x%X", ins->target);
    case BC_NATIVE:         return snprintf(text, size, "native %d", ins->operand);
    case BC_NATIVE4:        return snprintf(text, size, "native4 %d", ins->operand);
    case BC_GAME:           return snprintf(text, size, "game 0x%X", (unsigned)ins->operand);
    case BC_SERVICE2C:      return snprintf(text, size, "service 0x%X", (unsigned)ins->operand);
    case BC_SERVICE30:      return snprintf(text, size, "service30 0x%X", (unsigned)ins->operand);
    case BC_OPERATOR:       return snprintf(text, size, "%s", BC_OperatorName(ins->byte));
    case BC_PUSH:
        if (ins->area == BC_AREA_CONSTANT)
            return snprintf(text, size, ins->operand > -10000 && ins->operand < 10000 ? "push %d" : "push 0x%X",
                ins->operand);
        if (ins->area == BC_AREA_PROGRAM)
            return snprintf(text, size, "push &0x%X", ins->target);
        return FormatVariable(ins, text, size);
    default:
        return snprintf(text, size, ".byte 0x%02X", ins->byte);
    }
}

const char* BC_DataString(const BC_Program* program, uint32_t offset) {
    const SCN_Header* header = &program->scn->header;
    uint32_t size = header->end - header->dataStart;
    if (offset >= size)
        return NULL;
    const uint8_t* text = program->scn->data + header->dataStart + offset;
    if (offset > 0 && text[-1] != '\0')
        return NULL;
    uint32_t length = 0;
    while (offset + length < size && text[length] != '\0') {
        if (text[length] < 0x20 && text[length] != '\n' && text[length] != '\t' && text[length] != '\r')
            return NULL;
        length++;
    }
    return length > 0 && offset + length < size ? (const char*)text : NULL;
}

static bool Decode(const BC_Program* program, uint32_t pc, BC_Instruction* ins) {
    return BC_Decode(program->scn->data, program->codeEnd, pc, ins);
}

static bool IsStart(const BC_Program* program, uint32_t address) {
    return address >= program->scn->header.codeStart && address < program->codeEnd &&
        (program->starts[address] & START_INSTRUCTION);
}

static int CompareAddresses(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

// The function starts, without duplicates
static bool FindFunctions(BC_Program* program) {
    const SCN_Program* scn = program->scn;
    uint32_t capacity = 16 + scn->relocationCount;
    for (uint32_t pc = scn->header.codeStart; pc < program->codeEnd; pc++)
        capacity += (program->starts[pc] & START_INSTRUCTION) != 0;
    program->functions = malloc(capacity * sizeof(uint32_t));
    if (program->functions == NULL)
        return false;
    uint32_t count = 0;
    program->functions[count++] = scn->header.codeStart;
    if (IsStart(program, scn->header.entry))
        program->functions[count++] = scn->header.entry;
    for (uint32_t pc = scn->header.codeStart; pc < program->codeEnd; pc++) {
        BC_Instruction ins;
        if (!(program->starts[pc] & START_INSTRUCTION) || !Decode(program, pc, &ins))
            continue;
        if ((ins.kind == BC_CALL || (ins.kind == BC_PUSH && ins.area == BC_AREA_PROGRAM)) && IsStart(program, ins.target))
            program->functions[count++] = ins.target;
        // A function ends with a return: the next instruction starts another one, if no branch goes to it
        uint32_t next = pc + ins.size;
        if (ins.kind == BC_RET && IsStart(program, next) && !(program->starts[next] & START_TARGET))
            program->functions[count++] = next;
    }
    for (uint32_t r = 0; r < scn->relocationCount; r++) {
        SCN_Relocation relocation = SCN_GetRelocation(scn, r);
        if (relocation.type == SCN_RELOCATION_CODE && relocation.offset + 4 <= scn->header.size) {
            uint32_t value = Bytes_ReadU32(scn->data + relocation.offset);
            if (IsStart(program, value))
                program->functions[count++] = value;
        }
    }
    qsort(program->functions, count, sizeof(uint32_t), CompareAddresses);
    uint32_t unique = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (unique == 0 || program->functions[i] != program->functions[unique - 1])
            program->functions[unique++] = program->functions[i];
    }
    program->functionCount = unique;
    return true;
}

typedef struct {
    SH4_Value   values[MAX_STACK];
    uint32_t    depth;
} Stack;

static void Push(Stack* stack, SH4_Value value) {
    if (stack->depth == MAX_STACK) {
        memmove(stack->values, stack->values + 1, (MAX_STACK - 1) * sizeof(SH4_Value));
        stack->depth--;
    }
    stack->values[stack->depth++] = value;
}

static void Pop(Stack* stack, uint32_t count) {
    stack->depth = count < stack->depth ? stack->depth - count : 0;
}

static bool AddCall(BC_Program* program, uint32_t* capacity, const SH4_Call* call) {
    if (program->callCount == *capacity) {
        uint32_t grown = *capacity ? *capacity * 2 : 256;
        SH4_Call* calls = realloc(program->calls, grown * sizeof(SH4_Call));
        if (calls == NULL)
            return false;
        program->calls = calls;
        *capacity = grown;
    }
    program->calls[program->callCount++] = *call;
    return true;
}

// The calls with their arguments: the values on the stack are followed through the instructions of each block, as
// constants, addresses of the data or of the code
static bool FindCalls(BC_Program* program) {
    const SCN_Header* header = &program->scn->header;
    uint32_t capacity = 0;
    Stack stack = { 0 };
    int function = -1;
    for (uint32_t pc = header->codeStart; pc < program->codeEnd;) {
        BC_Instruction ins;
        if (!(program->starts[pc] & START_INSTRUCTION) || !Decode(program, pc, &ins)) {
            pc++;
            continue;
        }
        // A target can be reached with another stack
        if (program->starts[pc] & START_TARGET)
            stack.depth = 0;
        if (function + 1 < (int)program->functionCount && program->functions[function + 1] <= pc) {
            function++;
            stack.depth = 0;
        }
        SH4_Value value = { SH4_VALUE_UNKNOWN, 0, 0 };
        switch (ins.kind) {
        case BC_PUSH:
            // An address of the program in its data is an address of the data
            if (ins.area == BC_AREA_PROGRAM && ins.target >= header->dataStart && ins.target < header->end) {
                value.kind = SH4_VALUE_DATA;
                value.value = ins.target - header->dataStart;
            } else if (ins.area == BC_AREA_CONSTANT || ins.area == BC_AREA_PROGRAM) {
                value.kind = SH4_VALUE_CONSTANT;
                value.value = (uint32_t)ins.operand;
            } else if (ins.area == BC_AREA_DATA && ins.width == 0) {
                value.kind = SH4_VALUE_DATA;
                value.value = (uint32_t)ins.operand;
            }
            Push(&stack, value);
            break;
        case BC_OPERATOR:
            Pop(&stack, (uint32_t)OperatorPops(ins.byte));
            if (OperatorPushes(ins.byte))
                Push(&stack, value);
            break;
        case BC_STACK:
            if (ins.operand >= 0)
                Pop(&stack, (uint32_t)ins.operand / 4);
            for (int32_t i = 0; i < -ins.operand / 4; i++)
                Push(&stack, value);
            break;
        case BC_CALL:
        case BC_GAME:
        case BC_SERVICE2C:
        case BC_SERVICE30:
        case BC_DATA_SERVICE: {
            SH4_Call call;
            memset(&call, 0, sizeof(call));
            call.address = pc;
            call.function = function >= 0 ? (uint32_t)function : 0;
            call.id = ins.kind == BC_CALL ? -1 : ins.kind == BC_DATA_SERVICE ? 1 : ins.operand;
            call.kind = ins.kind == BC_CALL ? SH4_CALL_LOCAL : ins.kind == BC_GAME ? SH4_CALL_GAME :
                ins.kind == BC_SERVICE30 ? SH4_CALL_SERVICE30 : SH4_CALL_SERVICE2C;
            call.context = call.kind == SH4_CALL_SERVICE30 ? 0x30 : call.kind == SH4_CALL_SERVICE2C ? 0x2C : 0x28;
            call.target = ins.target;
            call.argumentBytes = -1;
            BC_Instruction next;
            if (ins.kind == BC_DATA_SERVICE) {
                call.argumentBytes = 8;
                call.argumentCount = 2;
                call.arguments[0].kind = SH4_VALUE_DATA;
                call.arguments[0].value = ins.target - header->dataStart;
                call.arguments[1].kind = SH4_VALUE_CONSTANT;
                call.arguments[1].value = (uint32_t)-1;
            } else if (Decode(program, pc + ins.size, &next) && next.kind == BC_STACK && next.operand > 0 &&
                next.operand % 4 == 0) {
                // The values pushed before the call, the last one first
                call.argumentBytes = next.operand;
                uint32_t count = (uint32_t)next.operand / 4;
                call.argumentCount = count < SH4_MAX_ARGUMENTS ? count : SH4_MAX_ARGUMENTS;
                for (uint32_t i = 0; i < call.argumentCount; i++) {
                    if (i < stack.depth)
                        call.arguments[i] = stack.values[stack.depth - 1 - i];
                }
            }
            if (!AddCall(program, &capacity, &call))
                return false;
            break;
        }
        case BC_RET:
        case BC_JUMP:
        case BC_BRANCH:
        case BC_CALL_POP:
            stack.depth = 0;
            break;
        default:
            break;
        }
        pc += ins.size;
    }
    return true;
}

bool BC_AnalyzeProgram(BC_Program* program, const SCN_Program* scn) {
    memset(program, 0, sizeof(*program));
    program->scn = scn;
    const SCN_Header* header = &scn->header;
    if (SCN_IsSH4(scn))
        return false;
    program->codeEnd = header->dataStart;
    program->starts = calloc(header->dataStart + 1, 1);
    if (program->starts == NULL)
        return false;
    for (uint32_t pc = header->codeStart; pc < program->codeEnd;) {
        BC_Instruction ins;
        if (!Decode(program, pc, &ins)) {
            // The padding that ends the code
            uint32_t end = pc;
            while (end < header->dataStart && scn->data[end] == PADDING)
                end++;
            if (end == header->dataStart) {
                program->codeEnd = pc;
                break;
            }
            program->invalidCount++;
            pc++;
            continue;
        }
        program->starts[pc] |= START_INSTRUCTION;
        program->instructionCount++;
        pc += ins.size;
    }
    for (uint32_t pc = header->codeStart; pc < program->codeEnd; pc++) {
        BC_Instruction ins;
        if (!(program->starts[pc] & START_INSTRUCTION) || !Decode(program, pc, &ins))
            continue;
        if ((ins.kind == BC_CALL || ins.kind == BC_JUMP || ins.kind == BC_BRANCH || ins.kind == BC_BRANCH_ZERO) &&
            ins.target < program->codeEnd)
            program->starts[ins.target] |= START_TARGET;
    }
    if (!FindFunctions(program) || !FindCalls(program)) {
        BC_FreeProgram(program);
        return false;
    }
    return true;
}

void BC_FreeProgram(BC_Program* program) {
    free(program->functions);
    free(program->calls);
    free(program->starts);
    memset(program, 0, sizeof(*program));
}

int BC_FunctionAt(const BC_Program* program, uint32_t address) {
    if (address < program->scn->header.codeStart || address >= program->codeEnd)
        return -1;
    uint32_t low = 0, high = program->functionCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (program->functions[middle] <= address)
            low = middle + 1;
        else
            high = middle;
    }
    return (int)low - 1;
}

uint32_t BC_FunctionEnd(const BC_Program* program, uint32_t function) {
    return function + 1 < program->functionCount ? program->functions[function + 1] : program->codeEnd;
}

const SH4_Call* BC_CallAt(const BC_Program* program, uint32_t address) {
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

static void DescribeValue(const BC_Program* program, const SH4_Value* value, char* text, size_t size) {
    if (value->kind == SH4_VALUE_CONSTANT) {
        SH4_DescribeNumber(value->value, text, size);
    } else if (value->kind == SH4_VALUE_DATA) {
        const char* string = BC_DataString(program, value->value);
        if (string != NULL)
            SH4_QuoteString(string, text, size);
        else
            snprintf(text, size, "data+0x%X", value->value);
    } else {
        snprintf(text, size, "?");
    }
}

void BC_DescribeCall(const BC_Program* program, const SH4_Call* call, char* text, size_t size) {
    size_t length;
    const char* name = call->kind == SH4_CALL_GAME ? SH4_GameName(call->id) : NULL;
    if (call->kind == SH4_CALL_LOCAL)
        length = (size_t)snprintf(text, size, "call 0x%X", call->target);
    else if (call->kind == SH4_CALL_GAME)
        length = (size_t)snprintf(text, size, name != NULL ? "game 0x%X %s" : "game 0x%X", (unsigned)call->id, name);
    else
        length = (size_t)snprintf(text, size, "ctx[0x%X] 0x%X", call->context, (unsigned)call->id);
    if (call->argumentBytes < 0 || length >= size)
        return;
    length += (size_t)snprintf(text + length, size - length, "(");
    for (uint32_t i = 0; i < call->argumentCount && length < size; i++) {
        char value[96];
        DescribeValue(program, &call->arguments[i], value, sizeof(value));
        length += (size_t)snprintf(text + length, size - length, i > 0 ? ", %s" : "%s", value);
    }
    if (length < size)
        snprintf(text + length, size - length, (uint32_t)call->argumentBytes / 4 > call->argumentCount ? ", ...)" : ")");
}

uint32_t BC_MaxFunctionLines(const BC_Program* program, uint32_t function) {
    if (function >= program->functionCount)
        return 0;
    return BC_FunctionEnd(program, function) - program->functions[function] + 1;
}

static void InstructionLine(const BC_Program* program, const BC_Instruction* ins, SH4_Line* line) {
    line->kind = SH4_LINE_INSTRUCTION;
    line->size = ins->size;
    BC_Format(ins, line->text, sizeof(line->text));
    const SH4_Call* call = BC_CallAt(program, ins->address);
    if (call != NULL)
        BC_DescribeCall(program, call, line->comment, sizeof(line->comment));
    if (ins->kind == BC_PUSH && ins->area == BC_AREA_CONSTANT && (ins->operand <= -10000 || ins->operand >= 10000))
        SH4_DescribeNumber((uint32_t)ins->operand, line->comment, sizeof(line->comment));
    else if (ins->kind == BC_PUSH && ((ins->area == BC_AREA_DATA && ins->width == 0) ||
        (ins->area == BC_AREA_PROGRAM && ins->target >= program->scn->header.dataStart))) {
        uint32_t offset = ins->area == BC_AREA_DATA ? (uint32_t)ins->operand : ins->target - program->scn->header.dataStart;
        const char* string = BC_DataString(program, offset);
        if (string != NULL)
            SH4_QuoteString(string, line->comment, sizeof(line->comment));
        else
            snprintf(line->comment, sizeof(line->comment), "data+0x%X", offset);
    }
    if ((ins->kind == BC_CALL || ins->kind == BC_JUMP || ins->kind == BC_BRANCH || ins->kind == BC_BRANCH_ZERO ||
        (ins->kind == BC_PUSH && ins->area == BC_AREA_PROGRAM)) && BC_FunctionAt(program, ins->target) >= 0)
        line->jump = ins->target;
}

uint32_t BC_FunctionLines(const BC_Program* program, uint32_t function, SH4_Line* lines) {
    if (function >= program->functionCount)
        return 0;
    uint32_t start = program->functions[function], end = BC_FunctionEnd(program, function), count = 0, callers = 0;
    for (uint32_t c = 0; c < program->callCount; c++)
        callers += program->calls[c].kind == SH4_CALL_LOCAL && program->calls[c].target == start;
    SH4_Line* header = &lines[count++];
    memset(header, 0, sizeof(*header));
    header->kind = SH4_LINE_FUNCTION;
    header->address = start;
    snprintf(header->text, sizeof(header->text), "function 0x%X", start);
    snprintf(header->comment, sizeof(header->comment), "%u local callers", callers);
    for (uint32_t pc = start; pc < end;) {
        SH4_Line* line = &lines[count++];
        memset(line, 0, sizeof(*line));
        line->address = pc;
        line->label = (program->starts[pc] & START_TARGET) && pc != start;
        BC_Instruction ins;
        if ((program->starts[pc] & START_INSTRUCTION) && Decode(program, pc, &ins)) {
            InstructionLine(program, &ins, line);
        } else {
            line->kind = SH4_LINE_UNKNOWN;
            line->size = 1;
            snprintf(line->text, sizeof(line->text), ".byte 0x%02X", program->scn->data[pc]);
        }
        pc += line->size;
    }
    return count;
}

void BC_WriteListing(const BC_Program* program, FILE* file) {
    uint32_t max = 0;
    for (uint32_t f = 0; f < program->functionCount; f++) {
        uint32_t lines = BC_MaxFunctionLines(program, f);
        max = lines > max ? lines : max;
    }
    SH4_Line* lines = malloc((max + 1) * sizeof(SH4_Line));
    if (lines == NULL)
        return;
    for (uint32_t f = 0; f < program->functionCount; f++) {
        uint32_t count = BC_FunctionLines(program, f, lines);
        for (uint32_t i = 0; i < count; i++) {
            const SH4_Line* line = &lines[i];
            if (line->kind == SH4_LINE_FUNCTION) {
                fprintf(file, "\n; %s: %s\n", line->text, line->comment);
                continue;
            }
            char bytes[16] = "";
            for (uint32_t b = 0; b < line->size && b < 5; b++)
                snprintf(bytes + b * 3, sizeof(bytes) - b * 3, "%02X ", program->scn->data[line->address + b]);
            fprintf(file, "%08X  %-15s %c %-28s%s%s\n", line->address, bytes, line->label ? '>' : ' ', line->text,
                line->comment[0] != '\0' ? "  ; " : "", line->comment);
        }
    }
    free(lines);
}

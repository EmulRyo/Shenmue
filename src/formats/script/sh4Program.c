#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/script/sh4Program.h"

#define CONTEXT_REGISTER    8   // r8: the context, for all the functions
#define STACK_REGISTER      13  // r13: the stack of the program
#define ID_REGISTER         5   // r5: ID of the calls to the game
#define DATA_FIELD          0x04    // ctx[0x04]: base of the data
#define MAX_PUSHES          32

// Grows an array of count elements to hold one more. Returns false on memory error
static bool Grow(void** items, uint32_t count, uint32_t* capacity, size_t size) {
    if (count < *capacity)
        return true;
    uint32_t newCapacity = *capacity ? *capacity * 2 : 64;
    void* grown = realloc(*items, newCapacity * size);
    if (grown == NULL)
        return false;
    *items = grown;
    *capacity = newCapacity;
    return true;
}

static bool IsCode(const SH4_Program* program, uint32_t address) {
    return (SH4_WordKind(&program->code, address) & SH4_WORD_CODE) != 0;
}

static bool Decode(const SH4_Program* program, uint32_t address, SH4_Instruction* ins) {
    return SH4_Decode(Bytes_ReadU16(program->code.data + address), address, 0, ins);
}

static bool TemplateIs(const SH4_Instruction* ins, const char* text) {
    return strcmp(SH4_EntryTemplate(ins->entry), text) == 0;
}

static bool EndsWith(const char* text, const char* end) {
    size_t length = strlen(text), endLength = strlen(end);
    return length >= endLength && strcmp(text + length - endLength, end) == 0;
}

// Registers that an instruction writes (bit per register): "op ...,rn", "op rn" (shifts, dt, movt), "@-rn", "@rn+",
// "@rm+", "...,r0"
static uint32_t WrittenRegisters(const SH4_Instruction* ins) {
    const char* text = SH4_EntryTemplate(ins->entry);
    uint32_t written = 0;
    if (EndsWith(text, ",%N") || strstr(text, "@-%N") != NULL || strstr(text, "@%N+") != NULL)
        written |= 1u << ins->n;
    if (strstr(text, "@%M+") != NULL)
        written |= 1u << ins->m;
    if (EndsWith(text, ",r0") || EndsWith(text, ",r0_bank"))
        written |= 1u;
    // One register operand that is modified: shifts, rotations, dt, movt
    const char* space = strchr(text, ' ');
    if (space != NULL && strcmp(space, " %N") == 0 && strncmp(text, "cmp/", 4) != 0 && strncmp(text, "jmp", 3) != 0 &&
        strncmp(text, "jsr", 3) != 0 && strncmp(text, "braf", 4) != 0 && strncmp(text, "bsrf", 4) != 0)
        written |= 1u << ins->n;
    return written;
}

static SH4_Literal* FindLiteral(SH4_Program* program, uint32_t address) {
    uint32_t low = 0, high = program->literalCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (program->literals[middle].address < address)
            low = middle + 1;
        else
            high = middle;
    }
    return low < program->literalCount && program->literals[low].address == address ? &program->literals[low] : NULL;
}

const SH4_Literal* SH4_LiteralAt(const SH4_Program* program, uint32_t address) {
    return FindLiteral((SH4_Program*)program, address);
}

// The literal that gave a value is used as kind (the specific uses win over a plain value)
static void MarkLiteral(SH4_Program* program, const SH4_Value* value, SH4_LiteralKind kind) {
    SH4_Literal* literal = value->literal ? FindLiteral(program, value->literal) : NULL;
    if (literal != NULL && literal->kind == SH4_LITERAL_VALUE)
        literal->kind = (uint8_t)kind;
}

int SH4_FunctionAt(const SH4_Program* program, uint32_t address) {
    uint32_t low = 0, high = program->functionCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (program->functions[middle].start <= address)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == 0 || address >= program->functions[low - 1].end)
        return -1;
    return (int)low - 1;
}

const char* SH4_CallKindName(SH4_CallKind kind) {
    switch (kind) {
    case SH4_CALL_LOCAL:        return "local";
    case SH4_CALL_GAME:         return "game";
    case SH4_CALL_SERVICE2C:    return "service 2C";
    case SH4_CALL_SERVICE30:    return "service 30";
    case SH4_CALL_YIELD:        return "yield";
    case SH4_CALL_CONTEXT:      return "context";
    default:                    return "unknown";
    }
}

static bool FindFunctions(SH4_Program* program) {
    uint32_t capacity = 0;
    for (uint32_t pc = program->code.start; pc < program->code.end; pc += 2) {
        if (!(SH4_WordKind(&program->code, pc) & SH4_WORD_FUNCTION))
            continue;
        if (!Grow((void**)&program->functions, program->functionCount, &capacity, sizeof(SH4_Function)))
            return false;
        SH4_Function* function = &program->functions[program->functionCount++];
        memset(function, 0, sizeof(*function));
        function->start = pc;
    }
    for (uint32_t i = 0; i < program->functionCount; i++)
        program->functions[i].end = i + 1 < program->functionCount ? program->functions[i + 1].start : program->code.end;
    return true;
}

static int CompareLiterals(const void* a, const void* b) {
    uint32_t x = ((const SH4_Literal*)a)->address, y = ((const SH4_Literal*)b)->address;
    return x < y ? -1 : x > y;
}

static bool FindLiterals(SH4_Program* program) {
    uint32_t capacity = 0;
    for (uint32_t pc = program->code.start; pc < program->code.end; pc += 2) {
        SH4_Instruction ins;
        if (!IsCode(program, pc) || !Decode(program, pc, &ins) || ins.literalSize == 0)
            continue;
        if (!Grow((void**)&program->literals, program->literalCount, &capacity, sizeof(SH4_Literal)))
            return false;
        SH4_Literal* literal = &program->literals[program->literalCount++];
        memset(literal, 0, sizeof(*literal));
        literal->address = ins.target;
        literal->size = (uint8_t)ins.literalSize;
        literal->value = ins.literalSize == 4 ? Bytes_ReadU32(program->code.data + ins.target) :
            Bytes_ReadU16(program->code.data + ins.target);
        literal->reads = 1;
    }
    if (program->literalCount == 0)
        return true;
    qsort(program->literals, program->literalCount, sizeof(SH4_Literal), CompareLiterals);
    uint32_t count = 1;
    for (uint32_t i = 1; i < program->literalCount; i++) {
        SH4_Literal* last = &program->literals[count - 1];
        if (program->literals[i].address == last->address)
            last->reads++;
        else
            program->literals[count++] = program->literals[i];
    }
    program->literalCount = count;
    return true;
}

// Blocks: they start at a function, a branch target or after a branch (and its delay slot), and end after a branch
static bool FindBlocks(SH4_Program* program) {
    uint32_t capacity = 0;
    SH4_Block* block = NULL;
    for (uint32_t pc = program->code.start; pc < program->code.end; pc += 2) {
        if (!IsCode(program, pc)) {
            block = NULL;
            continue;
        }
        uint8_t kind = SH4_WordKind(&program->code, pc);
        if (block == NULL || block->end != pc || (kind & (SH4_WORD_TARGET | SH4_WORD_FUNCTION))) {
            if (!Grow((void**)&program->blocks, program->blockCount, &capacity, sizeof(SH4_Block)))
                return false;
            block = &program->blocks[program->blockCount++];
            memset(block, 0, sizeof(*block));
            block->start = pc;
            block->end = pc;
            int function = SH4_FunctionAt(program, pc);
            block->function = function >= 0 ? (uint32_t)function : 0;
        }
        SH4_Instruction ins;
        Decode(program, pc, &ins);
        block->end = pc + 2;
        block->fallsThrough = true;
        if (!(ins.flags & SH4_BRANCH))
            continue;
        // The delay slot belongs to the block of its branch
        if ((ins.flags & SH4_DELAYED) && IsCode(program, pc + 2)) {
            pc += 2;
            block->end = pc + 2;
        }
        uint32_t target = 0;
        if (ins.flags & SH4_INDIRECT) {
            if ((ins.opcode & 0xF0DF) == 0x0003)    // braf, bsrf
                SH4_RelativeTarget(&program->code, &ins, &target);
        } else if (!(ins.flags & SH4_RETURN)) {
            target = ins.target;
        }
        block->target = (ins.flags & SH4_CALL) ? 0 : target;
        block->fallsThrough = (ins.flags & (SH4_CONDITIONAL | SH4_CALL)) != 0;
        if (!block->fallsThrough || (ins.flags & SH4_CONDITIONAL))
            block = NULL;
    }
    for (uint32_t i = 0; i < program->blockCount; i++) {
        SH4_Function* function = &program->functions[program->blocks[i].function];
        if (function->blockCount++ == 0)
            function->firstBlock = i;
    }
    return true;
}

static SH4_Value Unknown(void) {
    SH4_Value value = { SH4_VALUE_UNKNOWN, 0, 0 };
    return value;
}

static SH4_Value Constant(uint32_t constant, uint32_t literal) {
    SH4_Value value = { SH4_VALUE_CONSTANT, constant, literal };
    return value;
}

typedef struct {
    SH4_Value   regs[16];
    SH4_Value   pushes[MAX_PUSHES];
    uint32_t    pushCount;
} State;

static void ResetState(State* state) {
    memset(state, 0, sizeof(*state));
    state->regs[CONTEXT_REGISTER].kind = SH4_VALUE_CONTEXT;
    state->regs[STACK_REGISTER].kind = SH4_VALUE_STACK;
}

// Sum of two values: the base of the data plus a constant is a pointer into the data
static SH4_Value Add(SH4_Program* program, SH4_Value a, SH4_Value b) {
    if (a.kind == SH4_VALUE_CONSTANT && b.kind != SH4_VALUE_CONSTANT) {
        SH4_Value swap = a;
        a = b;
        b = swap;
    }
    if (b.kind != SH4_VALUE_CONSTANT)
        return Unknown();
    switch (a.kind) {
    case SH4_VALUE_CONSTANT:
        return Constant(a.value + b.value, 0);
    case SH4_VALUE_FIELD:
        if (a.value != DATA_FIELD)
            return Unknown();
        MarkLiteral(program, &b, SH4_LITERAL_DATA_OFFSET);
        a.kind = SH4_VALUE_DATA;
        a.value = b.value;
        a.literal = b.literal;
        return a;
    case SH4_VALUE_DATA:
    case SH4_VALUE_STACK:
        a.value += b.value;
        return a;
    default:
        return Unknown();
    }
}

static bool AddCall(SH4_Program* program, uint32_t* capacity, const SH4_Call* call) {
    if (!Grow((void**)&program->calls, program->callCount, capacity, sizeof(SH4_Call)))
        return false;
    program->calls[program->callCount++] = *call;
    return true;
}

// Bytes popped from r13 by the instruction after a call (add #n,r13), -1 if none
static int32_t PoppedBytes(const SH4_Program* program, uint32_t address) {
    SH4_Instruction ins;
    if (!IsCode(program, address) || !Decode(program, address, &ins) || !TemplateIs(&ins, "add %I,%N") ||
        ins.n != STACK_REGISTER || ins.immediate <= 0)
        return -1;
    return ins.immediate;
}

// Arguments of a call: the last pushes of the block, as many as the bytes popped after it. The called function reads them
// from the stack, the last push first (r6 points to it): the first argument is the last push. The arguments pushed
// before the block are unknown
static void SetArguments(SH4_Program* program, SH4_Call* call, State* state) {
    call->argumentBytes = PoppedBytes(program, call->address + 4);
    if (call->argumentBytes < 0 || call->argumentBytes % 4 != 0)
        return;
    uint32_t count = (uint32_t)call->argumentBytes / 4;
    call->argumentCount = count < SH4_MAX_ARGUMENTS ? count : SH4_MAX_ARGUMENTS;
    for (uint32_t i = 0; i < call->argumentCount; i++) {
        uint32_t fromEnd = i + 1; // 1 = last push
        call->arguments[i] = fromEnd <= state->pushCount ? state->pushes[state->pushCount - fromEnd] : Unknown();
    }
    state->pushCount = count < state->pushCount ? state->pushCount - count : 0;
}

static void Execute(SH4_Program* program, State* state, const SH4_Instruction* ins) {
    SH4_Value* regs = state->regs;
    const char* text = SH4_EntryTemplate(ins->entry);
    if (ins->flags & SH4_LITERAL) {
        uint32_t value = ins->literalSize == 4 ? Bytes_ReadU32(program->code.data + ins->target) :
            (uint32_t)(int32_t)(int16_t)Bytes_ReadU16(program->code.data + ins->target);
        regs[ins->n] = Constant(value, ins->target);
        return;
    }
    if (strcmp(text, "mov %I,%N") == 0) {
        regs[ins->n] = Constant((uint32_t)ins->immediate, 0);
    } else if (strcmp(text, "mov %M,%N") == 0) {
        regs[ins->n] = regs[ins->m];
    } else if (strcmp(text, "mov.l @(%D,%M),%N") == 0 && regs[ins->m].kind == SH4_VALUE_CONTEXT) {
        SH4_Value field = { SH4_VALUE_FIELD, (uint32_t)ins->disp, 0 };
        regs[ins->n] = field;
    } else if (strcmp(text, "add %M,%N") == 0) {
        regs[ins->n] = Add(program, regs[ins->n], regs[ins->m]);
    } else if (strcmp(text, "add %I,%N") == 0) {
        regs[ins->n] = Add(program, regs[ins->n], Constant((uint32_t)ins->immediate, 0));
        if (ins->n == STACK_REGISTER && ins->immediate > 0) {
            uint32_t popped = (uint32_t)ins->immediate / 4;
            state->pushCount = popped < state->pushCount ? state->pushCount - popped : 0;
        }
    } else if (strcmp(text, "mov.l %M,@-%N") == 0 && ins->n == STACK_REGISTER) {
        if (state->pushCount == MAX_PUSHES) {
            memmove(state->pushes, state->pushes + 1, (MAX_PUSHES - 1) * sizeof(SH4_Value));
            state->pushCount--;
        }
        state->pushes[state->pushCount++] = regs[ins->m];
        regs[STACK_REGISTER] = Add(program, regs[STACK_REGISTER], Constant((uint32_t)-4, 0));
    } else {
        uint32_t written = WrittenRegisters(ins);
        for (int r = 0; r < 16; r++) {
            if (written & (1u << r))
                regs[r] = Unknown();
        }
    }
}

// Analyzes the calls and the uses of the literals of a block, following the registers from its start
static bool AnalyzeBlock(SH4_Program* program, const SH4_Block* block, uint32_t* callCapacity, bool startCode) {
    State state;
    ResetState(&state);
    for (uint32_t pc = block->start; pc < block->end; pc += 2) {
        SH4_Instruction ins, slot;
        if (!Decode(program, pc, &ins))
            continue;
        if (!startCode && (WrittenRegisters(&ins) & (1u << CONTEXT_REGISTER)))
            program->contextWrites++;
        if (!(ins.flags & SH4_BRANCH)) {
            Execute(program, &state, &ins);
            continue;
        }
        // The delay slot runs before the branch
        bool hasSlot = (ins.flags & SH4_DELAYED) && pc + 2 < block->end && Decode(program, pc + 2, &slot);
        SH4_Value target = state.regs[ins.n];   // Register of jsr, braf, bsrf, read before the slot
        if (hasSlot) {
            if (!startCode && (WrittenRegisters(&slot) & (1u << CONTEXT_REGISTER)))
                program->contextWrites++;
            Execute(program, &state, &slot);
        }
        bool relative = (ins.opcode & 0xF0DF) == 0x0003; // braf, bsrf
        if (relative)
            MarkLiteral(program, &target, SH4_LITERAL_CODE_OFFSET);
        if (ins.flags & SH4_CALL) {
            SH4_Call call;
            memset(&call, 0, sizeof(call));
            call.address = pc;
            call.function = block->function;
            call.id = -1;
            call.kind = SH4_CALL_UNKNOWN;
            if (!(ins.flags & SH4_INDIRECT)) {
                call.kind = SH4_CALL_LOCAL;
                call.target = ins.target;
            } else if (relative) {
                if (SH4_RelativeTarget(&program->code, &ins, &call.target))
                    call.kind = SH4_CALL_LOCAL;
            } else if (target.kind == SH4_VALUE_FIELD) {
                call.context = (uint8_t)target.value;
                call.kind = target.value == 0x28 ? SH4_CALL_GAME : target.value == 0x2C ? SH4_CALL_SERVICE2C :
                    target.value == 0x30 ? SH4_CALL_SERVICE30 : target.value == 0x3C ? SH4_CALL_YIELD :
                    SH4_CALL_CONTEXT;
                const SH4_Value* id = &state.regs[ID_REGISTER];
                if (call.kind != SH4_CALL_YIELD && id->kind == SH4_VALUE_CONSTANT) {
                    call.id = (int32_t)id->value;
                    MarkLiteral(program, id, SH4_LITERAL_CALL_ID);
                }
            }
            SetArguments(program, &call, &state);
            if (!AddCall(program, callCapacity, &call))
                return false;
            // The called code can change r0 to r7
            for (int r = 0; r <= 7; r++)
                state.regs[r] = Unknown();
        }
        pc += hasSlot ? 2 : 0;
    }
    return true;
}

bool SH4_AnalyzeProgram(SH4_Program* program, const uint8_t* data, uint32_t codeStart, uint32_t dataStart,
    uint32_t dataEnd, uint32_t entry) {
    memset(program, 0, sizeof(*program));
    program->dataStart = dataStart;
    program->dataEnd = dataEnd;
    uint32_t roots[2] = { codeStart, entry };
    if (!SH4_ExploreCode(&program->code, data, codeStart, dataStart, roots, 2))
        return false;
    bool ok = FindFunctions(program) && FindLiterals(program) && FindBlocks(program);
    uint32_t callCapacity = 0;
    for (uint32_t i = 0; i < program->blockCount && ok; i++) {
        bool startCode = program->functions[program->blocks[i].function].start == codeStart;
        ok = AnalyzeBlock(program, &program->blocks[i], &callCapacity, startCode);
    }
    for (uint32_t i = 0; i < program->callCount && ok; i++) {
        if (program->calls[i].kind == SH4_CALL_LOCAL) {
            int callee = SH4_FunctionAt(program, program->calls[i].target);
            if (callee >= 0 && program->functions[callee].start == program->calls[i].target)
                program->functions[callee].callers++;
        }
    }
    if (!ok)
        SH4_FreeProgram(program);
    return ok;
}

void SH4_FreeProgram(SH4_Program* program) {
    SH4_FreeCode(&program->code);
    free(program->functions);
    free(program->blocks);
    free(program->calls);
    free(program->literals);
    memset(program, 0, sizeof(*program));
}

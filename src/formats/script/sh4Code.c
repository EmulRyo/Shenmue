#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/script/sh4Code.h"

#define PROLOGUE        0x2DE6  // mov.l r14,@-r13: first instruction of the functions of the scene programs
#define LOOKBACK        8       // Instructions searched before a braf or bsrf for the load of its register

typedef struct {
    uint32_t*   items;
    uint32_t    count;
    uint32_t    capacity;
} Stack;

static bool Push(Stack* stack, uint32_t address) {
    if (stack->count == stack->capacity) {
        uint32_t capacity = stack->capacity ? stack->capacity * 2 : 256;
        uint32_t* items = realloc(stack->items, capacity * sizeof(uint32_t));
        if (items == NULL)
            return false;
        stack->items = items;
        stack->capacity = capacity;
    }
    stack->items[stack->count++] = address;
    return true;
}

static bool Inside(const SH4_Code* code, uint32_t address) {
    return address >= code->start && address < code->end && address % 2 == 0;
}

static uint8_t* Word(SH4_Code* code, uint32_t address) {
    return &code->words[(address - code->start) / 2];
}

uint8_t SH4_WordKind(const SH4_Code* code, uint32_t address) {
    return Inside(code, address) ? code->words[(address - code->start) / 2] : 0;
}

static void Error(SH4_Code* code, uint32_t* counter, uint32_t address) {
    (*counter)++;
    if (code->stats.firstError == 0)
        code->stats.firstError = address;
}

static bool Decode(const SH4_Code* code, uint32_t address, SH4_Instruction* ins) {
    return SH4_Decode(Bytes_ReadU16(code->data + address), address, 0, ins);
}

// The instruction writes (or modifies) its register n: "op ...,rn", "@-rn", "@rn+"
static bool WritesN(const SH4_Instruction* ins) {
    const char* text = SH4_EntryTemplate(ins->entry);
    size_t length = strlen(text);
    return (length >= 3 && strcmp(text + length - 3, ",%N") == 0) || strstr(text, "@-%N") != NULL ||
        strstr(text, "@%N+") != NULL;
}

bool SH4_RelativeTarget(const SH4_Code* code, const SH4_Instruction* branch, uint32_t* target) {
    for (uint32_t back = 1; back <= LOOKBACK; back++) {
        uint32_t pc = branch->address - back * 2;
        if (!Inside(code, pc) || !(SH4_WordKind(code, pc) & SH4_WORD_CODE))
            return false;
        SH4_Instruction ins;
        if (!Decode(code, pc, &ins))
            return false;
        if ((ins.flags & SH4_LITERAL) && ins.literalSize == 4 && ins.n == branch->n) {
            if (!Inside(code, ins.target) || ins.target + 4 > code->end)
                return false;
            *target = branch->address + 4 + Bytes_ReadU32(code->data + ins.target);
            return true;
        }
        if (WritesN(&ins) && ins.n == branch->n)
            return false;
        // Another path can reach the instructions after a target or a branch
        if ((ins.flags & SH4_BRANCH) || (SH4_WordKind(code, pc + 2) & SH4_WORD_TARGET))
            return false;
    }
    return false;
}

static void MarkLiteral(SH4_Code* code, const SH4_Instruction* ins) {
    if (ins->literalSize == 0)
        return;
    if (!Inside(code, ins->target) || ins->target + ins->literalSize > code->end) {
        Error(code, &code->stats.outside, ins->address);
        return;
    }
    for (uint32_t address = ins->target; address < ins->target + ins->literalSize; address += 2) {
        if (*Word(code, address) & SH4_WORD_CODE)
            Error(code, &code->stats.overlaps, address);
        *Word(code, address) |= SH4_WORD_LITERAL;
    }
}

// Marks a branch target and pushes it to be explored
static bool AddTarget(SH4_Code* code, Stack* stack, uint32_t target, bool function, uint32_t from) {
    if (!Inside(code, target)) {
        Error(code, &code->stats.outside, from);
        return true;
    }
    *Word(code, target) |= function ? SH4_WORD_FUNCTION | SH4_WORD_TARGET : SH4_WORD_TARGET;
    return Push(stack, target);
}

// Decodes and marks the instruction at pc as code. Returns false if it can't be code
static bool MarkCode(SH4_Code* code, uint32_t pc, bool slot, SH4_Instruction* ins) {
    if (!Inside(code, pc)) {
        Error(code, &code->stats.outside, pc);
        return false;
    }
    if (*Word(code, pc) & SH4_WORD_LITERAL) {
        Error(code, &code->stats.overlaps, pc);
        return false;
    }
    if (!Decode(code, pc, ins)) {
        Error(code, &code->stats.invalid, pc);
        return false;
    }
    *Word(code, pc) |= SH4_WORD_CODE | (slot ? SH4_WORD_SLOT : 0);
    MarkLiteral(code, ins);
    return true;
}

static bool Explore(SH4_Code* code, Stack* stack) {
    while (stack->count > 0) {
        uint32_t pc = stack->items[--stack->count];
        for (;;) {
            if (Inside(code, pc) && (*Word(code, pc) & SH4_WORD_CODE))
                break;
            SH4_Instruction ins;
            if (!MarkCode(code, pc, false, &ins))
                break;
            if (!(ins.flags & SH4_BRANCH)) {
                pc += 2;
                continue;
            }
            if (ins.flags & SH4_DELAYED) {
                SH4_Instruction slot;
                if (Inside(code, pc + 2) && (*Word(code, pc + 2) & SH4_WORD_CODE)) {
                    // Already explored, e.g. the delay slot of a branch that is also the target of another one
                    if (Decode(code, pc + 2, &slot) && (slot.flags & SH4_SLOT_ILLEGAL))
                        Error(code, &code->stats.illegalSlots, pc + 2);
                    *Word(code, pc + 2) |= SH4_WORD_SLOT;
                } else if (MarkCode(code, pc + 2, true, &slot) && (slot.flags & SH4_SLOT_ILLEGAL)) {
                    Error(code, &code->stats.illegalSlots, pc + 2);
                }
            }
            bool call = (ins.flags & SH4_CALL) != 0;
            if (ins.flags & SH4_INDIRECT) {
                uint32_t target;
                bool relative = (ins.opcode & 0xF0DF) == 0x0003; // braf, bsrf
                if (relative && SH4_RelativeTarget(code, &ins, &target)) {
                    if (!AddTarget(code, stack, target, call, pc))
                        return false;
                } else if (relative) {
                    Error(code, &code->stats.unresolved, pc);
                }
            } else if (!(ins.flags & SH4_RETURN)) {
                if (!AddTarget(code, stack, ins.target, call, pc))
                    return false;
            }
            // The flow continues after the conditional branches and the calls
            if (!(ins.flags & (SH4_CONDITIONAL | SH4_CALL)))
                break;
            pc += (ins.flags & SH4_DELAYED) ? 4 : 2;
        }
    }
    return true;
}

bool SH4_ExploreCode(SH4_Code* code, const uint8_t* data, uint32_t start, uint32_t end, const uint32_t* roots,
    uint32_t rootCount) {
    memset(code, 0, sizeof(*code));
    code->data = data;
    code->start = start & ~1u;
    code->end = end > code->start ? end & ~1u : code->start;
    code->words = calloc((code->end - code->start) / 2 + 1, 1);
    Stack stack = { 0 };
    if (code->words == NULL)
        return false;
    bool ok = true;
    for (uint32_t i = 0; i < rootCount && ok; i++)
        ok = AddTarget(code, &stack, roots[i], true, roots[i]);
    ok = ok && Explore(code, &stack);
    // Functions only called through pointers: the unexplored runs that start with the prologue
    for (bool found = true; ok && found;) {
        found = false;
        for (uint32_t pc = code->start; pc + 2 <= code->end && ok; pc += 2) {
            if (*Word(code, pc) == 0 && Bytes_ReadU16(data + pc) == PROLOGUE) {
                ok = AddTarget(code, &stack, pc, true, pc) && Explore(code, &stack);
                found = true;
            }
        }
    }
    free(stack.items);
    if (!ok) {
        SH4_FreeCode(code);
        return false;
    }
    for (uint32_t pc = code->start; pc < code->end; pc += 2) {
        uint8_t kind = *Word(code, pc);
        code->stats.instructions += (kind & SH4_WORD_CODE) != 0;
        code->stats.literals += (kind & SH4_WORD_LITERAL) != 0;
        code->stats.unknown += (kind & (SH4_WORD_CODE | SH4_WORD_LITERAL)) == 0;
        code->stats.functions += (kind & SH4_WORD_FUNCTION) != 0;
    }
    return true;
}

void SH4_FreeCode(SH4_Code* code) {
    free(code->words);
    code->words = NULL;
}

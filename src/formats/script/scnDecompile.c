#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "formats/script/scnDecompile.h"

#define MAX_STACK   128
#define TEXT_LIMIT  2048    // Expressions longer than this are cut

// Precedence of the expressions, as in C
enum {
    PREC_TERNARY = 1, PREC_ASSIGN = 2, PREC_OR = 6, PREC_XOR = 7, PREC_AND = 8, PREC_EQUALITY = 9, PREC_RELATION = 10, PREC_SHIFT = 11,
    PREC_ADD = 12, PREC_MULTIPLY = 13, PREC_UNARY = 14, PREC_PRIMARY = 15,
};

typedef enum {
    ADDRESS_NONE,
    ADDRESS_VARIABLE,       // &var
    ADDRESS_OFFSET,         // &var + offset
    ADDRESS_INDEX,          // &var + (index << shift)
} AddressKind;

// A value of the stack: its text, and what it is when it matters (a constant, an address of a variable)
typedef struct {
    char*       text;
    uint8_t     precedence;
    bool        effect;         // Has side effects: a store or a call
    bool        constant;
    int32_t     value;
    uint8_t     address;        // AddressKind
    char        variable[32];
    int32_t     offset;
    char*       index;          // ADDRESS_INDEX, and the left side of the shifts by a constant
    uint8_t     shift;
    char*       assignment;     // (assignment) & constant: the assignment, which is the statement (a bit field)
} Value;

typedef enum {
    STATEMENT_TEXT,
    STATEMENT_BRANCH,       // goto target
    STATEMENT_BRANCH_ZERO,  // if (!text) goto target
    STATEMENT_RETURN,       // return text (or return)
} StatementKind;

typedef struct {
    uint32_t    address;
    uint32_t    next;       // Address after its instructions
    uint8_t     kind;
    uint32_t    target;
    char*       text;
} Statement;

typedef struct {
    const BC_Program*   program;
    uint32_t            start, end;     // Of the function
    Value               stack[MAX_STACK];
    uint32_t            depth;
    Statement*          statements;
    uint32_t            statementCount, statementCapacity;
    uint32_t            statementStart; // Address of the first instruction of the next statement
    int32_t             argumentCount;  // Of the next service of ctx[0x2C], -1 if not given
    int32_t             maxArgument;    // Highest argN used
    uint32_t            joinLabel;      // Label where the two values of a conditional expression join, not a statement
    bool                failed;         // Memory error
    DC_Function*        out;
    uint32_t            lineCapacity;
} Decompiler;

static char* Format(const char* format, ...) {
    char buffer[TEXT_LIMIT];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    size_t length = strlen(buffer);
    char* text = malloc(length + 1);
    if (text != NULL)
        memcpy(text, buffer, length + 1);
    return text;
}

static void FreeValue(Value* value) {
    free(value->text);
    free(value->index);
    free(value->assignment);
    memset(value, 0, sizeof(*value));
}

static Value MakeValue(uint8_t precedence, bool effect, char* text) {
    Value value;
    memset(&value, 0, sizeof(value));
    value.text = text != NULL ? text : Format("?");
    value.precedence = precedence;
    value.effect = effect;
    return value;
}

static void Push(Decompiler* d, Value value) {
    if (d->depth == MAX_STACK) {
        FreeValue(&d->stack[0]);
        memmove(d->stack, d->stack + 1, (MAX_STACK - 1) * sizeof(Value));
        d->depth--;
    }
    d->stack[d->depth++] = value;
}

static Value Pop(Decompiler* d) {
    if (d->depth == 0) {
        d->out->stackErrors++;
        return MakeValue(PREC_PRIMARY, false, Format("?"));
    }
    return d->stack[--d->depth];
}

// The text of a value as an operand of an operator of a precedence: between parentheses if it binds less
static char* Operand(const Value* value, uint8_t precedence) {
    return value->precedence < precedence ? Format("(%s)", value->text) : Format("%s", value->text);
}

static bool IsFourCC(uint32_t value) {
    for (int i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)(value >> (i * 8));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z')))
            return false;
    }
    return true;
}

static Value Constant(int32_t number) {
    uint32_t bits = (uint32_t)number;
    uint32_t exponent = (bits >> 23) & 0xFF;
    char* text;
    if (number > -10000 && number < 10000)
        text = Format("%d", number);
    else if (IsFourCC(bits))
        text = Format("'%c%c%c%c'", (char)bits, (char)(bits >> 8), (char)(bits >> 16), (char)(bits >> 24));
    else if (exponent >= 107 && exponent <= 150 && (bits & 0xFF) == 0) {
        float f;
        memcpy(&f, &bits, sizeof(f));
        text = Format("%gf", f);
    } else
        text = Format("0x%X", bits);
    Value value = MakeValue(PREC_PRIMARY, false, text);
    value.constant = true;
    value.value = number;
    return value;
}

static void VariableName(Decompiler* d, uint8_t area, int32_t offset, char* name, size_t size) {
    if (area == BC_AREA_LOCAL && offset >= 8) {
        snprintf(name, size, (offset - 8) % 4 == 0 ? "arg%d" : "arg%d_%d", (offset - 8) / 4, (offset - 8) % 4);
        if ((offset - 8) / 4 > d->maxArgument)
            d->maxArgument = (offset - 8) / 4;
    } else if (area == BC_AREA_LOCAL)
        snprintf(name, size, "local_%X", (unsigned)-offset);
    else if (area == BC_AREA_VARIABLE)
        snprintf(name, size, "var_%X", (unsigned)offset);
    else
        snprintf(name, size, "global_%X", (unsigned)offset);
}

static Value AddressValue(const char* variable) {
    Value value = MakeValue(PREC_UNARY, false, NULL);
    snprintf(value.variable, sizeof(value.variable), "%s", variable);
    free(value.text);
    value.text = Format("&%s", value.variable);
    value.address = ADDRESS_VARIABLE;
    return value;
}

// The address of a variable
static Value Address(Decompiler* d, uint8_t area, int32_t offset) {
    char name[32];
    VariableName(d, area, offset, name, sizeof(name));
    return AddressValue(name);
}

// An address of the data of the program: a string, or data_X
static Value DataAddress(Decompiler* d, uint32_t offset) {
    const char* string = BC_DataString(d->program, offset);
    if (string != NULL) {
        char quoted[SH4_LINE_COMMENT];
        SH4_QuoteString(string, quoted, sizeof(quoted));
        return MakeValue(PREC_PRIMARY, false, Format("%s", quoted));
    }
    char name[32];
    snprintf(name, sizeof(name), "data_%X", offset);
    return AddressValue(name);
}

static const char* WidthSuffix(uint8_t width) {
    return width == 1 ? ".b" : width == 2 ? ".w" : "";
}

// The memory at an address, of a width: a variable, an element of an array, or b[a], w[a], l[a]
static char* Access(const Value* address, uint8_t width) {
    const char* suffix = WidthSuffix(width);
    switch (address->address) {
    case ADDRESS_VARIABLE:
        return Format("%s%s", address->variable, suffix);
    case ADDRESS_OFFSET:
        if (address->offset % width == 0)
            return Format("%s%s[%d]", address->variable, suffix, address->offset / width);
        break;
    case ADDRESS_INDEX:
        if ((1 << address->shift) == width)
            return Format("%s%s[%s]", address->variable, suffix, address->index);
        break;
    default:
        break;
    }
    return Format("%c[%s]", width == 1 ? 'b' : width == 2 ? 'w' : 'l', address->text);
}

static Value Load(Decompiler* d, uint8_t width) {
    Value address = Pop(d);
    Value value = MakeValue(PREC_PRIMARY, address.effect, Access(&address, width));
    FreeValue(&address);
    return value;
}

static Value Store(Decompiler* d, uint8_t width) {
    Value stored = Pop(d), address = Pop(d);
    char* target = Access(&address, width);
    char* operand = Operand(&stored, PREC_ASSIGN);
    Value value = MakeValue(PREC_ASSIGN, true, Format("%s = %s", target, operand));
    free(target);
    free(operand);
    FreeValue(&stored);
    FreeValue(&address);
    return value;
}

static void BinaryPrecedence(uint8_t byte, const char** symbol, uint8_t* precedence) {
    switch (byte) {
    case 0x88: *symbol = "=="; *precedence = PREC_EQUALITY; break;
    case 0x89: *symbol = "!="; *precedence = PREC_EQUALITY; break;
    case 0x8A: case 0x9F: *symbol = "<"; *precedence = PREC_RELATION; break;
    case 0x8B: case 0xA0: *symbol = "<="; *precedence = PREC_RELATION; break;
    case 0x8C: case 0xA1: *symbol = ">"; *precedence = PREC_RELATION; break;
    case 0x8D: case 0xA2: *symbol = ">="; *precedence = PREC_RELATION; break;
    case 0x8F: *symbol = "&"; *precedence = PREC_AND; break;
    case 0x90: *symbol = "|"; *precedence = PREC_OR; break;
    case 0x91: *symbol = "^"; *precedence = PREC_XOR; break;
    case 0x92: case 0xA3: *symbol = "+"; *precedence = PREC_ADD; break;
    case 0x93: case 0xA4: *symbol = "-"; *precedence = PREC_ADD; break;
    case 0x94: case 0xA5: *symbol = "*"; *precedence = PREC_MULTIPLY; break;
    case 0x95: case 0xA6: *symbol = "/"; *precedence = PREC_MULTIPLY; break;
    case 0x96: *symbol = "%"; *precedence = PREC_MULTIPLY; break;
    case 0x97: *symbol = "<<"; *precedence = PREC_SHIFT; break;
    default:   *symbol = ">>"; *precedence = PREC_SHIFT; break;
    }
}

// x OP y, with the addresses of the variables plus an offset or an index kept as such, to write arrays
static Value Binary(Decompiler* d, uint8_t byte) {
    Value y = Pop(d), x = Pop(d);
    const char* symbol;
    uint8_t precedence;
    BinaryPrecedence(byte, &symbol, &precedence);
    char* left = Operand(&x, precedence);
    char* right = Operand(&y, (uint8_t)(precedence + 1));
    Value value = MakeValue(precedence, x.effect || y.effect, Format("%s %s %s", left, symbol, right));
    free(left);
    free(right);
    if (byte == 0x92 && x.address == ADDRESS_VARIABLE && y.constant) {
        value.address = ADDRESS_OFFSET;
        value.offset = y.value;
    } else if (byte == 0x92 && x.address == ADDRESS_OFFSET && y.constant) {
        value.address = ADDRESS_OFFSET;
        value.offset = x.offset + y.value;
    } else if (byte == 0x92 && x.address == ADDRESS_VARIABLE && !y.constant) {
        value.address = ADDRESS_INDEX;
        value.index = Format("%s", y.index != NULL ? y.index : y.text);
        value.shift = y.index != NULL ? y.shift : 0;
    } else if (byte == 0x97 && y.constant && y.value >= 0 && y.value < 4) {
        value.index = Format("%s", x.text);
        value.shift = (uint8_t)y.value;
    }
    if (value.address != ADDRESS_NONE)
        memcpy(value.variable, x.variable, sizeof(value.variable));
    if (byte == 0x8F && x.precedence == PREC_ASSIGN && y.constant)
        value.assignment = Format("%s", x.text);
    if (byte == 0x92 && x.constant && y.constant) {
        FreeValue(&value);
        value = Constant(x.value + y.value);
    }
    FreeValue(&x);
    FreeValue(&y);
    return value;
}

static void AddStatement(Decompiler* d, uint8_t kind, uint32_t target, char* text, uint32_t next) {
    if (d->statementCount == d->statementCapacity) {
        uint32_t capacity = d->statementCapacity ? d->statementCapacity * 2 : 64;
        Statement* statements = realloc(d->statements, capacity * sizeof(Statement));
        if (statements == NULL) {
            free(text);
            d->failed = true;
            return;
        }
        d->statements = statements;
        d->statementCapacity = capacity;
    }
    Statement* statement = &d->statements[d->statementCount++];
    statement->address = d->statementStart;
    statement->next = next;
    statement->kind = kind;
    statement->target = target;
    statement->text = text;
    d->statementStart = next;
}

// The values left on the stack become statements: those with side effects are kept
static void FlushStack(Decompiler* d, uint32_t next, bool error) {
    for (uint32_t i = 0; i < d->depth; i++) {
        if (error)
            d->out->stackErrors++;
        if (d->stack[i].effect || error)
            AddStatement(d, STATEMENT_TEXT, 0, Format(error ? "/* pushed */ %s;" : "%s;", d->stack[i].text), next);
        FreeValue(&d->stack[i]);
    }
    d->depth = 0;
}

static bool Decode(Decompiler* d, uint32_t pc, BC_Instruction* ins) {
    return pc < d->end && BC_Decode(d->program->scn->data, d->program->codeEnd, pc, ins);
}

// The next instruction, if no branch goes to it: it can be part of the same statement
static bool DecodeNext(Decompiler* d, uint32_t pc, BC_Instruction* ins) {
    return pc < d->end && !(d->program->starts[pc] & 2) && Decode(d, pc, ins);
}

static char* CallName(Decompiler* d, const BC_Instruction* ins) {
    const char* name;
    switch (ins->kind) {
    case BC_CALL:
        return Format("func_%X", ins->target);
    case BC_GAME:
        name = SH4_GameName(ins->operand);
        return name != NULL ? Format("%s", name) : Format("game_%03X", (unsigned)ins->operand);
    case BC_SERVICE2C:
        return Format("service2C_%X", (unsigned)ins->operand);
    case BC_SERVICE30:
        return Format("service30_%X", (unsigned)ins->operand);
    case BC_NATIVE:
        return Format("native_%d", ins->operand);
    default:
        return Format("native4_%d", ins->operand);
    }
}

// A call: its arguments are the values that the next instruction drops (sp += 4 × count), the first one at the top. If
// the instruction after it pushes the result, the call is a value; otherwise, a statement. Returns the address after
// the instructions used
static uint32_t Call(Decompiler* d, const BC_Instruction* ins) {
    uint32_t pc = ins->address + ins->size;
    BC_Instruction next;
    uint32_t count = 0;
    if (DecodeNext(d, pc, &next) && next.kind == BC_STACK && next.operand > 0 && next.operand % 4 == 0) {
        count = (uint32_t)next.operand / 4;
        pc += next.size;
    }
    char* name = CallName(d, ins);
    char arguments[TEXT_LIMIT] = "";
    size_t length = 0;
    bool effect = true;
    for (uint32_t i = 0; i < count; i++) {
        Value argument = Pop(d);
        length += (size_t)snprintf(arguments + length, length < sizeof(arguments) ? sizeof(arguments) - length : 0,
            i > 0 ? ", %s" : "%s", argument.text);
        FreeValue(&argument);
        if (length >= sizeof(arguments))
            length = sizeof(arguments) - 1;
    }
    Value call = MakeValue(PREC_PRIMARY, effect, Format("%s(%s)", name, arguments));
    free(name);
    d->argumentCount = -1;
    if (DecodeNext(d, pc, &next) && next.kind == BC_OPERATOR && next.byte == 0x81) {
        Push(d, call);
        return pc + next.size;
    }
    // A statement, after the values pushed before the arguments that have side effects
    FlushStack(d, pc, false);
    AddStatement(d, STATEMENT_TEXT, 0, Format("%s;", call.text), pc);
    FreeValue(&call);
    return pc;
}

// A branch to a return
static bool IsReturnBranch(Decompiler* d, const BC_Instruction* ins) {
    BC_Instruction target;
    return (ins->kind == BC_BRANCH || ins->kind == BC_JUMP) && Decode(d, ins->target, &target) && target.kind == BC_RET;
}

// The value computed by the instructions from start to end, if they only push values (constants, variables, operators,
// calls whose result is used) and leave one more on the stack, without labels inside. Returns false otherwise
static bool ComputeValue(Decompiler* d, uint32_t start, uint32_t end, Value* out) {
    uint32_t base = d->depth;
    bool ok = start < end;
    for (uint32_t pc = start; pc < end && ok;) {
        BC_Instruction ins;
        if (!Decode(d, pc, &ins) || (pc != start && (d->program->starts[pc] & 2))) {
            ok = false;
            break;
        }
        uint32_t after = pc + ins.size;
        if (ins.kind == BC_PUSH && ins.area == BC_AREA_CONSTANT)
            Push(d, Constant(ins.operand));
        else if (ins.kind == BC_PUSH && ins.area == BC_AREA_PROGRAM && ins.target < d->program->codeEnd)
            Push(d, MakeValue(PREC_PRIMARY, false, Format("func_%X", ins.target)));
        else if (ins.kind == BC_PUSH && ins.area == BC_AREA_PROGRAM)
            Push(d, DataAddress(d, ins.target - d->program->scn->header.dataStart));
        else if (ins.kind == BC_PUSH && ins.width == 0)
            Push(d, Address(d, ins.area, ins.operand));
        else if (ins.kind == BC_PUSH) {
            Value address = Address(d, ins.area, ins.operand);
            Push(d, MakeValue(PREC_PRIMARY, false, Access(&address, ins.width)));
            FreeValue(&address);
        } else if (ins.kind == BC_OPERATOR && ins.byte >= 0x82 && ins.byte <= 0x84)
            Push(d, Load(d, (uint8_t)(1 << (ins.byte - 0x82))));
        else if (ins.kind == BC_OPERATOR && ins.byte >= 0x85 && ins.byte <= 0x87)
            Push(d, Store(d, (uint8_t)(1 << (ins.byte - 0x85))));
        else if (ins.kind == BC_OPERATOR && ins.byte >= 0x88 && ins.byte <= 0xA6 && ins.byte != 0x8E && ins.byte != 0x9D &&
            ins.byte != 0x9E && (ins.byte < 0x99 || ins.byte > 0x9C) && d->depth >= base + 2)
            Push(d, Binary(d, ins.byte));
        else
            ok = false;
        pc = after;
        ok &= d->depth >= base;
    }
    if (ok && d->depth == base + 1) {
        *out = Pop(d);
        return true;
    }
    while (d->depth > base) {
        Value value = Pop(d);
        FreeValue(&value);
    }
    return false;
}

// condition ? a : b: the instructions after the branch push a and jump over those that push b, from the target of the
// branch to the join. Pushes the value and gives the address of the join. Returns false if they aren't that
static bool Conditional(Decompiler* d, const Value* condition, uint32_t start, uint32_t target, uint32_t* join) {
    // The jump at the end of the first value
    BC_Instruction ins, last = { 0 };
    bool found = false;
    for (uint32_t pc = start; pc < target && Decode(d, pc, &ins); pc += ins.size) {
        last = ins;
        found = pc + ins.size == target;
    }
    if (!found || last.kind != BC_BRANCH || last.target <= target || last.target > d->end)
        return false;
    Value a, b;
    if (!ComputeValue(d, start, last.address, &a))
        return false;
    if (!ComputeValue(d, target, last.target, &b)) {
        FreeValue(&a);
        return false;
    }
    char* c = Operand(condition, PREC_TERNARY + 1);
    char* x = Operand(&a, PREC_TERNARY + 1);
    char* y = Operand(&b, PREC_TERNARY);
    Push(d, MakeValue(PREC_TERNARY, condition->effect || a.effect || b.effect, Format("%s ? %s : %s", c, x, y)));
    free(c);
    free(x);
    free(y);
    FreeValue(&a);
    FreeValue(&b);
    *join = last.target;
    return true;
}

// The statements of the function, with their branches
static void BuildStatements(Decompiler* d) {
    const uint8_t* starts = d->program->starts;
    d->statementStart = d->start;
    for (uint32_t pc = d->start; pc < d->end && !d->failed;) {
        BC_Instruction ins, next;
        if (!Decode(d, pc, &ins)) {
            FlushStack(d, pc + 1, true);
            AddStatement(d, STATEMENT_TEXT, 0, Format("/* byte 0x%02X */", d->program->scn->data[pc]), pc + 1);
            pc++;
            continue;
        }
        // A label: another path can reach it, with an empty stack
        if (pc != d->start && (starts[pc] & 2) && pc != d->joinLabel) {
            FlushStack(d, pc, d->depth > 0);
            d->statementStart = pc;
        }
        uint32_t after = pc + ins.size;
        switch (ins.kind) {
        case BC_PUSH:
            if (ins.area == BC_AREA_CONSTANT)
                Push(d, Constant(ins.operand));
            else if (ins.area == BC_AREA_PROGRAM && ins.target < d->program->codeEnd)
                Push(d, MakeValue(PREC_PRIMARY, false, Format("func_%X", ins.target)));
            else if (ins.area == BC_AREA_PROGRAM)
                Push(d, DataAddress(d, ins.target - d->program->scn->header.dataStart));
            else if (ins.width == 0)
                Push(d, Address(d, ins.area, ins.operand));
            else {
                Value address = Address(d, ins.area, ins.operand);
                Value value = MakeValue(PREC_PRIMARY, false, Access(&address, ins.width));
                FreeValue(&address);
                Push(d, value);
            }
            break;
        case BC_OPERATOR:
            if (ins.byte == 0x80) {
                Value value = Pop(d);
                // A conditional expression (c ? a : b), a condition, a returned value or a statement
                uint32_t join;
                if (DecodeNext(d, after, &next) && next.kind == BC_BRANCH_ZERO &&
                    Conditional(d, &value, after + next.size, next.target, &join)) {
                    FreeValue(&value);
                    after = join;
                    d->joinLabel = join;
                    pc = after;
                    continue;
                }
                if (DecodeNext(d, after, &next) && next.kind == BC_BRANCH_ZERO) {
                    FlushStack(d, after, false);
                    AddStatement(d, STATEMENT_BRANCH_ZERO, next.target, Format("%s", value.text), after + next.size);
                    after += next.size;
                } else if (DecodeNext(d, after, &next) && (next.kind == BC_RET || IsReturnBranch(d, &next))) {
                    // The result, then a return or a branch to one
                    FlushStack(d, after, false);
                    AddStatement(d, STATEMENT_RETURN, 0, Format("%s", value.text), after + next.size);
                    after += next.size;
                } else {
                    FlushStack(d, after, false);
                    AddStatement(d, STATEMENT_TEXT, 0, Format("%s;", value.assignment != NULL ? value.assignment :
                        value.text), after);
                }
                FreeValue(&value);
            } else if (ins.byte == 0x81)
                Push(d, MakeValue(PREC_PRIMARY, false, Format("result")));
            else if (ins.byte >= 0x82 && ins.byte <= 0x84)
                Push(d, Load(d, (uint8_t)(1 << (ins.byte - 0x82))));
            else if (ins.byte >= 0x85 && ins.byte <= 0x87)
                Push(d, Store(d, (uint8_t)(1 << (ins.byte - 0x85))));
            else if (ins.byte == 0x8E || ins.byte == 0x9D || ins.byte == 0x9E) {
                Value x = Pop(d);
                char* operand = Operand(&x, PREC_UNARY);
                Value value = MakeValue(PREC_UNARY, x.effect, Format(ins.byte == 0x8E ? "~%s" : ins.byte == 0x9D ?
                    "(float)%s" : "(int)%s", operand));
                free(operand);
                FreeValue(&x);
                Push(d, value);
            } else if (ins.byte >= 0x99 && ins.byte <= 0x9C) {
                // Nothing
            } else
                Push(d, Binary(d, ins.byte));
            break;
        case BC_STACK:
            if (ins.operand > 0) {
                for (int32_t i = 0; i < ins.operand / 4; i++) {
                    Value value = Pop(d);
                    if (value.effect)
                        AddStatement(d, STATEMENT_TEXT, 0, Format("%s;", value.text), after);
                    FreeValue(&value);
                }
            } else if (pc != d->start) {
                for (int32_t i = 0; i < -ins.operand / 4; i++)
                    Push(d, MakeValue(PREC_PRIMARY, false, Format("?")));
            }
            if (d->depth == 0)
                d->statementStart = after;
            break;
        case BC_CALL:
        case BC_GAME:
        case BC_SERVICE2C:
        case BC_SERVICE30:
        case BC_NATIVE:
        case BC_NATIVE4:
            after = Call(d, &ins);
            break;
        case BC_CALL_POP: {
            Value function = Pop(d);
            FlushStack(d, after, false);
            AddStatement(d, STATEMENT_TEXT, 0, Format("(*%s)();", function.text), after);
            FreeValue(&function);
            break;
        }
        case BC_DATA_SERVICE: {
            FlushStack(d, after, false);
            Value data = DataAddress(d, ins.target - d->program->scn->header.dataStart);
            AddStatement(d, STATEMENT_TEXT, 0, Format("service2C_1(%s);", data.text), after);
            FreeValue(&data);
            break;
        }
        case BC_ARGUMENT_COUNT:
            d->argumentCount = ins.operand;
            break;
        case BC_RET:
            FlushStack(d, after, false);
            AddStatement(d, STATEMENT_RETURN, 0, NULL, after);
            break;
        case BC_JUMP:
        case BC_BRANCH:
            FlushStack(d, after, false);
            AddStatement(d, STATEMENT_BRANCH, ins.target, NULL, after);
            break;
        case BC_BRANCH_ZERO:
            FlushStack(d, after, false);
            AddStatement(d, STATEMENT_BRANCH_ZERO, ins.target, Format("result"), after);
            break;
        default:
            break;
        }
        pc = after;
    }
    FlushStack(d, d->end, d->depth > 0);
}

static void AddLine(Decompiler* d, uint32_t address, int indent, char* text) {
    DC_Function* out = d->out;
    if (text == NULL) {
        d->failed = true;
        return;
    }
    if (out->lineCount == d->lineCapacity) {
        uint32_t capacity = d->lineCapacity ? d->lineCapacity * 2 : 64;
        DC_Line* lines = realloc(out->lines, capacity * sizeof(DC_Line));
        if (lines == NULL) {
            free(text);
            d->failed = true;
            return;
        }
        out->lines = lines;
        d->lineCapacity = capacity;
    }
    out->lines[out->lineCount++] = (DC_Line) { address, (uint16_t)indent, text };
}

// Index of the statement at an address, or count for the end of the function; -1 if none
static int StatementAt(const Decompiler* d, uint32_t address) {
    if (address >= d->end)
        return address == d->end ? (int)d->statementCount : -1;
    uint32_t low = 0, high = d->statementCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (d->statements[middle].address < address)
            low = middle + 1;
        else
            high = middle;
    }
    return low < d->statementCount && d->statements[low].address == address ? (int)low : -1;
}

static uint32_t AddressOf(const Decompiler* d, int index) {
    return (uint32_t)index < d->statementCount ? d->statements[index].address : d->end;
}

typedef struct {
    uint32_t    head;   // Address of the start of the loop, 0 if none
    uint32_t    exit;   // Address after the loop
} Loop;

static void Emit(Decompiler* d, int from, int to, int indent, Loop loop, int skipLoop);

// A branch out of the structure: break, continue or goto
static char* Jump(Decompiler* d, uint32_t target, Loop loop, const char* condition) {
    const char* jump = target == loop.exit && loop.head != 0 ? "break" : target == loop.head && loop.head != 0 ?
        "continue" : NULL;
    if (jump == NULL)
        d->out->gotoCount++;
    char go[32];
    snprintf(go, sizeof(go), "goto L_%X", target);
    if (condition != NULL)
        return Format("if (!(%s)) %s;", condition, jump != NULL ? jump : go);
    return Format("%s;", jump != NULL ? jump : go);
}

static void Emit(Decompiler* d, int from, int to, int indent, Loop loop, int skipLoop) {
    for (int i = from; i < to && !d->failed;) {
        const Statement* s = &d->statements[i];
        // A loop: a later branch back to this statement, within the range
        int back = -1;
        for (int j = i; j < to && i != skipLoop; j++) {
            if ((d->statements[j].kind == STATEMENT_BRANCH || d->statements[j].kind == STATEMENT_BRANCH_ZERO) &&
                d->statements[j].target == s->address)
                back = j;
        }
        if (back >= 0 && d->statements[back].kind == STATEMENT_BRANCH_ZERO) {
            // do { ... } while (condition): the conditional branch back runs the loop again while the condition is 0
            Loop inner = { s->address, d->statements[back].next };
            AddLine(d, s->address, indent, Format("do {"));
            Emit(d, i, back, indent + 1, inner, i);
            AddLine(d, d->statements[back].address, indent, Format("} while (!(%s));", d->statements[back].text));
            i = back + 1;
            continue;
        }
        if (back >= 0) {
            Loop inner = { s->address, d->statements[back].next };
            if (s->kind == STATEMENT_BRANCH_ZERO && s->target == inner.exit) {
                AddLine(d, s->address, indent, Format("while (%s) {", s->text));
                Emit(d, i + 1, back, indent + 1, inner, -1);
            } else {
                AddLine(d, s->address, indent, Format("for (;;) {"));
                Emit(d, i, back, indent + 1, inner, i);
            }
            AddLine(d, d->statements[back].address, indent, Format("}"));
            i = back + 1;
            continue;
        }
        switch (s->kind) {
        case STATEMENT_TEXT:
            AddLine(d, s->address, indent, Format("%s", s->text));
            i++;
            break;
        case STATEMENT_RETURN:
            AddLine(d, s->address, indent, s->text != NULL ? Format("return %s;", s->text) : Format("return;"));
            i++;
            break;
        case STATEMENT_BRANCH:
            // A branch to the next statement does nothing
            if (s->target != AddressOf(d, i + 1))
                AddLine(d, s->address, indent, Jump(d, s->target, loop, NULL));
            i++;
            break;
        case STATEMENT_BRANCH_ZERO: {
            int k = StatementAt(d, s->target);
            if ((loop.head != 0 && (s->target == loop.exit || s->target == loop.head)) || k <= i || k > to) {
                AddLine(d, s->address, indent, Jump(d, s->target, loop, s->text));
                i++;
                break;
            }
            // if (condition) { ... } else { ... }: the then part ends with a branch over the else part
            const Statement* last = k - 1 > i ? &d->statements[k - 1] : NULL;
            int m = last != NULL && last->kind == STATEMENT_BRANCH && last->target > s->target ?
                StatementAt(d, last->target) : -1;
            bool jumpOut = last != NULL && loop.head != 0 && (last->target == loop.exit || last->target == loop.head);
            AddLine(d, s->address, indent, Format("if (%s) {", s->text));
            if (m > k && m <= to && !jumpOut) {
                Emit(d, i + 1, k - 1, indent + 1, loop, -1);
                AddLine(d, last->address, indent, Format("} else {"));
                Emit(d, k, m, indent + 1, loop, -1);
                AddLine(d, AddressOf(d, m), indent, Format("}"));
                i = m;
            } else {
                Emit(d, i + 1, k, indent + 1, loop, -1);
                AddLine(d, AddressOf(d, k), indent, Format("}"));
                i = k;
            }
            break;
        }
        default:
            i++;
            break;
        }
    }
}

// The labels of the gotos, before the first line of their address
static void AddLabels(Decompiler* d) {
    DC_Function* out = d->out;
    for (uint32_t l = 0; l < out->lineCount && !d->failed; l++) {
        const char* text = out->lines[l].text;
        const char* go = strstr(text, "goto L_");
        if (go == NULL)
            continue;
        uint32_t target = (uint32_t)strtoul(go + 7, NULL, 16);
        char label[32];
        snprintf(label, sizeof(label), "L_%X:", target);
        uint32_t at = out->lineCount;
        bool exists = false;
        for (uint32_t i = 0; i < out->lineCount; i++) {
            exists |= strcmp(out->lines[i].text, label) == 0;
            if (at == out->lineCount && out->lines[i].address >= target && i > 0)
                at = i;
        }
        if (exists)
            continue;
        AddLine(d, target, 0, Format("%s", label));
        if (d->failed)
            return;
        DC_Line line = out->lines[out->lineCount - 1];
        memmove(out->lines + at + 1, out->lines + at, (out->lineCount - 1 - at) * sizeof(DC_Line));
        out->lines[at] = line;
        if (at <= l)
            l++;
    }
}

bool DC_DecompileFunction(const BC_Program* program, uint32_t function, DC_Function* out) {
    memset(out, 0, sizeof(*out));
    if (function >= program->functionCount)
        return false;
    Decompiler* d = calloc(1, sizeof(Decompiler));
    if (d == NULL)
        return false;
    d->program = program;
    d->start = program->functions[function];
    d->end = BC_FunctionEnd(program, function);
    d->out = out;
    d->argumentCount = -1;
    d->maxArgument = -1;
    BuildStatements(d);
    // The header, with the arguments used
    char arguments[512] = "";
    size_t length = 0;
    for (int32_t a = 0; a <= d->maxArgument && length < sizeof(arguments); a++)
        length += (size_t)snprintf(arguments + length, sizeof(arguments) - length, a > 0 ? ", arg%d" : "arg%d", a);
    AddLine(d, d->start, 0, Format("func_%X(%s) {", d->start, arguments));
    Loop none = { 0, 0 };
    Emit(d, 0, (int)d->statementCount, 1, none, -1);
    AddLine(d, d->end, 0, Format("}"));
    AddLabels(d);
    for (uint32_t i = 0; i < d->statementCount; i++)
        free(d->statements[i].text);
    free(d->statements);
    for (uint32_t i = 0; i < d->depth; i++)
        FreeValue(&d->stack[i]);
    bool ok = !d->failed;
    free(d);
    if (!ok)
        DC_FreeFunction(out);
    return ok;
}

void DC_FreeFunction(DC_Function* function) {
    for (uint32_t i = 0; i < function->lineCount; i++)
        free(function->lines[i].text);
    free(function->lines);
    memset(function, 0, sizeof(*function));
}

void DC_WriteProgram(const BC_Program* program, FILE* file) {
    for (uint32_t f = 0; f < program->functionCount; f++) {
        DC_Function function;
        if (!DC_DecompileFunction(program, f, &function))
            continue;
        fputc('\n', file);
        for (uint32_t i = 0; i < function.lineCount; i++)
            fprintf(file, "%*s%s\n", function.lines[i].indent * 4, "", function.lines[i].text);
        DC_FreeFunction(&function);
    }
}

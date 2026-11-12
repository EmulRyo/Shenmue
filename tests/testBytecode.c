#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "corpus.h"
#include "formats/script/scn.h"
#include "formats/script/scnBytecode.h"
#include "test.h"

// The bytecode of the scene programs: the encodings of the instructions, and the bytecode of the 136 SCN files

typedef struct {
    uint8_t     bytes[6];
    uint8_t     size;       // Of the instruction, 0 if it isn't one
    uint32_t    address;
    const char* text;
} Encoding;

static const Encoding s_encodings[] = {
    // act01_mk99.scn
    { { 0x13, 0x00 }, 2, 0x30, "sp += 0" },
    { { 0x70, 0x41, 0x4B, 0x49, 0x52 }, 5, 0x32, "push 0x52494B41" },
    { { 0x50, 0x01 }, 2, 0x37, "push 1" },
    { { 0x73, 0x43, 0x00, 0x00, 0x00 }, 5, 0x39, "push &0x43" },
    { { 0x1B, 0x02 }, 2, 0x3E, "game 0x2" },
    { { 0x13, 0x0C }, 2, 0x40, "sp += 12" },
    { { 0x00 }, 1, 0x42, "ret" },
    { { 0x5E, 0x08 }, 2, 0x47, "push.l [fp+8]" },
    // Operands of 2 and 4 bytes, signed
    { { 0x23, 0xF0, 0xFF }, 3, 0, "sp -= 16" },
    { { 0x2B, 0x3E, 0x01 }, 3, 0, "game 0x13E" },
    { { 0x31, 0x00, 0x01, 0x00, 0x00 }, 5, 0, "call 0x100" },
    // Branches, relative to the end of the instruction
    { { 0x15, 0xFE }, 2, 0x100, "bra 0x100" },
    { { 0x26, 0x10, 0x00 }, 3, 0x100, "bz 0x113" },
    { { 0x34, 0x00, 0x02, 0x00, 0x00 }, 5, 0, "jmp 0x200" },
    // Pushes of variables
    { { 0x5C, 0xF8 }, 2, 0, "push.b [fp-8]" },
    { { 0x5F, 0xFC }, 2, 0, "push &fp-4" },
    { { 0x7B, 0x78, 0x00, 0x00, 0x00 }, 5, 0, "push &var+0x78" },
    { { 0x69, 0x10, 0x00 }, 3, 0, "push.w [var+0x10]" },
    { { 0x56, 0x04 }, 2, 0, "push.l [global+0x4]" },
    // Other control instructions
    { { 0x17, 0x03 }, 2, 0, "args 3" },
    { { 0x1C, 0x05 }, 2, 0, "service 0x5" },
    { { 0x1D, 0x02 }, 2, 0, "service30 0x2" },
    { { 0x38, 0x00, 0x10, 0x00, 0x00 }, 5, 0, "service 1 &0x1000" },
    { { 0x02 }, 1, 0, "call pop" },
    // Operators
    { { 0x80 }, 1, 0, "pop result" },
    { { 0x81 }, 1, 0, "push result" },
    { { 0x87 }, 1, 0, "store.l" },
    { { 0x8A }, 1, 0, "<" },
    { { 0x92 }, 1, 0, "+" },
    { { 0x9D }, 1, 0, "itof" },
    { { 0xA6 }, 1, 0, "f/" },
    // Not instructions
    { { 0xA7 }, 0, 0, NULL },
    { { 0xC0 }, 0, 0, NULL },
    { { 0xFF }, 0, 0, NULL },
    { { 0x0E }, 0, 0, NULL },
    { { 0x1B }, 0, 0, NULL },   // Its operand is missing
};

void Test_BytecodeEncodings(const TestContext* context) {
    (void)context;
    for (size_t i = 0; i < sizeof(s_encodings) / sizeof(s_encodings[0]); i++) {
        const Encoding* e = &s_encodings[i];
        // The bytes at their address of a buffer, followed by nothing (to check the operands that don't fit)
        uint32_t size = e->address + (e->size > 0 ? e->size : 1);
        uint8_t* data = calloc(size, 1);
        if (data == NULL)
            return;
        memcpy(data + e->address, e->bytes, e->size > 0 ? e->size : 1);
        BC_Instruction ins;
        bool valid = BC_Decode(data, size, e->address, &ins);
        CHECK(valid == (e->size > 0), "0x%02X: valid %d", e->bytes[0], valid);
        if (valid && e->text != NULL) {
            char text[64];
            BC_Format(&ins, text, sizeof(text));
            CHECK(strcmp(text, e->text) == 0, "0x%02X: \"%s\", expected \"%s\"", e->bytes[0], text, e->text);
            CHECK(ins.size == e->size, "0x%02X: size %u, expected %u", e->bytes[0], ins.size, e->size);
        }
        free(data);
    }
}

// Calls that the listing of these files show, as those of the SH-4 programs of the same maps
typedef struct {
    const char* file;
    const char* comment;
    bool        found;
} ExpectedCall;

void Test_BytecodeFiles(const TestContext* context) {
    ExpectedCall expected[] = {
        { "scenario/act01_mk99.scn", "game 0x2(67, 1, 'AKIR')" },
        { "scenario/act01_d000.scn", "game 0x13E(0, 4, \"seqdata1.bin\", \"D0W0\")" },
    };
    const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);
    CorpusList list = Corpus_ScnFiles(context->content);
    uint64_t instructions = 0, invalid = 0, functions = 0, badTargets = 0, gameCalls = 0, badIds = 0;
    uint64_t knownArguments = 0, arguments = 0, codeAddresses = 0, dataAddresses = 0, notAfterReturn = 0;
    uint32_t programs = 0, padded = 0, badEntry = 0, badRelocations = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program scn;
        BC_Program program;
        if (data == NULL || !SCN_Read(data, size, &scn) || !BC_AnalyzeProgram(&program, &scn)) {
            CHECK(false, "%s: can't be analyzed", name);
            free(data);
            continue;
        }
        programs++;
        instructions += program.instructionCount;
        invalid += program.invalidCount;
        functions += program.functionCount;
        padded += program.codeEnd < scn.header.dataStart;
        badEntry += BC_FunctionAt(&program, scn.header.entry) < 0 ||
            program.functions[BC_FunctionAt(&program, scn.header.entry)] != scn.header.entry;
        for (uint32_t r = 0; r < scn.relocationCount; r++) {
            SCN_Relocation relocation = SCN_GetRelocation(&scn, r);
            if (relocation.type != SCN_RELOCATION_CODE)
                continue;
            uint32_t value = Bytes_ReadU32(scn.data + relocation.offset);
            int function = BC_FunctionAt(&program, value);
            badRelocations += function < 0 || program.functions[function] != value;
        }
        uint8_t previous = BC_INVALID;  // Kind of the previous instruction
        uint32_t function = 1;
        for (uint32_t pc = scn.header.codeStart; pc < program.codeEnd;) {
            BC_Instruction ins;
            if (!BC_Decode(scn.data, program.codeEnd, pc, &ins)) {
                pc++;
                continue;
            }
            // Each function starts after the return of the previous one
            if (function < program.functionCount && program.functions[function] == pc) {
                notAfterReturn += previous != BC_RET;
                function++;
            }
            previous = ins.kind;
            // The addresses of the program pushed as values are functions, or variables and strings of the data
            bool address = ins.kind == BC_PUSH && ins.area == BC_AREA_PROGRAM;
            bool jumps = ins.kind == BC_CALL || ins.kind == BC_JUMP || ins.kind == BC_BRANCH ||
                ins.kind == BC_BRANCH_ZERO || (address && ins.target < scn.header.dataStart);
            if (address && ins.target >= scn.header.dataStart) {
                CHECK(ins.target < scn.header.end, "%s: 0x%X: address 0x%X after the data", name, pc, ins.target);
                dataAddresses++;
            } else if (address) {
                codeAddresses++;
                // A function: the start of one
                int function = BC_FunctionAt(&program, ins.target);
                CHECK(function >= 0 && program.functions[function] == ins.target, "%s: 0x%X: address 0x%X", name, pc,
                    ins.target);
            }
            if (jumps) {
                BC_Instruction target;
                if (ins.target >= program.codeEnd || !BC_Decode(scn.data, program.codeEnd, ins.target, &target) ||
                    (ins.target != pc && BC_FunctionAt(&program, ins.target) < 0)) {
                    if (badTargets < 10)
                        CHECK(false, "%s: 0x%X: target 0x%X", name, pc, ins.target);
                    badTargets++;
                }
            }
            if (ins.kind == BC_DATA_SERVICE)
                CHECK(ins.target >= scn.header.dataStart && ins.target < scn.header.end, "%s: 0x%X: data 0x%X",
                    name, pc, ins.target);
            pc += ins.size;
        }
        for (uint32_t c = 0; c < program.callCount; c++) {
            const SH4_Call* call = &program.calls[c];
            if (call->kind == SH4_CALL_GAME) {
                gameCalls++;
                // The table has no function for 0x76, 0x77 and 0x125
                badIds += call->id < 0 || call->id > 0x1CC || call->id == 0x76 || call->id == 0x77 || call->id == 0x125;
            }
            for (uint32_t a = 0; a < call->argumentCount; a++) {
                arguments++;
                knownArguments += call->arguments[a].kind != SH4_VALUE_UNKNOWN;
            }
            for (size_t e = 0; e < expectedCount; e++) {
                if (strcmp(name, expected[e].file) != 0)
                    continue;
                char text[256];
                BC_DescribeCall(&program, call, text, sizeof(text));
                expected[e].found |= strcmp(text, expected[e].comment) == 0;
            }
        }
        BC_FreeProgram(&program);
        free(data);
    }
    CHECK_EQ(programs, 136, "analyzed programs");
    CHECK_EQ(instructions, 4349410, "instructions");
    CHECK_EQ(invalid, 0, "bytes that aren't instructions");
    CHECK_EQ(padded, 112, "programs with 0xFF bytes after their code");
    CHECK_EQ(badEntry, 0, "entry points that aren't the start of a function");
    CHECK_EQ(badRelocations, 0, "code relocations that aren't the start of a function");
    CHECK_EQ(badTargets, 0, "calls, jumps and branches that don't go to an instruction");
    CHECK_EQ(notAfterReturn, 0, "functions that don't start after a return");
    CHECK_EQ(functions, 24400, "functions");
    CHECK_EQ(badIds, 0, "calls to an ID without a function of the game");
    for (size_t e = 0; e < expectedCount; e++)
        CHECK(expected[e].found, "%s: no call %s", expected[e].file, expected[e].comment);
    Test_Info("%llu addresses of functions and %llu of the data pushed as values", (unsigned long long)codeAddresses,
        (unsigned long long)dataAddresses);
    Test_Info("%u programs: %llu instructions, %llu functions, %llu calls to the game, %llu arguments (%.1f%% known)",
        programs, (unsigned long long)instructions, (unsigned long long)functions, (unsigned long long)gameCalls,
        (unsigned long long)arguments, arguments ? 100.0 * knownArguments / (double)arguments : 0.0);
    Corpus_Free(&list);
}

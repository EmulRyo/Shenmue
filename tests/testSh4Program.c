#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/sh4Program.h"
#include "test.h"

// The analysis of the SH-4 programs: functions, blocks, calls and literals

#define MAX_ID 0x200

static int FindBlock(const SH4_Program* program, uint32_t address) {
    uint32_t low = 0, high = program->blockCount;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        if (program->blocks[middle].start < address)
            low = middle + 1;
        else
            high = middle;
    }
    return low < program->blockCount && program->blocks[low].start == address ? (int)low : -1;
}

// String of the data at an offset, if a whole string starts there
static const char* DataString(const SCN_Program* scn, uint32_t offset) {
    const SCN_Header* header = &scn->header;
    if (offset >= header->end - header->dataStart)
        return NULL;
    const char* text = (const char*)scn->data + header->dataStart + offset;
    if (offset > 0 && text[-1] != '\0')
        return NULL;
    return text;
}

// The calls of scene/01/d000 that pass these strings, from the wiki (the files of its cutscenes)
static const char* const s_d000Strings[] = { "seqdata1.bin", "D0W0", "a1_yamaw.snd", "E1020", "/scene/01/D000/" };

#define START_CODE_SIZE 0x44    // The code at codeStart: resume (0x30) and yield (0x64), the same in every program
#define MAX_SUBCODES    8192

// Number of arguments of the calls of an ID whose last pushed argument is a given constant
typedef struct {
    int32_t     id;
    uint32_t    last;
    int32_t     bytes;
    bool        conflict;
} Subcode;

// Number of data references to a record of the door table: a word of the data equal to the offset of the name of a
// door model
static uint32_t DoorTableReferences(const SH4_Program* program, const SCN_Program* scn, const MAPINFO_File* file) {
    uint32_t references = 0;
    for (uint32_t l = 0; l < program->literalCount; l++) {
        const SH4_Literal* literal = &program->literals[l];
        uint32_t at = scn->header.dataStart + literal->value;
        if (literal->kind != SH4_LITERAL_DATA_OFFSET || at + 4 > scn->header.end)
            continue;
        uint32_t word = Bytes_ReadU32(scn->data + at);
        for (uint32_t d = 0; d < file->doorCount; d++) {
            if (word == (uint32_t)((const uint8_t*)file->doors[d].model - scn->data)) {
                references++;
                break;
            }
        }
    }
    return references;
}

void Test_Sh4Program(const TestContext* context) {
    CorpusList list = Corpus_MapinfoFiles(context->content);
    uint32_t programs = 0;
    uint64_t functions = 0, blocks = 0, blockWords = 0, instructions = 0, badTargets = 0, badLocal = 0;
    uint64_t calls[SH4_CALL_UNKNOWN + 1] = { 0 }, unknownIds = 0, noPop = 0, arguments = 0, knownArguments = 0;
    uint64_t literalKinds[4] = { 0 }, badData = 0, contextWrites = 0, uncalled = 0;
    uint32_t* idCalls = calloc(MAX_ID, sizeof(uint32_t));
    int32_t* idBytes = malloc(MAX_ID * sizeof(int32_t));
    uint32_t* idConflicts = calloc(MAX_ID, sizeof(uint32_t));
    bool d000Found[sizeof(s_d000Strings) / sizeof(s_d000Strings[0])] = { false };
    Subcode* subcodes = calloc(MAX_SUBCODES, sizeof(Subcode));
    uint32_t subcodeCount = 0, doorFiles = 0, doorReferenced = 0, sameStart = 0;
    uint8_t startCode[START_CODE_SIZE];
    bool haveStart = false;
    if (idCalls == NULL || idBytes == NULL || idConflicts == NULL || subcodes == NULL)
        return;
    for (int i = 0; i < MAX_ID; i++)
        idBytes[i] = -1;
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        MAPINFO_File file;
        if (!MAPINFO_Load(list.files[i].path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program scn;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &scn) || !SCN_IsSH4(&scn)) {
            MAPINFO_Free(&file);
            continue;
        }
        const SCN_Header* header = &scn.header;
        SH4_Program program;
        if (!SH4_AnalyzeProgram(&program, scn.data, header->codeStart, header->dataStart, header->end, header->entry)) {
            CHECK(false, "%s: the analysis failed", name);
            MAPINFO_Free(&file);
            continue;
        }
        programs++;
        bool d000 = strcmp(name, "scene/01/d000/mapinfo.bin") == 0;
        functions += program.functionCount;
        blocks += program.blockCount;
        instructions += program.code.stats.instructions;
        contextWrites += program.contextWrites;
        for (uint32_t b = 0; b < program.blockCount; b++) {
            const SH4_Block* block = &program.blocks[b];
            blockWords += (block->end - block->start) / 2;
            const SH4_Function* function = &program.functions[block->function];
            CHECK(block->start >= function->start && block->start < function->end,
                "%s: block 0x%X outside its function 0x%X", name, block->start, function->start);
            if (block->target != 0 && FindBlock(&program, block->target) < 0) {
                if (badTargets < 10)
                    CHECK(false, "%s: the branch of block 0x%X goes to 0x%X, not a block", name, block->start,
                        block->target);
                badTargets++;
            }
        }
        for (uint32_t f = 0; f < program.functionCount; f++) {
            const SH4_Function* function = &program.functions[f];
            CHECK(function->blockCount > 0 && program.blocks[function->firstBlock].start == function->start,
                "%s: function 0x%X doesn't start with a block", name, function->start);
            uncalled += function->callers == 0;
        }
        for (uint32_t c = 0; c < program.callCount; c++) {
            const SH4_Call* call = &program.calls[c];
            calls[call->kind]++;
            if (call->kind == SH4_CALL_LOCAL) {
                int callee = SH4_FunctionAt(&program, call->target);
                if (callee < 0 || program.functions[callee].start != call->target)
                    badLocal++;
            }
            if (call->kind == SH4_CALL_GAME || call->kind == SH4_CALL_SERVICE2C || call->kind == SH4_CALL_SERVICE30)
                unknownIds += call->id < 0;
            if (call->kind == SH4_CALL_GAME || call->kind == SH4_CALL_LOCAL)
                noPop += call->argumentBytes < 0;
            if (call->kind == SH4_CALL_GAME && call->id >= 0 && call->id < MAX_ID && call->argumentBytes >= 0) {
                idCalls[call->id]++;
                if (idBytes[call->id] < 0)
                    idBytes[call->id] = call->argumentBytes;
                else if (idBytes[call->id] != call->argumentBytes)
                    idConflicts[call->id]++;
                const SH4_Value* last = call->argumentCount > 0 ? &call->arguments[call->argumentCount - 1] : NULL;
                if (last != NULL && last->kind == SH4_VALUE_CONSTANT) {
                    uint32_t s = 0;
                    while (s < subcodeCount && (subcodes[s].id != call->id || subcodes[s].last != last->value))
                        s++;
                    if (s == subcodeCount && subcodeCount < MAX_SUBCODES) {
                        subcodes[s].id = call->id;
                        subcodes[s].last = last->value;
                        subcodes[s].bytes = call->argumentBytes;
                        subcodeCount++;
                    } else if (s < subcodeCount && subcodes[s].bytes != call->argumentBytes) {
                        subcodes[s].conflict = true;
                    }
                }
            }
            for (uint32_t a = 0; a < call->argumentCount; a++) {
                const SH4_Value* value = &call->arguments[a];
                arguments++;
                knownArguments += value->kind != SH4_VALUE_UNKNOWN;
                if (value->kind == SH4_VALUE_DATA) {
                    const char* text = DataString(&scn, value->value);
                    for (size_t s = 0; d000 && text != NULL && s < sizeof(s_d000Strings) / sizeof(s_d000Strings[0]); s++)
                        d000Found[s] |= strcmp(text, s_d000Strings[s]) == 0;
                }
            }
        }
        for (uint32_t l = 0; l < program.literalCount; l++) {
            const SH4_Literal* literal = &program.literals[l];
            literalKinds[literal->kind]++;
            if (literal->kind == SH4_LITERAL_DATA_OFFSET && literal->value >= header->end - header->dataStart)
                badData++;
        }
        CHECK(program.functions[0].start == header->codeStart, "%s: the first function isn't at codeStart", name);
        if (!haveStart && header->codeStart + START_CODE_SIZE <= header->dataStart) {
            memcpy(startCode, scn.data + header->codeStart, START_CODE_SIZE);
            haveStart = true;
        }
        sameStart += haveStart && header->codeStart + START_CODE_SIZE <= header->dataStart &&
            memcmp(startCode, scn.data + header->codeStart, START_CODE_SIZE) == 0;
        if (file.doorCount > 0) {
            uint32_t references = DoorTableReferences(&program, &scn, &file);
            doorFiles++;
            doorReferenced += references > 0;
            // The Hazuki house (jomo) has other records that point to the names of the door models
            CHECK(references == (strcmp(list.files[i].map, "jomo") == 0 ? 6u : 1u), "%s: %u references to the door table",
                name, references);
        }
        SH4_FreeProgram(&program);
        MAPINFO_Free(&file);
    }
    CHECK_EQ(programs, 119, "analyzed programs");
    CHECK_EQ(blockWords, instructions, "words of the blocks");
    CHECK_EQ(badTargets, 0, "branches that don't go to the start of a block");
    CHECK_EQ(badLocal, 0, "local calls that don't go to the start of a function");
    CHECK_EQ(contextWrites, 0, "writes to r8 (the context) outside the code at codeStart");
    CHECK_EQ(unknownIds, 0, "calls to the game or to a service without a constant ID");
    CHECK_EQ(calls[SH4_CALL_GAME], 255148, "calls to the game (ctx[0x28])");
    CHECK_EQ(calls[SH4_CALL_SERVICE2C], 31569, "calls to ctx[0x2C]");
    CHECK_EQ(calls[SH4_CALL_SERVICE30], 3431, "calls to ctx[0x30]");
    CHECK_EQ(calls[SH4_CALL_YIELD], 32115, "yields (ctx[0x3C])");
    CHECK_EQ(badData, 0, "data offsets out of the data");
    for (size_t s = 0; s < sizeof(s_d000Strings) / sizeof(s_d000Strings[0]); s++)
        CHECK(d000Found[s], "scene/01/d000: no call passes \"%s\"", s_d000Strings[s]);
    CHECK_EQ(sameStart, 119, "programs with the same code at codeStart");
    CHECK_EQ(doorFiles, 96, "SH-4 programs with a door table");
    CHECK_EQ(doorReferenced, 96, "door tables referenced by the code");
    // The IDs called with different numbers of arguments: for most of them, the number depends on the last argument
    uint32_t ids = 0, conflicts = 0, bySubcode = 0;
    for (int id = 0; id < MAX_ID; id++) {
        ids += idCalls[id] > 0;
        if (idConflicts[id] == 0)
            continue;
        conflicts++;
        bool fixed = true;
        for (uint32_t s = 0; s < subcodeCount; s++)
            fixed &= subcodes[s].id != id || !subcodes[s].conflict;
        bySubcode += fixed;
    }
    CHECK_EQ(ids, 329, "game IDs");
    CHECK_EQ(conflicts, 59, "game IDs called with different numbers of arguments");
    CHECK_EQ(bySubcode, 51, "of them, with a number of arguments fixed by the last argument");
    free(subcodes);
    Test_Info("%u programs: %llu functions (%llu never called locally), %llu blocks", programs,
        (unsigned long long)functions, (unsigned long long)uncalled, (unsigned long long)blocks);
    Test_Info("calls: %llu local, %llu game, %llu ctx[0x2C], %llu ctx[0x30], %llu yield, %llu other context, %llu unknown",
        (unsigned long long)calls[SH4_CALL_LOCAL], (unsigned long long)calls[SH4_CALL_GAME],
        (unsigned long long)calls[SH4_CALL_SERVICE2C], (unsigned long long)calls[SH4_CALL_SERVICE30],
        (unsigned long long)calls[SH4_CALL_YIELD], (unsigned long long)calls[SH4_CALL_CONTEXT],
        (unsigned long long)calls[SH4_CALL_UNKNOWN]);
    Test_Info("%llu game and local calls without a pop of their arguments; %llu arguments, %llu known (%.1f%%)",
        (unsigned long long)noPop, (unsigned long long)arguments, (unsigned long long)knownArguments,
        arguments ? 100.0 * knownArguments / (double)arguments : 0.0);
    Test_Info("%u game IDs, %u with calls of different numbers of arguments", ids, conflicts);
    Test_Info("literals: %llu values, %llu code offsets, %llu data offsets, %llu call IDs",
        (unsigned long long)literalKinds[0], (unsigned long long)literalKinds[1], (unsigned long long)literalKinds[2],
        (unsigned long long)literalKinds[3]);
    free(idCalls);
    free(idBytes);
    free(idConflicts);
    Corpus_Free(&list);
}

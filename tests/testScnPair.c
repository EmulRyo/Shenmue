#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/scnBytecode.h"
#include "formats/script/scnPair.h"
#include "formats/script/sh4Listing.h"
#include "test.h"

// The two versions of the program of each SH-4 map: the SH-4 code of its mapinfo.bin and the bytecode of its SCN file
// have the same functions in the same order, and the paired calls have the same arguments when both versions know them,
// but for a few values that differ between the two revisions

typedef struct {
    uint64_t    functions;
    uint64_t    sameFunctions;
    uint64_t    calls;              // Calls of the SH-4 programs that both versions make
    uint64_t    pairedCalls;
    uint64_t    arguments;          // Of the paired calls
    uint64_t    knownSh4;           // Known in the SH-4 code
    uint64_t    knownMerged;        // Known in the SH-4 code or in the bytecode
    uint64_t    compared;           // Known in both
    uint64_t    different;          // Known in both, different
} PairStats;

// The text of a value of a program, for the comparison: the number, or the text of the string of the data
static void ValueText(const SH4_Program* sh4, const BC_Program* bytecode, const SH4_Value* value, char* text,
    size_t size) {
    if (value->kind == SH4_VALUE_CONSTANT)
        snprintf(text, size, "%u", value->value);
    else if (value->kind == SH4_VALUE_DATA) {
        const char* string = sh4 != NULL ? SH4_DataString(sh4, value->value) : BC_DataString(bytecode, value->value);
        snprintf(text, size, "\"%.200s\"", string != NULL ? string : "?");
    } else
        snprintf(text, size, "?");
}

static void CompareCalls(const SH4_Program* sh4, const BC_Program* bytecode, const SCN_Pair* pair, PairStats* stats,
    const char* name) {
    for (uint32_t c = 0; c < sh4->callCount; c++) {
        const SH4_Call* call = &sh4->calls[c];
        if (call->kind != SH4_CALL_GAME && call->kind != SH4_CALL_LOCAL && call->kind != SH4_CALL_SERVICE2C &&
            call->kind != SH4_CALL_SERVICE30)
            continue;
        stats->calls++;
        const SH4_Call* other = SCN_PairedCall(pair, bytecode, c);
        if (other == NULL)
            continue;
        stats->pairedCalls++;
        CHECK(other->argumentBytes == call->argumentBytes || call->argumentBytes < 0 || other->argumentBytes < 0,
            "%s: 0x%X: %d bytes of arguments, %d in the bytecode", name, call->address, call->argumentBytes,
            other->argumentBytes);
        for (uint32_t a = 0; a < call->argumentCount && a < other->argumentCount; a++) {
            const SH4_Value* x = &call->arguments[a];
            const SH4_Value* y = &other->arguments[a];
            bool knownX = x->kind == SH4_VALUE_CONSTANT || x->kind == SH4_VALUE_DATA;
            bool knownY = y->kind == SH4_VALUE_CONSTANT || y->kind == SH4_VALUE_DATA;
            stats->arguments++;
            stats->knownSh4 += knownX;
            stats->knownMerged += knownX || knownY;
            if (!knownX || !knownY)
                continue;
            stats->compared++;
            char textX[256], textY[256];
            ValueText(sh4, NULL, x, textX, sizeof(textX));
            ValueText(NULL, bytecode, y, textY, sizeof(textY));
            // The two versions are slightly different revisions: a few values changed (e.g. phone numbers)
            if (strcmp(textX, textY) != 0) {
                if (stats->different < 4)
                    Test_Info("%s: 0x%X: argument %u is %s, %s in the bytecode", name, call->address, a, textX, textY);
                stats->different++;
            }
        }
    }
}

void Test_ScnPair(const TestContext* context) {
    CorpusList maps = Corpus_MapinfoFiles(context->content);
    PairStats stats = { 0 };
    uint32_t pairs = 0, programs = 0;
    for (uint32_t i = 0; i < maps.count; i++) {
        const CorpusFile* map = &maps.files[i];
        MAPINFO_File file;
        if (!MAPINFO_Load(map->path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program scn;
        SH4_Program sh4;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &scn) || !SCN_IsSH4(&scn) ||
            !SH4_AnalyzeProgram(&sh4, scn.data, scn.header.codeStart, scn.header.dataStart, scn.header.end,
                scn.header.entry)) {
            MAPINFO_Free(&file);
            continue;
        }
        programs++;
        // Its SCN file
        CorpusFile scnFile = *map;
        snprintf(scnFile.name, sizeof(scnFile.name), "scenario/act%02d_%s.scn", map->disc, map->map);
        snprintf(scnFile.path, sizeof(scnFile.path), "%s/%s", context->content, scnFile.name);
        uint32_t size;
        uint8_t* data = Corpus_Load(&scnFile, &size);
        SCN_Program scn2;
        BC_Program bytecode;
        SCN_Pair pair;
        if (data != NULL && SCN_Read(data, size, &scn2) && BC_AnalyzeProgram(&bytecode, &scn2)) {
            if (SCN_PairPrograms(&pair, &sh4, &bytecode)) {
                pairs++;
                stats.functions += pair.pairedFunctions;
                stats.sameFunctions += pair.sameFunctions;
                CompareCalls(&sh4, &bytecode, &pair, &stats, map->name);
                SCN_FreePair(&pair);
            } else {
                CHECK(false, "%s: %u functions, %u in %s", map->name, sh4.functionCount, bytecode.functionCount,
                    scnFile.name);
            }
            BC_FreeProgram(&bytecode);
        }
        free(data);
        SH4_FreeProgram(&sh4);
        MAPINFO_Free(&file);
    }
    CHECK_EQ(programs, 119, "SH-4 programs");
    CHECK_EQ(pairs, 119, "SH-4 programs paired with their SCN file");
    CHECK_EQ(stats.functions, 23864, "paired functions");
    CHECK_EQ(stats.different, 184, "arguments known in both versions with different values");
    Test_Info("%llu paired functions, %llu with the same calls (%.1f%%); %llu of %llu calls paired",
        (unsigned long long)stats.functions, (unsigned long long)stats.sameFunctions,
        stats.functions ? 100.0 * stats.sameFunctions / (double)stats.functions : 0.0,
        (unsigned long long)stats.pairedCalls, (unsigned long long)stats.calls);
    Test_Info("arguments of the paired calls: %llu, known in the SH-4 code %.1f%%, with the bytecode %.1f%%; %llu compared",
        (unsigned long long)stats.arguments, stats.arguments ? 100.0 * stats.knownSh4 / (double)stats.arguments : 0.0,
        stats.arguments ? 100.0 * stats.knownMerged / (double)stats.arguments : 0.0, (unsigned long long)stats.compared);
    Corpus_Free(&maps);
}

#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/script/scn.h"
#include "formats/script/scnDecompile.h"
#include "test.h"

// The pseudo-C of the bytecode of the 136 SCN files: every function is decompiled with the values of the stack used
// where they are expected, the branches become blocks but for a few gotos, and two programs give a known text

// The whole program of act01_mk99.scn (without the names of the game functions, which the tests don't set)
static const char* const s_mk99[] = {
    "func_30() {",
    "    game_002(func_43, 1, 'AKIR');",
    "    return;",
    "}",
    "func_43(arg0) {",
    "    game_01F(arg0, 1);",
    "    game_005(arg0);",
    "    game_02D(1);",
    "    return;",
    "}",
};

#define D000_LINE "game_13E(0, 4, \"seqdata1.bin\", \"D0W0\");"

static void CheckMk99(const BC_Program* program) {
    uint32_t expected = 0, count = sizeof(s_mk99) / sizeof(s_mk99[0]);
    for (uint32_t f = 0; f < program->functionCount; f++) {
        DC_Function function;
        if (!DC_DecompileFunction(program, f, &function))
            continue;
        for (uint32_t l = 0; l < function.lineCount; l++, expected++) {
            char line[256];
            snprintf(line, sizeof(line), "%*s%s", function.lines[l].indent * 4, "", function.lines[l].text);
            CHECK(expected < count && strcmp(line, s_mk99[expected]) == 0, "act01_mk99: line %u \"%s\", expected \"%s\"",
                expected, line, expected < count ? s_mk99[expected] : "nothing");
        }
        DC_FreeFunction(&function);
    }
    CHECK_EQ(expected, count, "lines of act01_mk99");
}

void Test_Decompile(const TestContext* context) {
    CorpusList list = Corpus_ScnFiles(context->content);
    uint64_t functions = 0, lines = 0, gotos = 0, withGoto = 0, stackErrors = 0, failed = 0;
    bool d000 = false;
    for (uint32_t i = 0; i < list.count; i++) {
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program scn;
        BC_Program program;
        if (data == NULL || !SCN_Read(data, size, &scn) || !BC_AnalyzeProgram(&program, &scn)) {
            free(data);
            continue;
        }
        bool isD000 = strcmp(list.files[i].name, "scenario/act01_d000.scn") == 0;
        if (strcmp(list.files[i].name, "scenario/act01_mk99.scn") == 0)
            CheckMk99(&program);
        for (uint32_t f = 0; f < program.functionCount; f++) {
            DC_Function function;
            if (!DC_DecompileFunction(&program, f, &function)) {
                failed++;
                continue;
            }
            functions++;
            lines += function.lineCount;
            gotos += function.gotoCount;
            withGoto += function.gotoCount > 0;
            stackErrors += function.stackErrors;
            for (uint32_t l = 0; isD000 && l < function.lineCount; l++)
                d000 |= strcmp(function.lines[l].text, D000_LINE) == 0;
            DC_FreeFunction(&function);
        }
        BC_FreeProgram(&program);
        free(data);
    }
    CHECK_EQ(failed, 0, "functions that can't be decompiled");
    CHECK_EQ(functions, 24400, "decompiled functions");
    CHECK_EQ(stackErrors, 0, "values of the stack missing or left at a label");
    CHECK_EQ(gotos, 12, "gotos");
    CHECK(d000, "act01_d000: no line %s", D000_LINE);
    Test_Info("%llu functions, %llu lines of pseudo-C, %llu gotos in %llu functions", (unsigned long long)functions,
        (unsigned long long)lines, (unsigned long long)gotos, (unsigned long long)withGoto);
    Corpus_Free(&list);
}

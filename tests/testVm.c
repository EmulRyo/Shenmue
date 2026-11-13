#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/script/scn.h"
#include "formats/script/scnVm.h"
#include "test.h"

// The emulation of the bytecode of the 136 SCN files: the arguments that the emulation knows are those of the analysis
// of the bytecode when it knows them too (the constants and the addresses of the data and of the code pushed before
// the calls), and the program of act01_mk99.scn runs its two threads with the expected calls

// act01_mk99.scn: the entry point starts a thread of the function 0x43 with 'AKIR', which calls three functions with it
static void CheckMk99(const BC_Program* program, const VM_Result* result) {
    CHECK_EQ(result->threads, 2, "act01_mk99: threads");
    CHECK_EQ(result->callCount, 4, "act01_mk99: calls");
    if (result->callCount != 4)
        return;
    const uint32_t akir = 0x52494B41;
    CHECK(result->calls[0].id == 0x002 && result->calls[0].count == 3 && result->calls[0].known == 7 &&
        result->calls[0].values[0] == VM_PROGRAM_BASE + 0x43 && result->calls[0].values[1] == 1 &&
        result->calls[0].values[2] == akir, "act01_mk99: StartThread(0x43, 1, 'AKIR')");
    CHECK(result->calls[1].id == 0x01F && result->calls[1].known == 3 && result->calls[1].values[0] == akir &&
        result->calls[1].values[1] == 1, "act01_mk99: game_01F('AKIR', 1)");
    CHECK(result->calls[2].id == 0x005 && result->calls[2].values[0] == akir, "act01_mk99: game_005('AKIR')");
    CHECK(result->calls[3].id == 0x02D && result->calls[3].values[0] == 1, "act01_mk99: game_02D(1)");
    (void)program;
}

void Test_Vm(const TestContext* context) {
    CorpusList list = Corpus_ScnFiles(context->content);
    uint64_t threads = 0, steps = 0, calls = 0, compared = 0, different = 0, errors = 0, programs = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program scn;
        BC_Program program;
        VM_Result result;
        if (data == NULL || !SCN_Read(data, size, &scn) || !BC_AnalyzeProgram(&program, &scn)) {
            free(data);
            continue;
        }
        if (VM_Run(&program, &result)) {
            programs++;
            threads += result.threads;
            steps += result.steps;
            calls += result.callCount;
            errors += result.errors;
            if (strcmp(list.files[i].name, "scenario/act01_mk99.scn") == 0)
                CheckMk99(&program, &result);
            for (uint32_t c = 0; c < result.callCount; c++) {
                const VM_Call* run = &result.calls[c];
                const SH4_Call* call = BC_CallAt(&program, run->address);
                CHECK(call != NULL && call->id == run->id, "%s: 0x%X: call not found", list.files[i].name, run->address);
                for (uint32_t a = 0; call != NULL && a < run->count && a < call->argumentCount; a++) {
                    const SH4_Value* value = &call->arguments[a];
                    if (!(run->known & (1u << a)) || (value->kind != SH4_VALUE_CONSTANT && value->kind != SH4_VALUE_DATA))
                        continue;
                    uint32_t expected = value->kind == SH4_VALUE_CONSTANT ? value->value :
                        VM_PROGRAM_BASE + scn.header.dataStart + value->value;
                    compared++;
                    // The analysis gives the addresses of the code (functions) as their offset
                    bool code = value->kind == SH4_VALUE_CONSTANT && expected >= scn.header.codeStart &&
                        expected < program.codeEnd && run->values[a] == VM_PROGRAM_BASE + expected;
                    if (run->values[a] != expected && !code) {
                        if (different < 5)
                            CHECK(false, "%s: 0x%X: argument %u is 0x%X, 0x%X in the bytecode", list.files[i].name,
                                run->address, a, run->values[a], expected);
                        different++;
                    }
                }
            }
            VM_Free(&result);
        }
        BC_FreeProgram(&program);
        free(data);
    }
    Corpus_Free(&list);
    CHECK_EQ(programs, 136, "programs emulated");
    CHECK_EQ(errors, 0, "paths stopped by an invalid instruction or stack");
    CHECK_EQ(different, 0, "arguments different from those of the bytecode");
    Test_Info("%llu threads, %llu instructions run, %llu different calls; %llu arguments compared with the bytecode",
        (unsigned long long)threads, (unsigned long long)steps, (unsigned long long)calls, (unsigned long long)compared);
}

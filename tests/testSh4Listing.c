#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/sh4Listing.h"
#include "test.h"

// The listing of the SH-4 programs: the lines of each function cover it without gaps, every place a line leads to is a
// line of a function, and the comments of the calls show their ID and arguments

// A call of scene/01/d000, from the wiki
#define D000_CALL "game 0x13E(0, 4, \"seqdata1.bin\", \"D0W0\")"

void Test_Sh4Listing(const TestContext* context) {
    CorpusList list = Corpus_MapinfoFiles(context->content);
    uint32_t programs = 0;
    uint64_t lines = 0, gaps = 0, badJumps = 0, uncommentedCalls = 0, jumps = 0;
    bool d000Call = false;
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        MAPINFO_File file;
        if (!MAPINFO_Load(list.files[i].path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program scn;
        SH4_Program program;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &scn) || !SCN_IsSH4(&scn) ||
            !SH4_AnalyzeProgram(&program, scn.data, scn.header.codeStart, scn.header.dataStart, scn.header.end,
                scn.header.entry)) {
            MAPINFO_Free(&file);
            continue;
        }
        programs++;
        bool d000 = strcmp(name, "scene/01/d000/mapinfo.bin") == 0;
        uint32_t max = 0;
        for (uint32_t f = 0; f < program.functionCount; f++) {
            uint32_t m = SH4_MaxFunctionLines(&program, f);
            max = m > max ? m : max;
        }
        SH4_Line* functionLines = malloc((max + 1) * sizeof(SH4_Line));
        // Start of each line (a byte per word of the code), and the places the lines lead to
        uint32_t codeWords = (program.code.end - program.code.start) / 2;
        uint8_t* lineStarts = calloc(codeWords + 1, 1);
        uint32_t* targets = malloc((codeWords + 1) * sizeof(uint32_t));
        uint32_t targetCount = 0;
        for (uint32_t f = 0; f < program.functionCount && functionLines != NULL && lineStarts != NULL && targets != NULL; f++) {
            const SH4_Function* function = &program.functions[f];
            uint32_t count = SH4_FunctionLines(&program, f, functionLines);
            lines += count;
            CHECK(count > 1 && functionLines[0].kind == SH4_LINE_FUNCTION, "%s: function 0x%X without lines", name,
                function->start);
            uint32_t next = function->start;
            for (uint32_t l = 1; l < count; l++) {
                const SH4_Line* line = &functionLines[l];
                if (line->address != next)
                    gaps++;
                if (line->address >= program.code.start && line->address < program.code.end)
                    lineStarts[(line->address - program.code.start) / 2] = 1;
                next = line->address + line->size;
                if (line->kind == SH4_LINE_INSTRUCTION && SH4_CallAt(&program, line->address) != NULL &&
                    line->comment[0] == '\0')
                    uncommentedCalls++;
                if (d000 && strcmp(line->comment, D000_CALL) == 0)
                    d000Call = true;
                if (line->jump == 0)
                    continue;
                jumps++;
                if (targetCount <= codeWords)
                    targets[targetCount++] = line->jump;
            }
            if (next != function->end)
                gaps++;
        }
        for (uint32_t t = 0; t < targetCount && lineStarts != NULL; t++) {
            uint32_t target = targets[t];
            if (target < program.code.start || target >= program.code.end ||
                !lineStarts[(target - program.code.start) / 2])
                badJumps++;
        }
        free(functionLines);
        free(lineStarts);
        free(targets);
        SH4_FreeProgram(&program);
        MAPINFO_Free(&file);
    }
    CHECK_EQ(programs, 119, "listed programs");
    CHECK_EQ(gaps, 0, "gaps or overlaps between the lines of a function");
    CHECK_EQ(badJumps, 0, "lines that lead to a place that isn't a line");
    CHECK_EQ(uncommentedCalls, 0, "calls without a comment");
    CHECK(d000Call, "scene/01/d000: no line with the comment %s", D000_CALL);
    Test_Info("%u programs: %llu lines, %llu lead to another place", programs, (unsigned long long)lines,
        (unsigned long long)jumps);
    Corpus_Free(&list);
}

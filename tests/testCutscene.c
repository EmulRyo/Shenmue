#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "corpus.h"
#include "formats/script/scn.h"
#include "formats/script/scnCutscene.h"
#include "formats/script/scnData.h"
#include "test.h"

// The run with time of the code of the scene programs that plays with the cutscenes of their table (scnCutscene.h):
// over the cutscenes that the 136 SCN files play, the calls are in the order of the frames and move nodes of characters,
// and in act01_d000.scn the cutscenes 16 and 17 of the bus open and close its doors and turn its wheels at the frames
// of the wiki

#define CUTSCENE_FRAMES 600
#define BUS             0x5F535542  // 'BUS_'

// The rotation of a node of the bus at a frame
static int32_t Rotation(const SCNCutscene* cutscene, uint16_t node, uint32_t frame, int axis) {
    SCNCutscene_Transform transform = { { 0 }, { 0 }, { 1, 1, 1 } };
    SCNCutscene_NodeAt(cutscene, BUS, node, frame, &transform);
    return (int16_t)transform.rotation[axis];
}

static void CheckBus(const BC_Program* program) {
    // 16, getting on (seqdata1.bin): the front doors, open, close from the frame 100; the wheels turn from 125
    SCNCutscene cutscene;
    if (!SCNCutscene_Run(program, 16, "seqdata1.bin", 246, &cutscene) || !cutscene.found) {
        CHECK(false, "act01_d000: cutscene 16 not run");
        return;
    }
    CHECK(cutscene.run.started && cutscene.run.stopped == 0, "act01_d000: cutscene 16 run");
    CHECK(Rotation(&cutscene, 153, 0, 1) == 0x371C && Rotation(&cutscene, 154, 0, 1) == 0x770A, "act01_d000: cutscene 16, front doors open");
    CHECK(Rotation(&cutscene, 153, 99, 1) == 0x371C, "act01_d000: cutscene 16, front door open at the frame 99");
    CHECK(Rotation(&cutscene, 153, 100, 1) == 0x393E - 455, "act01_d000: cutscene 16, front door closing at the frame 100 (%d)", Rotation(&cutscene, 153, 100, 1));
    CHECK(Rotation(&cutscene, 153, 130, 1) == 0x393E - 31 * 455 && Rotation(&cutscene, 154, 130, 1) == (int16_t)(0x88F6 + 31 * 983),
        "act01_d000: cutscene 16, front doors closed at the frame 130 (%d, %d)", Rotation(&cutscene, 153, 130, 1), Rotation(&cutscene, 154, 130, 1));
    CHECK(Rotation(&cutscene, 163, 124, 0) == 0 && Rotation(&cutscene, 163, 125, 0) == 0xAAA &&
        Rotation(&cutscene, 163, 127, 0) == 3 * 0xAAA && Rotation(&cutscene, 182, 127, 0) == -3 * 0xAAA,
        "act01_d000: cutscene 16, wheels from the frame 125");
    bool engine = false;
    for (uint32_t s = 0; s < cutscene.soundCount; s++)
        engine |= cutscene.sounds[s].command == 0x1E04A9 && cutscene.sounds[s].frame == 125;
    CHECK(cutscene.soundCount >= 2 && cutscene.sounds[0].command == 0x1D04A9 && cutscene.sounds[0].frame == 0 && engine,
        "act01_d000: cutscene 16, sounds 0x1D04A9 at 0 and 0x1E04A9 at 125");
    SCNCutscene_Free(&cutscene);

    // 17, getting off (seqdata2.bin): the middle and back doors, open, close from the frame 132; the wheels from 165
    if (!SCNCutscene_Run(program, 17, "seqdata2.bin", 246, &cutscene) || !cutscene.found) {
        CHECK(false, "act01_d000: cutscene 17 not run");
        return;
    }
    CHECK(Rotation(&cutscene, 157, 0, 1) == 0x371C && Rotation(&cutscene, 158, 0, 1) == -0x770A &&
        Rotation(&cutscene, 155, 0, 1) == -0x371C && Rotation(&cutscene, 156, 0, 1) == 0x770A, "act01_d000: cutscene 17, doors open");
    CHECK(Rotation(&cutscene, 157, 131, 1) == 0x371C && Rotation(&cutscene, 157, 132, 1) == 0x393E - 455,
        "act01_d000: cutscene 17, doors closing at the frame 132");
    CHECK(Rotation(&cutscene, 157, 162, 1) == 0x393E - 31 * 455 && Rotation(&cutscene, 155, 162, 1) == -0x393E + 31 * 455,
        "act01_d000: cutscene 17, doors closed at the frame 162");
    CHECK(Rotation(&cutscene, 173, 164, 0) == 0 && Rotation(&cutscene, 173, 165, 0) == 0xAAA, "act01_d000: cutscene 17, wheels from the frame 165");
    CHECK(cutscene.soundCount >= 2 && cutscene.sounds[0].command == 0x1B04A9 && cutscene.sounds[0].frame == 0,
        "act01_d000: cutscene 17, sound 0x1B04A9 at 0");
    SCNCutscene_Free(&cutscene);
}

// The nodes moved, e.g. "BUS_ 153 154 163"
static const char* NodesText(const SCNCutscene* cutscene) {
    static char text[512];
    int length = 0;
    text[0] = '\0';
    for (uint32_t n = 0; n < cutscene->nodeCount && length < (int)sizeof(text) - 16; n++) {
        const SCNCutscene_Node* node = &cutscene->nodes[n];
        bool seen = false, seenCharacter = false;
        for (uint32_t m = 0; m < n && !seen; m++) {
            seen = cutscene->nodes[m].character == node->character && cutscene->nodes[m].node == node->node;
            seenCharacter |= cutscene->nodes[m].character == node->character;
        }
        if (seen)
            continue;
        if (!seenCharacter)
            length += snprintf(text + length, sizeof(text) - length, "%s%.4s", length > 0 ? " " : "", (const char*)&node->character);
        length += snprintf(text + length, sizeof(text) - length, " %u", node->node);
    }
    return text;
}

void Test_Cutscene(const TestContext* context) {
    CorpusList list = Corpus_ScnFiles(context->content);
    uint32_t programs = 0, cutscenes = 0, found = 0, started = 0, moving = 0, sounding = 0, nodeCalls = 0, soundCalls = 0;
    uint32_t stopped = 0;
    clock_t start = clock();
    for (uint32_t i = 0; i < list.count; i++) {
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program scn;
        BC_Program program;
        if (data == NULL || !SCN_Read(data, size, &scn) || !BC_AnalyzeProgram(&program, &scn)) {
            free(data);
            continue;
        }
        programs++;
        if (strcmp(list.files[i].name, "scenario/act01_d000.scn") == 0)
            CheckBus(&program);
        // The slots played: the calls to Cutscene run by the emulation with a known slot
        bool played[SD_CUTSCENE_SLOTS] = { false };
        VM_Result emulation;
        if (VM_Run(&program, &emulation)) {
            for (uint32_t c = 0; c < emulation.callCount; c++) {
                const VM_Call* call = &emulation.calls[c];
                if (call->id == SCNCUTSCENE_PLAY && (call->known & 1) && call->values[0] < SD_CUTSCENE_SLOTS)
                    played[call->values[0]] = true;
            }
            VM_Free(&emulation);
        }
        // And those where the program loads an AUTH file
        for (uint32_t c = 0; c < program.callCount; c++) {
            const SH4_Call* call = &program.calls[c];
            if (call->kind == SH4_CALL_GAME && call->id == SCNCUTSCENE_SEQUENCE && call->argumentCount >= 2 &&
                call->arguments[0].kind == SH4_VALUE_CONSTANT && call->arguments[0].value == 0 &&
                call->arguments[1].kind == SH4_VALUE_CONSTANT && call->arguments[1].value < SD_CUTSCENE_SLOTS)
                played[call->arguments[1].value] = true;
        }
        for (uint32_t slot = 0; slot < SD_CUTSCENE_SLOTS; slot++) {
            if (!played[slot])
                continue;
            cutscenes++;
            SCNCutscene cutscene;
            if (!SCNCutscene_Run(&program, slot, NULL, CUTSCENE_FRAMES, &cutscene)) {
                CHECK(false, "%s: cutscene %u: memory error", list.files[i].name, slot);
                continue;
            }
            found += cutscene.found;
            started += cutscene.run.started;
            stopped += cutscene.run.stopped;
            moving += cutscene.nodeCount > 0;
            sounding += cutscene.soundCount > 0;
            nodeCalls += cutscene.nodeCount;
            soundCalls += cutscene.soundCount;
            for (uint32_t n = 1; n < cutscene.nodeCount; n++)
                CHECK(cutscene.nodes[n].frame >= cutscene.nodes[n - 1].frame, "%s: cutscene %u: calls not in order", list.files[i].name, slot);
            for (uint32_t n = 0; n < cutscene.nodeCount; n++)
                CHECK(SD_IsCharacterId(cutscene.nodes[n].character) && cutscene.nodes[n].frame < CUTSCENE_FRAMES,
                    "%s: cutscene %u: node of 0x%08X at %u", list.files[i].name, slot, cutscene.nodes[n].character, cutscene.nodes[n].frame);
            if (cutscene.nodeCount > 0)
                Test_Info("%s: cutscene %u: %u node calls (%s), %u sound calls, %u threads, %u frames", list.files[i].name, slot,
                    cutscene.nodeCount, NodesText(&cutscene), cutscene.soundCount, cutscene.run.threads, cutscene.run.frames);
            SCNCutscene_Free(&cutscene);
        }
        BC_FreeProgram(&program);
        free(data);
    }
    Corpus_Free(&list);
    Test_Info("%u programs, %u cutscenes played or loaded: %u found by the emulation, %u run, %u with node calls "
        "(%u calls), %u with sound commands (%u), %u threads stopped, %.1f s", programs, cutscenes, found, started, moving,
        nodeCalls, sounding, soundCalls, stopped, (double)(clock() - start) / CLOCKS_PER_SEC);
    CHECK_EQ(programs, 136, "SCN files");
    CHECK(found > 0 && started == found, "cutscenes found but not run");
    CHECK_EQ(stopped, 0, "threads stopped");
}

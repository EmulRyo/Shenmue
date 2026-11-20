#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "corpus.h"
#include "formats/script/scn.h"
#include "scenario/scnCutscene.h"
#include "scenario/scnData.h"
#include "test.h"

// The run with time of the code of the scene programs that plays with the cutscenes of their table (scnCutscene.h):
// over the cutscenes that the 136 SCN files play, the calls are in the order of the frames and move nodes of characters,
// and in act01_d000.scn the cutscenes 16 and 17 of the bus open and close its doors and turn its wheels at the frames
// of the wiki (17 then loads the sound banks of the map and plays its music), the cutscene 2 places the phone books, and the cutscene 22 of the vending machine has the machine JIHN
// where the loop of the map puts the machine VM_0 when Ryo is there, and the can and the letter in the hands

#define CUTSCENE_FRAMES 600
#define BUS             0x5F535542  // 'BUS_'
#define PHONE_BOOK      0x314B4254  // 'TBK1'
#define PHONE_BOOK_OPEN 0x324B4254  // 'TBK2'
#define VENDING_MACHINE 0x4E48494A  // 'JIHN'
#define MAP_MACHINE     0x305F4D56  // 'VM_0'
#define BIN             0x424D4F47  // 'GOMB'
#define WANG            0x49484B59  // 'YKHI'
#define CAN             0x314E4143  // 'CAN1'
#define LETTER          0x534C414D  // 'MALS'
#define HAND            18          // Node of the right hand

// The rotation of a node of the bus at a frame
static int32_t Rotation(const SCNCutscene* cutscene, uint16_t node, uint32_t frame, int axis) {
    SCNCutscene_Transform transform = { { 0 }, { 0 }, { 1, 1, 1 } };
    SCNCutscene_NodeAt(cutscene, BUS, node, frame, &transform);
    return (int16_t)transform.rotation[axis];
}

static void CheckBus(const BC_Program* program) {
    // 16, getting on (seqdata1.bin): the front doors, open, close from the frame 100; the wheels turn from 125
    SCNCutscene cutscene;
    if (!SCNCutscene_Run(program, 16, "seqdata1.bin", 246, NULL, &cutscene) || !cutscene.found) {
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
    if (!SCNCutscene_Run(program, 17, "seqdata2.bin", 246, NULL, &cutscene) || !cutscene.found) {
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
    // At the frame 217 the program loads the banks of the map (the music chosen by the game in the slot 2, the ambient
    // sounds amb021.snd in the slot 1) and plays the music 0x3FA8 at the volume 100
    bool music = false, ambient = false, select = false;
    for (uint32_t s = 0; s + 1 < cutscene.soundCount; s++) {
        const SCNCutscene_Sound* sound = &cutscene.sounds[s];
        const SCNCutscene_Sound* next = &cutscene.sounds[s + 1];
        music |= sound->command == 0x3FA8 && sound->frame == 217 && next->command == 0x4A0 && next->slot == 2 &&
            next->value == 100 && next->known == 3;
    }
    for (uint32_t b = 0; b < cutscene.bankCount; b++) {
        const SCNCutscene_Bank* bank = &cutscene.banks[b];
        ambient |= bank->frame == 217 && bank->slot == 1 && strcmp(bank->name, "amb021.snd") == 0 &&
            bank->function == SCNCUTSCENE_SOUND_BANKS;
        select |= bank->frame == 217 && bank->slot == 2 && strcmp(bank->name, "SELECT") == 0;
    }
    CHECK(music, "act01_d000: cutscene 17, music 0x3FA8 at 217 with the volume 100 of the slot 2");
    CHECK(ambient && select, "act01_d000: cutscene 17, banks amb021.snd and SELECT loaded at 217");
    SCNCutscene_Free(&cutscene);
}

// The characters placed, e.g. "TBK1 TBK2"
static const char* PlacedText(const SCNCutscene* cutscene) {
    static char text[512];
    int length = 0;
    text[0] = '\0';
    for (uint32_t n = 0; n < cutscene->placeCount && length < (int)sizeof(text) - 16; n++) {
        bool seen = false;
        for (uint32_t m = 0; m < n && !seen; m++)
            seen = cutscene->places[m].character == cutscene->places[n].character;
        if (!seen)
            length += snprintf(text + length, sizeof(text) - length, "%s%.4s", length > 0 ? " " : "",
                (const char*)&cutscene->places[n].character);
    }
    return text;
}

// The position (or the angles in degrees) of a character placed by the program at a frame
static bool Placed(const SCNCutscene* cutscene, uint32_t character, uint32_t frame, float position[3], float angle[3]) {
    position[0] = position[1] = position[2] = angle[0] = angle[1] = angle[2] = 0.0f;
    return SCNCutscene_PlaceAt(cutscene, character, frame, position, angle);
}

static bool Near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

// The cutscene 2 (seqdata0.bin of auth.pks): Ryo looks for a number in the phone book of the shop. The closed book of
// the map, TBK1, is placed on the counter at the frame 0; at the frame 120, it goes away (0, -10, 100) and the open
// book of the package of the cutscene, TBK2, takes its place
static void CheckPhoneBook(const BC_Program* program) {
    SCNCutscene cutscene;
    if (!SCNCutscene_Run(program, 2, "seqdata0.bin", 890, NULL, &cutscene) || !cutscene.found) {
        CHECK(false, "act01_d000: cutscene 2 not run");
        return;
    }
    float position[3], angle[3];
    CHECK(!Placed(&cutscene, PHONE_BOOK_OPEN, 0, position, angle) || Near(position[1], -10.0f),
        "act01_d000: cutscene 2, open phone book away at the frame 0");
    CHECK(Placed(&cutscene, PHONE_BOOK, 0, position, angle) && Near(position[0], 38.1611f) && Near(position[1], 0.972f) &&
        Near(position[2], 10.1892f) && Near(angle[1], 0x2D92 * 360.0f / 65536.0f),
        "act01_d000: cutscene 2, closed phone book on the counter at the frame 0 (%g %g %g, %g)", position[0], position[1],
        position[2], angle[1]);
    CHECK(Placed(&cutscene, PHONE_BOOK, 119, position, angle) && Near(position[0], 38.1611f),
        "act01_d000: cutscene 2, closed phone book at the frame 119");
    CHECK(Placed(&cutscene, PHONE_BOOK, 120, position, angle) && Near(position[1], -10.0f) && Near(position[2], 100.0f),
        "act01_d000: cutscene 2, closed phone book away at the frame 120");
    CHECK(Placed(&cutscene, PHONE_BOOK_OPEN, 120, position, angle) && Near(position[0], 38.2248f) && Near(position[1], 0.9712f) &&
        Near(position[2], 10.056f) && Near(angle[1], 0xACBC * 360.0f / 65536.0f),
        "act01_d000: cutscene 2, open phone book on the counter at the frame 120 (%g %g %g, %g)", position[0], position[1],
        position[2], angle[1]);
    SCNCutscene_Free(&cutscene);
}

// The cutscene 22 (seqdata3.bin of djhn_03.pks): Ryo and YKHI at the vending machine. Before it, the event of the machine
// hides the machine of the map VM_0 and puts JIHN, the machine of scene/01/stream/vend.pkf, at its place: where the
// loop of the map puts VM_0 when Ryo is near it (Ryo where the cutscene starts); JIHN is put to sleep at the frame 1600
static void CheckVendingMachine(const BC_Program* program) {
    const uint32_t characters[2] = { SCNCUTSCENE_RYO, WANG };
    SCNCutscene_Scene scene = { true, { -16.0296726f, 0.0643469989f, 73.2457428f }, { 0.0f, 90.0f, 0.0f }, 2, characters };
    SCNCutscene cutscene;
    if (!SCNCutscene_Run(program, 22, "seqdata3.bin", 2260, &scene, &cutscene) || !cutscene.found) {
        CHECK(false, "act01_d000: cutscene 22 not run");
        return;
    }
    CHECK(cutscene.levels >= 1 && cutscene.run.started, "act01_d000: cutscene 22 run from the event of the machine (%u levels)",
        cutscene.levels);
    float position[3], angle[3];
    CHECK(Placed(&cutscene, VENDING_MACHINE, 0, position, angle) && Near(position[0], -15.99f) && Near(position[1], 0.0724f) &&
        Near(position[2], 72.2601f) && Near(angle[0], 0.0f) && Near(angle[1], 0.0f) && Near(angle[2], 0.0f),
        "act01_d000: cutscene 22, JIHN at VM_0 at the frame 0 (%g %g %g, %g)", position[0], position[1], position[2], angle[1]);
    bool shown = true;
    CHECK(SCNCutscene_ShownAt(&cutscene, MAP_MACHINE, 0, &shown) && !shown, "act01_d000: cutscene 22, VM_0 hidden");
    shown = true;
    CHECK(SCNCutscene_ShownAt(&cutscene, BIN, 0, &shown) && !shown, "act01_d000: cutscene 22, GOMB asleep");
    shown = true;
    CHECK(!SCNCutscene_ShownAt(&cutscene, VENDING_MACHINE, 1599, &shown) &&
        SCNCutscene_ShownAt(&cutscene, VENDING_MACHINE, 1600, &shown) && !shown, "act01_d000: cutscene 22, JIHN asleep at 1600");
    // The can of the drink bought, SPRT before the choice of the player, in the hand of Ryo at 160, given to YKHI at 255
    // and dropped at 1810; the letter in the hand of Ryo from 580 to 1160
    const SCNCutscene_Package* package = SCNCutscene_PackageOf(&cutscene, CAN);
    CHECK(package != NULL && strcmp(package->folder, "/scene/01/stream/") == 0 && strcmp(package->name, "SPRT") == 0,
        "act01_d000: cutscene 22, package of CAN1 (%s%s)", package != NULL ? package->folder : "", package != NULL ? package->name : "none");
    const SCNCutscene_Attach* attach = SCNCutscene_AttachAt(&cutscene, CAN, 159);
    CHECK(attach == NULL, "act01_d000: cutscene 22, CAN1 attached before 160");
    attach = SCNCutscene_AttachAt(&cutscene, CAN, 160);
    CHECK(attach != NULL && attach->frame == 160 && attach->parent == SCNCUTSCENE_RYO && attach->node == HAND && attach->known == 0x3F,
        "act01_d000: cutscene 22, CAN1 in the hand of Ryo at 160");
    attach = SCNCutscene_AttachAt(&cutscene, CAN, 255);
    CHECK(attach != NULL && attach->frame == 255 && attach->parent == WANG && attach->node == HAND,
        "act01_d000: cutscene 22, CAN1 in the hand of YKHI at 255");
    attach = SCNCutscene_AttachAt(&cutscene, CAN, 1810);
    CHECK(attach != NULL && attach->frame == 1810 && attach->parent == 0, "act01_d000: cutscene 22, CAN1 dropped at 1810");
    attach = SCNCutscene_AttachAt(&cutscene, LETTER, 580);
    CHECK(attach != NULL && attach->frame == 580 && attach->parent == SCNCUTSCENE_RYO && attach->node == HAND,
        "act01_d000: cutscene 22, MALS in the hand of Ryo at 580");
    attach = SCNCutscene_AttachAt(&cutscene, LETTER, 1160);
    CHECK(attach != NULL && attach->frame == 1160 && attach->parent == 0, "act01_d000: cutscene 22, MALS detached at 1160");
    shown = true;
    CHECK(SCNCutscene_ShownAt(&cutscene, LETTER, 1160, &shown) && !shown, "act01_d000: cutscene 22, MALS hidden at 1160");
    SCNCutscene_Free(&cutscene);
    // Without the scene, Ryo isn't known: nor at which of its places the loop of the map puts VM_0 (only the height, the
    // same at all of them)
    if (!SCNCutscene_Run(program, 22, "seqdata3.bin", 2260, NULL, &cutscene) || !cutscene.found) {
        CHECK(false, "act01_d000: cutscene 22 not run without the scene");
        return;
    }
    bool horizontal = false;
    for (uint32_t i = 0; i < cutscene.placeCount; i++) {
        const SCNCutscene_Place* place = &cutscene.places[i];
        horizontal |= place->character == VENDING_MACHINE && !place->angle && (place->known & 5) != 0;
    }
    CHECK(!horizontal, "act01_d000: cutscene 22, JIHN placed without the scene");
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
    uint32_t stopped = 0, placing = 0, placeCalls = 0, up = 0, levels = 0, displaying = 0, displayCalls = 0, before = 0;
    uint32_t attaching = 0, attachCalls = 0, packages = 0, facing = 0, faceCalls = 0, looks = 0, points = 0;
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
        if (strcmp(list.files[i].name, "scenario/act01_d000.scn") == 0) {
            CheckBus(&program);
            CheckPhoneBook(&program);
            CheckVendingMachine(&program);
        }
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
            if (!SCNCutscene_Run(&program, slot, NULL, CUTSCENE_FRAMES, NULL, &cutscene)) {
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
            for (uint32_t n = 1; n < cutscene.placeCount; n++)
                CHECK(cutscene.places[n].frame >= cutscene.places[n - 1].frame, "%s: cutscene %u: places not in order", list.files[i].name, slot);
            for (uint32_t n = 0; n < cutscene.placeCount; n++)
                CHECK(SD_IsCharacterId(cutscene.places[n].character) && cutscene.places[n].known != 0,
                    "%s: cutscene %u: place of 0x%08X at %u", list.files[i].name, slot, cutscene.places[n].character, cutscene.places[n].frame);
            placing += cutscene.placeCount > 0;
            placeCalls += cutscene.placeCount;
            up += cutscene.levels > 0;
            levels += cutscene.levels;
            displaying += cutscene.displayCount > 0;
            displayCalls += cutscene.displayCount;
            for (uint32_t n = 1; n < cutscene.displayCount; n++)
                CHECK(cutscene.displays[n].frame >= cutscene.displays[n - 1].frame, "%s: cutscene %u: displays not in order", list.files[i].name, slot);
            attaching += cutscene.attachCount > 0;
            attachCalls += cutscene.attachCount;
            packages += cutscene.packageCount;
            for (uint32_t n = 0; n < cutscene.attachCount; n++)
                CHECK(SD_IsCharacterId(cutscene.attaches[n].character) && (n == 0 || cutscene.attaches[n].frame >= cutscene.attaches[n - 1].frame),
                    "%s: cutscene %u: attach of 0x%08X at %u", list.files[i].name, slot, cutscene.attaches[n].character, cutscene.attaches[n].frame);
            facing += cutscene.faceCount > 0;
            faceCalls += cutscene.faceCount;
            uint32_t cutsceneLooks = 0, cutscenePoints = 0;
            for (uint32_t n = 0; n < cutscene.faceCount; n++) {
                const SCNCutscene_Face* face = &cutscene.faces[n];
                CHECK(SD_IsCharacterId(face->character) && (n == 0 || face->frame >= cutscene.faces[n - 1].frame),
                    "%s: cutscene %u: face of 0x%08X at %u", list.files[i].name, slot, face->character, face->frame);
                cutsceneLooks += face->function == SCNCUTSCENE_EYES;
                cutscenePoints += face->function == SCNCUTSCENE_EYES && (face->known & 8);
            }
            looks += cutsceneLooks;
            points += cutscenePoints;
            if (cutsceneLooks > 0)
                Test_Info("%s: cutscene %u: %u face calls, %u EyesLook (%u to a known point)", list.files[i].name, slot,
                    cutscene.faceCount, cutsceneLooks, cutscenePoints);
            for (uint32_t n = 0; n < cutscene.packageCount; n++)
                Test_Info("%s: cutscene %u: package %s%s for %.4s", list.files[i].name, slot, cutscene.packages[n].folder,
                    cutscene.packages[n].name, (const char*)&cutscene.packages[n].character);
            // The calls before the cutscene, at its frame 0, out of the function run with it
            int function = BC_FunctionAt(&program, cutscene.function);
            uint32_t end = function >= 0 ? BC_FunctionEnd(&program, (uint32_t)function) : 0;
            for (uint32_t n = 0; n < cutscene.placeCount; n++)
                before += cutscene.places[n].frame == 0 && (cutscene.places[n].address < cutscene.function || cutscene.places[n].address >= end);
            for (uint32_t n = 0; n < cutscene.displayCount; n++)
                before += cutscene.displays[n].frame == 0 && (cutscene.displays[n].address < cutscene.function || cutscene.displays[n].address >= end);
            if (cutscene.levels > 0)
                Test_Info("%s: cutscene %u: emulated from 0x%X, %u levels up, %u place and %u display calls", list.files[i].name, slot,
                    cutscene.start, cutscene.levels, cutscene.placeCount, cutscene.displayCount);
            if (cutscene.placeCount > 0)
                Test_Info("%s: cutscene %u: %u place calls (%s)", list.files[i].name, slot, cutscene.placeCount, PlacedText(&cutscene));
            if (cutscene.attachCount > 0)
                Test_Info("%s: cutscene %u: %u attach calls", list.files[i].name, slot, cutscene.attachCount);
            if (cutscene.nodeCount > 0)
                Test_Info("%s: cutscene %u: %u node calls (%s), %u sound calls, %u threads, %u frames", list.files[i].name, slot,
                    cutscene.nodeCount, NodesText(&cutscene), cutscene.soundCount, cutscene.run.threads, cutscene.run.frames);
            SCNCutscene_Free(&cutscene);
        }
        BC_FreeProgram(&program);
        free(data);
    }
    Corpus_Free(&list);
    Test_Info("%u programs, %u cutscenes played or loaded: %u found by the emulation (%u from functions up, %u levels), "
        "%u run, %u with node calls (%u calls), %u placing characters (%u calls), %u showing or hiding characters (%u calls), "
        "%u calls before the cutscene, %u attaching characters (%u calls), %u packages of characters, %u with sound commands (%u), "
        "%u threads stopped, %.1f s", programs, cutscenes, found, up, levels, started, moving, nodeCalls, placing, placeCalls,
        displaying, displayCalls, before, attaching, attachCalls, packages, sounding, soundCalls, stopped,
        (double)(clock() - start) / CLOCKS_PER_SEC);
    Test_Info("%u cutscenes with face calls (%u calls), %u EyesLook (%u to a known point)", facing, faceCalls, looks, points);
    CHECK_EQ(programs, 136, "SCN files");
    CHECK(found > 0 && started == found, "cutscenes found but not run");
    CHECK_EQ(stopped, 0, "threads stopped");
}

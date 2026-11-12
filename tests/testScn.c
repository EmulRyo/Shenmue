#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "test.h"

// The header, the code and the relocations of the scene programs: the SCN3 chunks of the mapinfo.bin files and the SCN
// files of scenario/. The expected values are those of the SCN page and the SCN3 section of the MAPINFO page of the wiki

// The values of the default fog (20.0, 800.0, 1.0, 0xFFFFFFFF), found in the data of most programs
static bool HasDefaultFog(const SCN_Program* program) {
    static const uint8_t fog[16] = {
        0x00, 0x00, 0xA0, 0x41, 0x00, 0x00, 0x48, 0x44, 0x00, 0x00, 0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF };
    const SCN_Header* header = &program->header;
    for (uint32_t pos = header->dataStart; pos + sizeof(fog) <= header->end; pos += 4) {
        if (memcmp(program->data + pos, fog, sizeof(fog)) == 0)
            return true;
    }
    return false;
}

static void CheckCommonHeader(const SCN_Program* program, const char* name) {
    const SCN_Header* header = &program->header;
    CHECK(header->codeStart == 0x30, "%s: codeStart 0x%X", name, header->codeStart);
    CHECK(header->unknown1C == 0, "%s: 0x1C is 0x%X", name, header->unknown1C);
    CHECK(header->unknown28 == 0 && header->unknown2C == 0, "%s: 0x28 is not 0", name);
}

// Relocations of a bytecode program: the type, the place of the value and the value itself
typedef struct {
    uint32_t    files;      // With at least one relocation
    uint32_t    total;
    uint32_t    code;       // SCN_RELOCATION_CODE
    uint32_t    data;       // SCN_RELOCATION_DATA
} RelocationStats;

static void CheckRelocations(const SCN_Program* program, const char* name, RelocationStats* stats) {
    const SCN_Header* header = &program->header;
    CHECK(program->relocationsEnd == header->size, "%s: relocations end at 0x%X, size 0x%X", name,
        program->relocationsEnd, header->size);
    stats->files += program->relocationCount > 0;
    stats->total += program->relocationCount;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < program->relocationCount; i++) {
        SCN_Relocation relocation = SCN_GetRelocation(program, i);
        CHECK(relocation.offset >= header->dataStart && relocation.offset + 4 <= header->end,
            "%s: relocation %u at 0x%X out of the data", name, i, relocation.offset);
        CHECK(i == 0 || relocation.offset > previous, "%s: relocation %u not sorted", name, i);
        previous = relocation.offset;
        if (relocation.offset + 4 > header->end)
            continue;
        uint32_t value = Bytes_ReadU32(program->data + relocation.offset);
        if (relocation.type == SCN_RELOCATION_CODE) {
            stats->code++;
            CHECK(value >= header->codeStart && value < header->dataStart,
                "%s: code relocation %u at 0x%X: value 0x%X out of the code", name, i, relocation.offset, value);
        } else if (relocation.type == SCN_RELOCATION_DATA) {
            stats->data++;
            CHECK(value >= header->dataStart && value < header->end,
                "%s: data relocation %u at 0x%X: value 0x%X out of the data", name, i, relocation.offset, value);
        } else {
            CHECK(false, "%s: relocation %u of unknown type 0x%X", name, i, relocation.type);
        }
    }
}

void Test_Scn3Chunks(const TestContext* context) {
    static const uint8_t prologue[8] = { 0x86, 0x2F, 0x96, 0x2F, 0xA6, 0x2F, 0xD6, 0x2F };
    CorpusList list = Corpus_MapinfoFiles(context->content);
    uint32_t sh4 = 0, bytecode = 0, bytecodeEnd = 0, fog = 0, zero18 = 0, max18 = 0;
    RelocationStats relocations = { 0 };
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        MAPINFO_File file;
        if (!MAPINFO_Load(list.files[i].path, &file)) {
            CHECK(false, "%s: can't be loaded", name);
            continue;
        }
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program program;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &program)) {
            CHECK(false, "%s: no valid SCN3 chunk", name);
            MAPINFO_Free(&file);
            continue;
        }
        const SCN_Header* header = &program.header;
        CHECK_EQ(header->size, chunk->size, "size of the program");
        CheckCommonHeader(&program, name);
        fog += HasDefaultFog(&program);
        zero18 += header->variableSize == 0;
        if (header->variableSize > max18)
            max18 = header->variableSize;
        if (SCN_IsSH4(&program)) {
            sh4++;
            CHECK(header->end == header->size, "%s: end 0x%X, size 0x%X", name, header->end, header->size);
            CHECK(header->end2 == 0, "%s: 0x24 is 0x%X", name, header->end2);
            CHECK(header->entry % 2 == 0 && Bytes_ReadU16(program.data + header->entry) == 0x2DE6,
                "%s: the entry point 0x%X doesn't start with mov.l r14,@-r13", name, header->entry);
            CHECK(memcmp(program.data + header->codeStart, prologue, sizeof(prologue)) == 0,
                "%s: the code doesn't start with the usual prologue", name);
        } else {
            bytecode++;
            bytecodeEnd += header->end == header->size - 8;
            CHECK(header->end2 == header->end, "%s: end2 0x%X, end 0x%X", name, header->end2, header->end);
            CheckRelocations(&program, name, &relocations);
            CHECK(program.relocationCount == (strcmp(list.files[i].map, "door") == 0 && list.files[i].disc == 3 ? 49 : 0),
                "%s: %u relocations", name, program.relocationCount);
        }
        MAPINFO_Free(&file);
    }
    CHECK_EQ(sh4, 119, "SH-4 programs");
    CHECK_EQ(bytecode, 17, "bytecode programs");
    CHECK_EQ(bytecodeEnd, 16, "bytecode programs with end = size - 8");
    CHECK_EQ(relocations.total, 49, "relocations");
    CHECK_EQ(fog, 129, "programs with the default fog in their data");
    CHECK_EQ(zero18, 11, "programs with 0 at 0x18");
    CHECK_EQ(max18, 0xF50, "maximum value at 0x18");
    Test_Info("%u SH-4, %u bytecode, %u with the default fog, value at 0x18 up to 0x%X", sh4, bytecode, fog, max18);
    Corpus_Free(&list);
}

void Test_ScnFiles(const TestContext* context) {
    CorpusList list = Corpus_ScnFiles(context->content);
    CHECK_EQ(list.count, 136, "SCN files");
    uint32_t discs[4] = { 0 }, odd = 0, entryStart = 0, noRelocations = 0, fog = 0, zero18 = 0, max18 = 0;
    uint32_t minSize = UINT32_MAX, maxSize = 0;
    RelocationStats relocations = { 0 };
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        discs[list.files[i].disc <= 3 ? list.files[i].disc : 0]++;
        uint32_t size;
        uint8_t* data = Corpus_Load(&list.files[i], &size);
        SCN_Program program;
        if (data == NULL || !SCN_Read(data, size, &program)) {
            CHECK(false, "%s: not a valid SCN file", name);
            free(data);
            continue;
        }
        const SCN_Header* header = &program.header;
        if (size < minSize)
            minSize = size;
        if (size > maxSize)
            maxSize = size;
        CHECK(header->size == size, "%s: size 0x%X, file size 0x%X", name, header->size, size);
        CHECK(header->version == SCN_VERSION_BYTECODE, "%s: version 0x%X", name, header->version);
        CHECK(header->end2 == header->end, "%s: end2 0x%X, end 0x%X", name, header->end2, header->end);
        CheckCommonHeader(&program, name);
        CheckRelocations(&program, name, &relocations);
        odd += header->entry % 2;
        entryStart += header->entry == header->codeStart;
        noRelocations += header->end == size - 8;
        CHECK((header->end == size - 8) == (program.relocationCount == 0), "%s: end 0x%X with %u relocations", name,
            header->end, program.relocationCount);
        fog += HasDefaultFog(&program);
        zero18 += header->variableSize == 0;
        if (header->variableSize > max18)
            max18 = header->variableSize;
        free(data);
    }
    CHECK_EQ(discs[1], 44, "act01 files");
    CHECK_EQ(discs[2], 42, "act02 files");
    CHECK_EQ(discs[3], 50, "act03 files");
    CHECK_EQ(minSize, 144, "smallest file");
    CHECK_EQ(maxSize, 416616, "biggest file");
    CHECK_EQ(odd, 75, "files with an odd entry point");
    CHECK_EQ(entryStart, 3, "files with the entry point at the start of the code");
    CHECK_EQ(noRelocations, 39, "files without relocations");
    CHECK_EQ(relocations.files, 97, "files with relocations");
    CHECK_EQ(relocations.total, 4265, "relocations");
    CHECK_EQ(relocations.code, 3797, "relocations of type 0x2402");
    CHECK_EQ(relocations.data, 468, "relocations of type 0x1402");
    CHECK_EQ(fog, 129, "files with the default fog in their data");
    CHECK_EQ(zero18, 11, "files with 0 at 0x18");
    CHECK_EQ(max18, 0xF50, "maximum value at 0x18");
    Test_Info("%u files, %u to %u bytes, %u relocations in %u files (0x2402: %u, 0x1402: %u)", list.count, minSize,
        maxSize, relocations.total, relocations.files, relocations.code, relocations.data);
    Corpus_Free(&list);
}

typedef struct {
    uint8_t*    data;
    uint32_t    size;
} Blob;

static uint32_t CountDistinct(const Blob* blobs, uint32_t count) {
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < count; i++) {
        bool repeated = false;
        for (uint32_t j = 0; j < i && !repeated; j++) {
            repeated = blobs[i].data != NULL && blobs[j].data != NULL && blobs[i].size == blobs[j].size &&
                memcmp(blobs[i].data, blobs[j].data, blobs[i].size) == 0;
        }
        distinct += !repeated;
    }
    return distinct;
}

// Each map has its SCN file, actNN_<map>.scn for scene/NN/<map>/mapinfo.bin, and the bytecode chunks are (almost) the
// same as their file
void Test_ScnPairs(const TestContext* context) {
    static const char* const different[] = {
        "scenario/act01_jd99.scn", "scenario/act01_ju99.scn", "scenario/act01_yq14.scn", "scenario/act02_arar.scn" };
    CorpusList maps = Corpus_MapinfoFiles(context->content);
    CorpusList scns = Corpus_ScnFiles(context->content);
    Blob* mapBlobs = calloc(maps.count + 1, sizeof(Blob));
    Blob* scnBlobs = calloc(scns.count + 1, sizeof(Blob));
    uint32_t pairs = 0, identical = 0, differentCount = 0;
    for (uint32_t i = 0; i < maps.count && mapBlobs != NULL && scnBlobs != NULL; i++)
        mapBlobs[i].data = Corpus_Load(&maps.files[i], &mapBlobs[i].size);
    for (uint32_t i = 0; i < scns.count && mapBlobs != NULL && scnBlobs != NULL; i++)
        scnBlobs[i].data = Corpus_Load(&scns.files[i], &scnBlobs[i].size);
    for (uint32_t i = 0; i < maps.count && mapBlobs != NULL && scnBlobs != NULL; i++) {
        const CorpusFile* map = &maps.files[i];
        uint32_t s = 0;
        while (s < scns.count && (scns.files[s].disc != map->disc || strcmp(scns.files[s].map, map->map) != 0))
            s++;
        if (s == scns.count) {
            CHECK(false, "%s: no SCN file", map->name);
            continue;
        }
        pairs++;
        MAPINFO_File file;
        if (!MAPINFO_Load(map->path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        if (chunk != NULL && file.scnVersion == SCN_VERSION_BYTECODE) {
            bool same = chunk->size == scnBlobs[s].size &&
                memcmp(file.data + chunk->offset, scnBlobs[s].data, chunk->size) == 0;
            bool expectedDifferent = false;
            for (size_t d = 0; d < sizeof(different) / sizeof(different[0]); d++)
                expectedDifferent |= strcmp(scns.files[s].name, different[d]) == 0;
            CHECK(same != expectedDifferent, "%s: the SCN3 chunk is %s its SCN file", map->name,
                same ? "identical to" : "different from");
            identical += same;
            differentCount += !same;
        }
        MAPINFO_Free(&file);
    }
    CHECK_EQ(pairs, 136, "maps with their SCN file");
    CHECK_EQ(identical, 13, "bytecode chunks identical to their SCN file");
    CHECK_EQ(differentCount, 4, "bytecode chunks different from their SCN file");
    uint32_t distinctMaps = mapBlobs != NULL ? CountDistinct(mapBlobs, maps.count) : 0;
    uint32_t distinctScns = scnBlobs != NULL ? CountDistinct(scnBlobs, scns.count) : 0;
    CHECK_EQ(distinctMaps, 134, "different mapinfo.bin files");
    CHECK_EQ(distinctScns, 133, "different SCN files");
    Test_Info("%u pairs, %u identical bytecode chunks, %u different mapinfo.bin and %u different SCN files", pairs,
        identical, distinctMaps, distinctScns);
    for (uint32_t i = 0; mapBlobs != NULL && i < maps.count; i++)
        free(mapBlobs[i].data);
    for (uint32_t i = 0; scnBlobs != NULL && i < scns.count; i++)
        free(scnBlobs[i].data);
    free(mapBlobs);
    free(scnBlobs);
    Corpus_Free(&maps);
    Corpus_Free(&scns);
}

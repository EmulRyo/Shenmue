#include <stdio.h>
#include <string.h>
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "test.h"

// The list of chunks of the mapinfo.bin files: the table of chunks of the MAPINFO page of the wiki

typedef struct {
    const char* id;
    uint32_t    files;  // Number of files that contain the chunk
    uint32_t    found;
} ChunkCount;

void Test_MapinfoChunks(const TestContext* context) {
    CorpusList list = Corpus_MapinfoFiles(context->content);
    CHECK_EQ(list.count, 136, "mapinfo.bin files");
    uint32_t discs[4] = { 0 };
    for (uint32_t i = 0; i < list.count; i++)
        discs[list.files[i].disc <= 3 ? list.files[i].disc : 0]++;
    CHECK_EQ(discs[1], 44, "files in scene/01");
    CHECK_EQ(discs[2], 42, "files in scene/02");
    CHECK_EQ(discs[3], 50, "files in scene/03");

    ChunkCount chunks[] = {
        { "ATTR", 136 }, { "SCN3", 136 }, { "REGD", 136 }, { "CHRD", 136 }, { "END\0", 136 }, { "ECAM", 135 },
        { "MAPR", 135 }, { "LGHT", 122 }, { "COLS", 100 }, { "SNDD", 100 }, { "MAPT", 93 },  { "LSCN", 86 },
        { "DIRT", 86 },  { "FOG ", 78 },  { "SCRL", 62 },  { "SCEX", 22 },  { "CMPS", 17 },  { "SCOF", 12 },
        { "DYNA", 10 },  { "MAPD", 6 },   { "SCNF", 3 },   { "SCLM", 2 },   { "MVTR", 1 },   { "HGML", 1 },
        { "BLKS", 1 },
    };
    const uint32_t chunkTypes = sizeof(chunks) / sizeof(chunks[0]);
    uint32_t firstATTR = 0, firstLGHT = 0, firstDYNA = 0, loaded = 0, unknownChunks = 0;
    uint32_t minSize = UINT32_MAX, maxSize = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        MAPINFO_File file;
        if (!MAPINFO_Load(list.files[i].path, &file)) {
            CHECK(false, "%s: can't be loaded", list.files[i].name);
            continue;
        }
        loaded++;
        if (file.size < minSize)
            minSize = (uint32_t)file.size;
        if (file.size > maxSize)
            maxSize = (uint32_t)file.size;
        const char* first = file.chunks[0].id;
        firstATTR += strcmp(first, "ATTR") == 0;
        firstLGHT += strcmp(first, "LGHT") == 0;
        firstDYNA += strcmp(first, "DYNA") == 0;
        CHECK(memcmp(file.chunks[file.chunkCount - 1].id, "END\0", 4) == 0, "%s: the last chunk isn't END",
            list.files[i].name);
        CHECK_EQ(file.chunks[file.chunkCount - 1].size, 8, "size of the END chunk");
        for (uint32_t c = 0; c < chunkTypes; c++) {
            if (MAPINFO_FindChunk(&file, chunks[c].id) != NULL)
                chunks[c].found++;
        }
        for (uint32_t k = 0; k < file.chunkCount; k++) {
            bool known = false;
            for (uint32_t c = 0; c < chunkTypes && !known; c++)
                known = memcmp(file.chunks[k].id, chunks[c].id, 4) == 0;
            if (!known) {
                CHECK(false, "%s: unknown chunk %s at 0x%X", list.files[i].name, file.chunks[k].id,
                    file.chunks[k].offset);
                unknownChunks++;
            }
        }
        MAPINFO_Free(&file);
    }
    CHECK_EQ(loaded, 136, "loaded files");
    CHECK_EQ(unknownChunks, 0, "chunks not in the table");
    CHECK_EQ(minSize, 452, "smallest file");
    CHECK_EQ(maxSize, 1053576, "biggest file");
    CHECK_EQ(firstATTR, 120, "files that start with ATTR");
    CHECK_EQ(firstLGHT, 14, "files that start with LGHT");
    CHECK_EQ(firstDYNA, 2, "files that start with DYNA");
    for (uint32_t c = 0; c < chunkTypes; c++) {
        char what[32];
        snprintf(what, sizeof(what), "files with %.4s", chunks[c].id[0] == 'E' && chunks[c].id[3] == '\0' ? "END" :
            chunks[c].id);
        CHECK_EQ(chunks[c].found, chunks[c].files, what);
    }
    Test_Info("%u files (%u, %u, %u), %u to %u bytes", list.count, discs[1], discs[2], discs[3], minSize, maxSize);
    Corpus_Free(&list);
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/animation/talk.h"
#include "formats/archive/afs.h"
#include "formats/model/ftbl.h"
#include "formats/model/mt5.h"
#include "formats/sound/lipsync.h"
#include "test.h"

// The faces: the FTBL skin of every face model of the model/face folders, and the sets of poses of talk_n00.bin. The
// pose 0 of every set is the face at rest: the end of the chain of each bone (TALK_BoneOffsets) is its tip, within the
// precision of the 16-bit floats, for every face of the set. The skin has a count for each vertex of the face mesh

#define FACES_REST_TOLERANCE    0.0005f // Meters
#define FACES_SCENES            3

static void CheckFace(const char* folder, const char* skinPath, const TALK_File* talk, uint32_t* faces, uint32_t* vertices) {
    FTBL_Skin skin;
    CHECK(FTBL_Load(skinPath, &skin), "%s: invalid FTBL file", skinPath);
    if (skin.vertexCount == 0)
        return;
    (*faces)++;
    CHECK(skin.talkSet >= 0 && (uint32_t)skin.talkSet < talk->setCount, "%s: talk set %d", skinPath, skin.talkSet);
    for (int b = 0; b < FTBL_BONES; b++)
        CHECK(skin.bones[b].type == 2, "%s: bone %d of type %d", skinPath, b, skin.bones[b].type);
    if (skin.talkSet >= 0 && (uint32_t)skin.talkSet < talk->setCount) {
        float offsets[FTBL_BONES][3] = { 0 };
        const TALK_Set* set = &talk->sets[skin.talkSet];
        CHECK(TALK_BoneOffsets(set, &skin, TALK_LAYER_EYES, 0, offsets) && TALK_BoneOffsets(set, &skin, TALK_LAYER_MOUTH, 0, offsets),
            "%s: no bone in a part of the face", skinPath);
        float worst = 0.0f;
        for (int b = 0; b < FTBL_BONES; b++)
            worst = fmaxf(worst, sqrtf(offsets[b][0] * offsets[b][0] + offsets[b][1] * offsets[b][1] + offsets[b][2] * offsets[b][2]));
        CHECK(worst < FACES_REST_TOLERANCE, "%s: the pose 0 moves a bone %.4f", skinPath, worst);
    }
    // The model with the same three letters: at most two counts more than the vertices of its root (mgr and fro differ)
    char modelPath[1024];
    snprintf(modelPath, sizeof(modelPath), "%s/%.3s_f.mt5", folder, GetFileName(skinPath));
    MT5_Model model;
    if (FileExists(modelPath) && MT5_Load(modelPath, &model)) {
        bool known = strncmp(GetFileName(skinPath), "mgr", 3) == 0 || strncmp(GetFileName(skinPath), "fro", 3) == 0;
        uint32_t meshVertices = model.nodeCount > 0 ? model.nodes[0].vertexCount : 0;
        CHECK(known || (skin.vertexCount >= meshVertices && skin.vertexCount <= meshVertices + 2), "%s: %u counts for %u vertices",
            skinPath, skin.vertexCount, meshVertices);
        *vertices += meshVertices;
        MT5_Free(&model);
    }
    FTBL_Free(&skin);
}

void Test_Faces(const TestContext* context) {
    uint32_t folders = 0, faces = 0, vertices = 0, sets = 0;
    for (int scene = 1; scene <= FACES_SCENES; scene++) {
        char folder[1024], talkPath[1024];
        snprintf(folder, sizeof(folder), "%s/scene/%02d/model/face", context->content, scene);
        snprintf(talkPath, sizeof(talkPath), "%s/talk_n00.bin", folder);
        TALK_File talk;
        if (!DirectoryExists(folder) || !FileExists(talkPath))
            continue;
        folders++;
        CHECK(TALK_Load(talkPath, &talk), "%s: invalid TALK file", talkPath);
        CHECK_EQ(talk.setCount, 4, "sets of talk_n00.bin");
        sets += talk.setCount;
        FilePathList files = LoadDirectoryFilesEx(folder, ".bin", false);
        for (unsigned int i = 0; i < files.count; i++) {
            const char* name = GetFileName(files.paths[i]);
            size_t length = strlen(name);
            if (length > 9 && TextIsEqual(TextToLower(name + length - 9), "_ftbl.bin"))
                CheckFace(folder, files.paths[i], &talk, &faces, &vertices);
        }
        UnloadDirectoryFiles(files);
        TALK_Free(&talk);
    }
    if (folders == 0) {
        Test_Info("No model/face folder");
        return;
    }
    Test_Info("%u face folders, %u TALK sets, %u FTBL files (%u vertices)", folders, sets, faces, vertices);
}

// The lip-sync records of the voice clips: the IDX0 file of every voice AFS gives the table and the offset of the record
// of each clip, which has its keys (the shapes of the mouth 0 to 5)
typedef struct {
    uint32_t    records;
    uint32_t    keys;
    uint32_t    invalid;
    uint32_t    badMouths;
} LipSyncCounts;

static void CheckVoices(const char* afsPath, const char* idxPath, LipSyncCounts* counts) {
    size_t size = 0;
    uint8_t* idx = File_LoadToMemoryEx(idxPath, &size);
    if (idx == NULL || size < 0x14 || memcmp(idx, "IDX0", 4) != 0) {
        free(idx);
        return;
    }
    uint32_t count = Bytes_ReadU16(idx + 6), loadedTable = UINT32_MAX;
    uint8_t* table = NULL;
    size_t tableSize = 0;
    for (uint32_t i = 0; i < count && 0x14 + (i + 1) * 20 <= size; i++) {
        const uint8_t* entry = idx + 0x14 + i * 20;
        uint32_t tableIndex = Bytes_ReadU16(entry + 0x0E), offset = Bytes_ReadU32(entry + 0x10);
        if (tableIndex == 0xFFFF || offset == 0xFFFFFFFF)
            continue;
        if (tableIndex != loadedTable) {
            free(table);
            uint32_t entries;
            table = AFS_LoadEntry(afsPath, tableIndex, &tableSize, &entries);
            loadedTable = tableIndex;
        }
        LIPSYNC_Record record;
        if (table == NULL || !LIPSYNC_Read(table, tableSize, offset, &record)) {
            counts->invalid++;
            continue;
        }
        counts->records++;
        counts->keys += record.keyCount;
        for (uint32_t k = 0; k < record.keyCount; k++)
            counts->badMouths += record.keys[k].mouth > 5;
        LIPSYNC_Free(&record);
    }
    free(table);
    free(idx);
}

void Test_LipSync(const TestContext* context) {
    const char* languages[2] = { "eng", "jap" };
    LipSyncCounts counts = { 0 };
    uint32_t files = 0;
    for (int scene = 1; scene <= FACES_SCENES; scene++) {
        for (int l = 0; l < 2; l++) {
            char folder[1024];
            snprintf(folder, sizeof(folder), "%s/scene/%02d/stream/pc/%s", context->content, scene, languages[l]);
            if (!DirectoryExists(folder))
                continue;
            FilePathList list = LoadDirectoryFilesEx(folder, ".afs", false);
            for (unsigned int i = 0; i < list.count; i++) {
                char idxPath[1024];
                snprintf(idxPath, sizeof(idxPath), "%.*s.idx", (int)strlen(list.paths[i]) - 4, list.paths[i]);
                if (!FileExists(idxPath))
                    continue;
                files++;
                CheckVoices(list.paths[i], idxPath, &counts);
            }
            UnloadDirectoryFiles(list);
        }
    }
    if (files == 0) {
        Test_Info("No voice AFS file");
        return;
    }
    CHECK_EQ(counts.invalid, 0, "invalid lip-sync records");
    CHECK_EQ(counts.badMouths, 0, "keys with a mouth above 5");
    Test_Info("%u voice AFS files, %u lip-sync records, %u keys", files, counts.records, counts.keys);
}

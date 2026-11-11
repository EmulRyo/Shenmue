#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/sh4Program.h"
#include "test.h"

// The database of the functions of the game called by the scene programs (resources/Shenmue1GameFunctions.tsv): one row
// per ID of the table of 1ST_READ.BIN, every ID called by the SH-4 programs has the code of its function, and the
// numbers of arguments of its calls are those of the database

#define GAME_FUNCTIONS  0x1CD   // IDs 0 to 0x1CC
#define LINE_SIZE       1024

typedef struct {
    bool    present;
    bool    handler;
    char    arguments[64];
    char    name[64];
} Row;

// "1-5, 17" contains count
static bool ContainsCount(const char* list, int count) {
    for (const char* p = list; *p != '\0';) {
        char* end;
        long first = strtol(p, &end, 10);
        if (end == p)
            return false;
        long last = first;
        if (*end == '-')
            last = strtol(end + 1, &end, 10);
        if (count >= first && count <= last)
            return true;
        p = end;
        while (*p == ',' || *p == ' ')
            p++;
    }
    return false;
}

static bool IsIdentifier(const char* text) {
    if (!isalpha((unsigned char)text[0]))
        return false;
    for (; *text != '\0'; text++) {
        if (!isalnum((unsigned char)*text) && *text != '_')
            return false;
    }
    return true;
}

// Reads the rows of the file. Returns their number, -1 if it can't be read
static int ReadRows(const char* path, Row* rows) {
    FILE* file = fopen(path, "r");
    if (file == NULL)
        return -1;
    char line[LINE_SIZE];
    int count = 0, expected = 0;
    bool header = true;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (header || line[0] == '\0') {
            header = false;
            continue;
        }
        char* cells[5] = { 0 };
        char* cell = line;
        for (int c = 0; c < 5 && cell != NULL; c++) {
            cells[c] = cell;
            char* tab = strchr(cell, '\t');
            if (tab != NULL)
                *tab = '\0';
            cell = tab != NULL ? tab + 1 : NULL;
        }
        int id = (int)strtol(cells[0], NULL, 16);
        CHECK(id == expected, "row %d: ID 0x%X, expected 0x%X", count, id, expected);
        expected = id + 1;
        count++;
        if (id < 0 || id >= GAME_FUNCTIONS)
            continue;
        Row* row = &rows[id];
        row->present = true;
        row->handler = cells[1] != NULL && strncmp(cells[1], "0x0C", 4) == 0 && strlen(cells[1]) == 10;
        snprintf(row->arguments, sizeof(row->arguments), "%s", cells[2] != NULL ? cells[2] : "");
        snprintf(row->name, sizeof(row->name), "%s", cells[3] != NULL ? cells[3] : "");
        CHECK(row->name[0] == '\0' || IsIdentifier(row->name), "ID 0x%X: invalid name \"%s\"", id, row->name);
    }
    fclose(file);
    return count;
}

void Test_GameFunctions(const TestContext* context) {
    Row* rows = calloc(GAME_FUNCTIONS, sizeof(Row));
    if (rows == NULL)
        return;
    int count = ReadRows(SHENMUE_RESOURCES "/Shenmue1GameFunctions.tsv", rows);
    CHECK_EQ(count, GAME_FUNCTIONS, "rows of the database");
    uint32_t handlers = 0, named = 0, duplicates = 0;
    for (int i = 0; i < GAME_FUNCTIONS; i++) {
        handlers += rows[i].handler;
        named += rows[i].name[0] != '\0';
        for (int j = 0; j < i && rows[i].name[0] != '\0'; j++)
            duplicates += strcmp(rows[i].name, rows[j].name) == 0;
    }
    // The table has no function for 0x76, 0x77 and 0x125, which no program calls
    CHECK_EQ(handlers, GAME_FUNCTIONS - 3, "IDs with the code of their function");
    CHECK_EQ(duplicates, 0, "repeated names");

    CorpusList list = Corpus_MapinfoFiles(context->content);
    uint64_t calls = 0, namedCalls = 0, withoutHandler = 0, otherCount = 0;
    bool used[GAME_FUNCTIONS] = { false };
    for (uint32_t i = 0; i < list.count; i++) {
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
        for (uint32_t c = 0; c < program.callCount; c++) {
            const SH4_Call* call = &program.calls[c];
            if (call->kind != SH4_CALL_GAME)
                continue;
            calls++;
            if (call->id < 0 || call->id >= GAME_FUNCTIONS || !rows[call->id].handler) {
                withoutHandler++;
                continue;
            }
            used[call->id] = true;
            namedCalls += rows[call->id].name[0] != '\0';
            if (call->argumentBytes >= 0 && !ContainsCount(rows[call->id].arguments, call->argumentBytes / 4)) {
                if (otherCount < 10)
                    CHECK(false, "%s: 0x%X: game function 0x%X with %d arguments, not in \"%s\"", list.files[i].name,
                        call->address, call->id, call->argumentBytes / 4, rows[call->id].arguments);
                otherCount++;
            }
        }
        SH4_FreeProgram(&program);
        MAPINFO_Free(&file);
    }
    uint32_t usedCount = 0, usedNamed = 0;
    for (int i = 0; i < GAME_FUNCTIONS; i++) {
        usedCount += used[i];
        usedNamed += used[i] && rows[i].name[0] != '\0';
        CHECK(used[i] || rows[i].arguments[0] == '\0', "ID 0x%X: arguments \"%s\" but no call", i, rows[i].arguments);
    }
    CHECK_EQ(withoutHandler, 0, "calls to an ID without code");
    CHECK_EQ(otherCount, 0, "calls with a number of arguments not in the database");
    Test_Info("%d IDs, %u with code, %u named; the programs call %u of them (%u named)", count, handlers, named,
        usedCount, usedNamed);
    Test_Info("%llu calls to the game, %llu to a named function (%.1f%%)", (unsigned long long)calls,
        (unsigned long long)namedCalls, calls ? 100.0 * namedCalls / (double)calls : 0.0);
    free(rows);
    Corpus_Free(&list);
}

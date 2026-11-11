#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formats/script/chrt.h"
#include "app/gameDB.h"

// resources/Shenmue1Characters.tsv and resources/Shenmue1Maps.tsv, embedded by the build (see embed_resource in
// CMakeLists.txt)
extern const unsigned char g_shenmue1Characters[];
extern const size_t g_shenmue1CharactersSize;
extern const unsigned char g_shenmue1Maps[];
extern const size_t g_shenmue1MapsSize;
extern const unsigned char g_shenmue1GameFunctions[];
extern const size_t g_shenmue1GameFunctionsSize;

#define GAMEDB_MAX_COLUMNS  16
#define GAMEDB_BUFFERS      8   // Strings returned by GameDB_CharacterLabel at the same time
#define GAMEDB_LABEL_SIZE   96

// The rows of a table, after the header: the columns of each one point into a copy of the file, with their tabs and
// line ends replaced by '\0'
typedef struct {
    char*           text;
    size_t          count;
    const char**    cells;      // count * columns
    int             columns;
} Table;

static bool g_loaded;
static size_t g_characterCount, g_mapCount, g_gameFunctionCount;
static GameDB_Character* g_characters;
static GameDB_Map* g_maps;
static GameDB_GameFunction* g_gameFunctions;

// Splits the text in rows of columns cells, the missing ones empty. Returns false on memory error
static bool ReadTable(const unsigned char* data, size_t size, int columns, Table* table) {
    memset(table, 0, sizeof(*table));
    table->columns = columns;
    table->text = malloc(size + 1);
    if (table->text == NULL)
        return false;
    memcpy(table->text, data, size);
    table->text[size] = '\0';
    size_t lines = 1;
    for (size_t i = 0; i < size; i++)
        lines += table->text[i] == '\n';
    table->cells = calloc(lines * columns + 1, sizeof(const char*));
    if (table->cells == NULL)
        return false;
    char* line = table->text;
    bool header = true;
    while (*line != '\0') {
        char* end = strchr(line, '\n');
        char* next = end != NULL ? end + 1 : line + strlen(line);
        if (end != NULL)
            *end = '\0';
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\r') // The checkout can have CRLF
            line[--length] = '\0';
        if (!header && length > 0) {
            const char** cells = &table->cells[table->count++ * columns];
            char* cell = line;
            for (int c = 0; c < columns; c++) {
                cells[c] = cell;
                char* tab = cell != NULL ? strchr(cell, '\t') : NULL;
                if (tab != NULL)
                    *tab = '\0';
                cell = tab != NULL ? tab + 1 : NULL;
                if (cells[c] == NULL)
                    cells[c] = "";
            }
        }
        header = false;
        line = next;
    }
    return true;
}

// id, model, name, jpname, area, place, maps, scenes, wiki
static void LoadCharacters(void) {
    Table table;
    if (!ReadTable(g_shenmue1Characters, g_shenmue1CharactersSize, 9, &table) ||
        (g_characters = calloc(table.count + 1, sizeof(GameDB_Character))) == NULL) {
        fprintf(stderr, "[GameDB]: Out of memory\n");
        return;
    }
    for (size_t i = 0; i < table.count; i++) {
        const char** cells = &table.cells[i * table.columns];
        g_characters[g_characterCount++] = (GameDB_Character) { cells[0], cells[1], cells[2], cells[4], cells[5] };
    }
}

// id, ipac, scenes, name, area, notes
static void LoadMaps(void) {
    Table table;
    if (!ReadTable(g_shenmue1Maps, g_shenmue1MapsSize, 6, &table) || (g_maps = calloc(table.count + 1, sizeof(GameDB_Map))) == NULL) {
        fprintf(stderr, "[GameDB]: Out of memory\n");
        return;
    }
    for (size_t i = 0; i < table.count; i++) {
        const char** cells = &table.cells[i * table.columns];
        g_maps[g_mapCount++] = (GameDB_Map) { cells[0], cells[1], cells[3], cells[4] };
    }
}

// id (hexadecimal), handler, arguments, name, description
static void LoadGameFunctions(void) {
    Table table;
    if (!ReadTable(g_shenmue1GameFunctions, g_shenmue1GameFunctionsSize, 5, &table) ||
        (g_gameFunctions = calloc(table.count + 1, sizeof(GameDB_GameFunction))) == NULL) {
        fprintf(stderr, "[GameDB]: Out of memory\n");
        return;
    }
    for (size_t i = 0; i < table.count; i++) {
        const char** cells = &table.cells[i * table.columns];
        g_gameFunctions[g_gameFunctionCount++] = (GameDB_GameFunction) { (int)strtol(cells[0], NULL, 16), cells[1],
            cells[2], cells[3], cells[4] };
    }
}

static void Load(void) {
    if (g_loaded)
        return;
    g_loaded = true;
    LoadCharacters();
    LoadMaps();
    LoadGameFunctions();
}

const GameDB_GameFunction* GameDB_FindGameFunction(int id) {
    Load();
    for (size_t i = 0; i < g_gameFunctionCount; i++) {
        if (g_gameFunctions[i].id == id)
            return &g_gameFunctions[i];
    }
    return NULL;
}

const char* GameDB_GameFunctionName(int id) {
    const GameDB_GameFunction* function = GameDB_FindGameFunction(id);
    return function != NULL && function->name[0] != '\0' ? function->name : NULL;
}

const GameDB_Character* GameDB_FindCharacter(const char* id) {
    Load();
    if (id == NULL || id[0] == '\0')
        return NULL;
    for (size_t i = 0; i < g_characterCount; i++) {
        if (CHRT_EqualsIgnoreCase(g_characters[i].id, id))
            return &g_characters[i];
    }
    return NULL;
}

const char* GameDB_CharacterName(const char* id) {
    const GameDB_Character* character = GameDB_FindCharacter(id);
    return character != NULL && character->name[0] != '\0' ? character->name : NULL;
}

const char* GameDB_CharacterDisplay(const char* id) {
    const char* name = GameDB_CharacterName(id);
    return name != NULL ? name : id;
}

const char* GameDB_CharacterLabel(const char* id) {
    static char buffers[GAMEDB_BUFFERS][GAMEDB_LABEL_SIZE];
    static int next;
    char* buffer = buffers[next];
    next = (next + 1) % GAMEDB_BUFFERS;
    const char* name = GameDB_CharacterName(id);
    if (name != NULL)
        snprintf(buffer, GAMEDB_LABEL_SIZE, "%s  %s", id, name);
    else
        snprintf(buffer, GAMEDB_LABEL_SIZE, "%s", id);
    return buffer;
}

const char* GameDB_CharacterPlace(const char* id) {
    static char buffer[GAMEDB_LABEL_SIZE * 2];
    const GameDB_Character* character = GameDB_FindCharacter(id);
    if (character == NULL || (character->area[0] == '\0' && character->place[0] == '\0'))
        return NULL;
    snprintf(buffer, sizeof(buffer), "%s%s%s", character->area, character->area[0] != '\0' && character->place[0] != '\0' ? ", " : "",
        character->place);
    return buffer;
}

void GameDB_CharacterProperties(Properties* properties, const char* id) {
    const char* name = GameDB_CharacterName(id);
    const char* place = GameDB_CharacterPlace(id);
    if (name != NULL)
        Properties_Text(properties, "Name", name);
    if (place != NULL)
        Properties_Text(properties, "Location", place);
}

const GameDB_Map* GameDB_FindMap(const char* id, const char* ipac) {
    Load();
    if (id == NULL || id[0] == '\0')
        return NULL;
    if (ipac == NULL)
        ipac = "";
    for (size_t i = 0; i < g_mapCount; i++) {
        if (CHRT_EqualsIgnoreCase(g_maps[i].id, id) && CHRT_EqualsIgnoreCase(g_maps[i].ipac, ipac))
            return &g_maps[i];
    }
    return NULL;
}

const GameDB_Map* GameDB_MapOfFile(const char* path) {
    // The names of the folder and of its parent
    char folders[2][64] = { "", "" };
    const char* end = path + strlen(path);
    for (int f = -1; f < 2 && end > path; f++) {
        const char* start = end;
        while (start > path && start[-1] != '/' && start[-1] != '\\')
            start--;
        if (f >= 0)
            snprintf(folders[f], sizeof(folders[f]), "%.*s", (int)(end - start), start);
        end = start > path ? start - 1 : path;
    }
    const GameDB_Map* map = GameDB_FindMap(folders[1], folders[0]);
    return map != NULL ? map : GameDB_FindMap(folders[0], NULL);
}

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <nfd.h>
#include <raylib.h>
#include "raygui.h"
#include "core/bytes.h"
#include "app/gameDB.h"
#include "core/file.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/scnBytecode.h"
#include "formats/script/scnPair.h"
#include "formats/script/scnData.h"
#include "formats/script/scnDecompile.h"
#include "formats/script/sh4Listing.h"
#include "formats/script/sh4Program.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/mapProgram.h"
#include "views/mapinfoView.h"
#include "views/programView.h"

#define PROGRAMVIEW_LIST_WIDTH  360.0f
#define PROGRAMVIEW_LABEL_SIZE  96
#define PROGRAMVIEW_HISTORY     64
#define PROGRAMVIEW_MARGIN      8.0f

// Groups of the tree
typedef enum {
    GROUP_FUNCTIONS,
    GROUP_DATA,         // What the program does with the map (scnData.h): packages, cutscenes, characters...
    GROUP_GAME,         // IDs of the functions of the game called by the program
    GROUP_STRINGS,      // Strings of the data loaded by the code
    GROUP_RELOCATIONS,  // Of the bytecode programs
    GROUP_COUNT,
} Group;

static const char* g_groupNames[GROUP_COUNT] = { "Functions", "Program data", "Game functions", "Strings", "Relocations" };

typedef struct {
    int32_t     id;
    uint32_t    calls;
    int32_t     minArguments;   // -1 if unknown
    int32_t     maxArguments;
} GameFunction;

typedef struct {
    uint32_t    offset;     // In the data
    uint32_t    loads;      // Instructions that load it
} DataString;

typedef struct {
    bool            open;
    bool            fromMap;    // The SCN3 chunk of a mapinfo.bin, opened from the view of the map
    char            path[1024];
    uint8_t*        fileData;   // .scn file
    MAPINFO_File    mapinfo;    // mapinfo.bin
    SCN_Program     scn;
    bool            sh4;        // Analyzed SH-4 code: program is valid
    SH4_Program     program;
    bool            bytecode;   // Analyzed bytecode: bc is valid
    BC_Program      bc;
    // The bytecode of the SCN file of the map of an SH-4 program, paired with it
    bool            paired;
    char            pairName[64];   // e.g. "act01_d000.scn"
    uint8_t*        pairData;
    SCN_Program     pairScn;
    BC_Program      pairBc;
    SCN_Pair        pair;
    // Pseudo-C of the function shown, from the bytecode (of the program, or of its pair)
    DC_Function     pseudo;
    int32_t         pseudoFunction; // Function decompiled, -1 if none
    char            (*functionLabels)[PROGRAMVIEW_LABEL_SIZE];
    uint32_t        gameCount;
    GameFunction*   games;
    char            (*gameLabels)[PROGRAMVIEW_LABEL_SIZE];
    uint32_t        stringCount;
    DataString*     strings;
    char            (*stringLabels)[PROGRAMVIEW_LABEL_SIZE];
    char            (*relocationLabels)[PROGRAMVIEW_LABEL_SIZE];
    // The references of the data of the program, and the distinct ones (their first reference), sorted by kind
    SD_Data         data;
    uint32_t        dataCount;
    uint32_t*       dataItems;
    char            (*dataLabels)[PROGRAMVIEW_LABEL_SIZE];
    Tree            tree;
    Properties      properties;
    // Selection of the tree
    Group           selectedGroup;
    int32_t         selected;   // Index in its group, -1 if none
    // References of the selected game function or string: addresses of the instructions
    uint32_t*       references;
    uint32_t        referenceCount;
    uint32_t        reference;  // The one shown
    // Listing of the function shown
    int32_t         function;   // -1 if none
    SH4_Line*       lines;
    uint32_t        lineCount;
    uint32_t        lineCapacity;
    int32_t         current;    // Highlighted line, -1 if none
    bool            reveal;     // Scroll to the current line
    Vector2         scroll;
    Rectangle       panelView;
    // Places visited before (back) and after (forward) the current one: addresses
    uint32_t        back[PROGRAMVIEW_HISTORY];
    uint32_t        backCount;
    uint32_t        forward[PROGRAMVIEW_HISTORY];
    uint32_t        forwardCount;
} ProgramView_State;

static ProgramView_State g_view;
static bool g_expanded[GROUP_COUNT] = { true, true, false, false, true };
static bool g_pseudoC;  // Show the pseudo-C instead of the listing, kept when another file is opened

void ProgramView_Close(void) {
    free(g_view.fileData);
    MAPINFO_Free(&g_view.mapinfo);
    if (g_view.sh4)
        SH4_FreeProgram(&g_view.program);
    if (g_view.bytecode)
        BC_FreeProgram(&g_view.bc);
    if (g_view.paired) {
        SCN_FreePair(&g_view.pair);
        BC_FreeProgram(&g_view.pairBc);
    }
    free(g_view.pairData);
    free(g_view.functionLabels);
    free(g_view.games);
    free(g_view.gameLabels);
    free(g_view.strings);
    free(g_view.stringLabels);
    free(g_view.relocationLabels);
    SD_FreeData(&g_view.data);
    free(g_view.dataItems);
    free(g_view.dataLabels);
    free(g_view.references);
    free(g_view.lines);
    DC_FreeFunction(&g_view.pseudo);
    memset(&g_view, 0, sizeof(g_view));
    g_view.pseudoFunction = -1;
    g_view.function = -1;
    g_view.current = -1;
    g_view.selected = -1;
}

bool ProgramView_IsOpen(void) {
    return g_view.open;
}

static bool SamePath(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a == '\\' ? '/' : (char)tolower((unsigned char)*a), y = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
        if (x != y)
            return false;
    }
    return *a == *b;
}

bool ProgramView_UsesMap(void) {
    const char* map = MAPINFOView_Path();
    return g_view.open && g_view.fromMap && map != NULL && SamePath(map, g_view.path);
}

// The analyzed program, SH-4 code or bytecode: its functions, calls, strings and listing

static bool HasListing(void) {
    return g_view.sh4 || g_view.bytecode;
}

static uint32_t FunctionCount(void) {
    return g_view.sh4 ? g_view.program.functionCount : g_view.bytecode ? g_view.bc.functionCount : 0;
}

static uint32_t FunctionStart(uint32_t function) {
    return g_view.sh4 ? g_view.program.functions[function].start : g_view.bc.functions[function];
}

static uint32_t FunctionEnd(uint32_t function) {
    return g_view.sh4 ? g_view.program.functions[function].end : BC_FunctionEnd(&g_view.bc, function);
}

static int FunctionAt(uint32_t address) {
    return g_view.sh4 ? SH4_FunctionAt(&g_view.program, address) :
        g_view.bytecode ? BC_FunctionAt(&g_view.bc, address) : -1;
}

static uint32_t CallCount(void) {
    return g_view.sh4 ? g_view.program.callCount : g_view.bytecode ? g_view.bc.callCount : 0;
}

static const SH4_Call* Calls(void) {
    return g_view.sh4 ? g_view.program.calls : g_view.bc.calls;
}

static const char* StringAt(uint32_t offset) {
    return g_view.sh4 ? SH4_DataString(&g_view.program, offset) : BC_DataString(&g_view.bc, offset);
}

static uint32_t MaxFunctionLines(uint32_t function) {
    return g_view.sh4 ? SH4_MaxFunctionLines(&g_view.program, function) : BC_MaxFunctionLines(&g_view.bc, function);
}

static uint32_t FunctionLines(uint32_t function, SH4_Line* lines) {
    return g_view.sh4 ? SH4_FunctionLines(&g_view.program, function, lines) : BC_FunctionLines(&g_view.bc, function, lines);
}

// Offset in the data of the string that an instruction of the bytecode pushes, false if none
static bool BytecodeString(const BC_Instruction* ins, uint32_t* offset) {
    const SCN_Header* header = &g_view.scn.header;
    if (ins->kind != BC_PUSH)
        return false;
    if (ins->area == BC_AREA_PROGRAM && ins->target >= header->dataStart && ins->target < header->end)
        *offset = ins->target - header->dataStart;
    else
        return false;
    return BC_DataString(&g_view.bc, *offset) != NULL;
}

static int CompareGames(const void* a, const void* b) {
    return ((const GameFunction*)a)->id - ((const GameFunction*)b)->id;
}

static int CompareStrings(const void* a, const void* b) {
    uint32_t x = ((const DataString*)a)->offset, y = ((const DataString*)b)->offset;
    return x < y ? -1 : x > y;
}

// The game functions called, and the strings of the data loaded by the code, with their labels
// Adds a load of a string of the data
static void AddString(uint32_t offset, uint32_t loads) {
    uint32_t s = 0;
    while (s < g_view.stringCount && g_view.strings[s].offset != offset)
        s++;
    if (s == g_view.stringCount)
        g_view.strings[g_view.stringCount++].offset = offset;
    g_view.strings[s].loads += loads;
}

static bool CreateLists(void) {
    const SH4_Program* program = &g_view.program;
    uint32_t stringCapacity = g_view.sh4 ? program->literalCount : g_view.bc.instructionCount;
    g_view.functionLabels = calloc(FunctionCount() + 1, PROGRAMVIEW_LABEL_SIZE);
    g_view.games = calloc(CallCount() + 1, sizeof(GameFunction));
    g_view.strings = calloc(stringCapacity + 1, sizeof(DataString));
    if (g_view.functionLabels == NULL || g_view.games == NULL || g_view.strings == NULL)
        return false;
    for (uint32_t f = 0; f < FunctionCount(); f++) {
        uint32_t start = FunctionStart(f);
        snprintf(g_view.functionLabels[f], PROGRAMVIEW_LABEL_SIZE, "0x%05X%s", start,
            start == g_view.scn.header.entry ? "  entry" :
            start == g_view.scn.header.codeStart && g_view.sh4 ? "  resume and yield" : "");
    }
    for (uint32_t c = 0; c < CallCount(); c++) {
        const SH4_Call* call = &Calls()[c];
        if (call->kind != SH4_CALL_GAME || call->id < 0)
            continue;
        uint32_t g = 0;
        while (g < g_view.gameCount && g_view.games[g].id != call->id)
            g++;
        GameFunction* game = &g_view.games[g];
        int32_t arguments = call->argumentBytes >= 0 ? call->argumentBytes / 4 : -1;
        if (g == g_view.gameCount) {
            g_view.gameCount++;
            game->id = call->id;
            game->minArguments = game->maxArguments = arguments;
        }
        game->calls++;
        if (arguments >= 0 && (game->minArguments < 0 || arguments < game->minArguments))
            game->minArguments = arguments;
        if (arguments > game->maxArguments)
            game->maxArguments = arguments;
    }
    qsort(g_view.games, g_view.gameCount, sizeof(GameFunction), CompareGames);
    for (uint32_t l = 0; g_view.sh4 && l < program->literalCount; l++) {
        const SH4_Literal* literal = &program->literals[l];
        if (literal->kind == SH4_LITERAL_DATA_OFFSET && SH4_DataString(program, literal->value) != NULL)
            AddString(literal->value, literal->reads);
    }
    for (uint32_t pc = g_view.scn.header.codeStart; g_view.bytecode && pc < g_view.bc.codeEnd;) {
        BC_Instruction ins;
        uint32_t offset;
        if (!BC_Decode(g_view.scn.data, g_view.bc.codeEnd, pc, &ins)) {
            pc++;
            continue;
        }
        if (BytecodeString(&ins, &offset))
            AddString(offset, 1);
        pc += ins.size;
    }
    qsort(g_view.strings, g_view.stringCount, sizeof(DataString), CompareStrings);
    g_view.gameLabels = calloc(g_view.gameCount + 1, PROGRAMVIEW_LABEL_SIZE);
    g_view.stringLabels = calloc(g_view.stringCount + 1, PROGRAMVIEW_LABEL_SIZE);
    if (g_view.gameLabels == NULL || g_view.stringLabels == NULL)
        return false;
    for (uint32_t g = 0; g < g_view.gameCount; g++) {
        const char* name = GameDB_GameFunctionName(g_view.games[g].id);
        snprintf(g_view.gameLabels[g], PROGRAMVIEW_LABEL_SIZE, "0x%03X  %s%s(%u call%s)", (unsigned)g_view.games[g].id,
            name != NULL ? name : "", name != NULL ? "  " : "", g_view.games[g].calls, g_view.games[g].calls == 1 ? "" : "s");
    }
    // The same text can be at several offsets of the data
    for (uint32_t s = 0; s < g_view.stringCount; s++) {
        char text[PROGRAMVIEW_LABEL_SIZE];
        SH4_QuoteString(StringAt(g_view.strings[s].offset), text, sizeof(text));
        snprintf(g_view.stringLabels[s], PROGRAMVIEW_LABEL_SIZE, "%s  @0x%X", text, g_view.strings[s].offset);
    }
    return true;
}

static bool CreateRelocationLabels(void) {
    g_view.relocationLabels = calloc(g_view.scn.relocationCount + 1, PROGRAMVIEW_LABEL_SIZE);
    if (g_view.relocationLabels == NULL)
        return false;
    for (uint32_t r = 0; r < g_view.scn.relocationCount; r++) {
        SCN_Relocation relocation = SCN_GetRelocation(&g_view.scn, r);
        uint32_t value = relocation.offset + 4 <= g_view.scn.header.size ? Bytes_ReadU32(g_view.scn.data + relocation.offset) : 0;
        snprintf(g_view.relocationLabels[r], PROGRAMVIEW_LABEL_SIZE, "0x%05X: %s 0x%X", relocation.offset,
            relocation.type == SCN_RELOCATION_CODE ? "code" : relocation.type == SCN_RELOCATION_DATA ? "data" : "?", value);
    }
    return true;
}

static bool SameReference(const SD_Reference* a, const SD_Reference* b) {
    return a->kind == b->kind && a->number == b->number && strcmp(a->name, b->name) == 0 && strcmp(a->folder, b->folder) == 0;
}

static uint32_t ReferenceCount(const SD_Reference* reference) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.data.count; i++)
        count += SameReference(&g_view.data.references[i], reference);
    return count;
}

// The text of a distinct reference of the data
static const char* DataText(const SD_Reference* reference) {
    switch (reference->kind) {
    case SD_PACKAGE:
        return reference->folder[0] != '\0' ? TextFormat("Package %s of %s", reference->name, reference->folder) :
            TextFormat("Package %s", reference->name);
    case SD_AFS_PACKAGE:
        return TextFormat("Package %s #%03d", reference->name, reference->number);
    case SD_CUTSCENE:
        return TextFormat("Plays cutscene %d", reference->number);
    case SD_SEQUENCE:
        return TextFormat("Loads cutscene %d: %s of %s", reference->number, reference->name, reference->folder);
    case SD_WAKE:
        return TextFormat("Wakes %s", GameDB_CharacterLabel(reference->name));
    case SD_SLEEP:
        return TextFormat("Puts to sleep %s", GameDB_CharacterLabel(reference->name));
    case SD_CHARACTER:
        return TextFormat("Character %s", GameDB_CharacterLabel(reference->name));
    case SD_SOUND_BANK:
        return TextFormat("Sound bank %d: %s", reference->number, reference->name);
    default:
        return TextFormat("File %s", reference->name);
    }
}

// The distinct references of the data of the program, by kind, with their labels. Returns false on memory error
static bool CreateDataLabels(void) {
    const bool* characterFunctions = GameDB_CharacterFunctions();
    bool read = g_view.sh4 ? SD_ReadProgram(&g_view.data, &g_view.program, g_view.paired ? &g_view.pairBc : NULL,
        g_view.paired ? &g_view.pair : NULL, characterFunctions) :
        SD_ReadProgram(&g_view.data, NULL, &g_view.bc, NULL, characterFunctions);
    if (!read)
        return false;
    g_view.dataItems = calloc(g_view.data.count + 1, sizeof(uint32_t));
    g_view.dataLabels = calloc(g_view.data.count + 1, PROGRAMVIEW_LABEL_SIZE);
    if (g_view.dataItems == NULL || g_view.dataLabels == NULL)
        return false;
    for (int kind = 0; kind < SD_KIND_COUNT; kind++) {
        for (uint32_t i = 0; i < g_view.data.count; i++) {
            const SD_Reference* reference = &g_view.data.references[i];
            bool first = reference->kind == kind;
            for (uint32_t j = 0; j < i && first; j++)
                first = !SameReference(&g_view.data.references[j], reference);
            if (!first)
                continue;
            uint32_t count = ReferenceCount(reference);
            snprintf(g_view.dataLabels[g_view.dataCount], PROGRAMVIEW_LABEL_SIZE, "%s%s", DataText(reference),
                count > 1 ? TextFormat("  (%u calls)", count) : "");
            g_view.dataItems[g_view.dataCount++] = i;
        }
    }
    return true;
}

// The bytecode of the map of an SH-4 program, paired with it: scenario/actNN_<map>.scn for scene/NN/<map>/mapinfo.bin
static void LoadPair(const char* filepath) {
    char path[1200];
    if (!MapProgram_ScnPath(filepath, g_view.pairName, sizeof(g_view.pairName), path, sizeof(path)))
        return;
    size_t size = 0;
    if (!FileExists(path) || (g_view.pairData = File_LoadToMemoryDecompressed(path, &size)) == NULL)
        return;
    if (!SCN_Read(g_view.pairData, size, &g_view.pairScn) || !BC_AnalyzeProgram(&g_view.pairBc, &g_view.pairScn))
        return;
    if (!SCN_PairPrograms(&g_view.pair, &g_view.program, &g_view.pairBc)) {
        BC_FreeProgram(&g_view.pairBc);
        return;
    }
    g_view.paired = true;
    printf("[ProgramView]: Paired with %s: %u of %u functions with the same calls\n", path, g_view.pair.sameFunctions,
        g_view.pair.pairedFunctions);
}

// The comment of a call of the SH-4 program, with the arguments it doesn't know taken from the paired call of the
// bytecode
static void PairedCallComment(const SH4_Call* call, char* text, size_t size) {
    const SH4_Call* other = SCN_PairedCall(&g_view.pair, &g_view.pairBc, (uint32_t)(call - g_view.program.calls));
    if (other == NULL || call->argumentBytes < 0)
        return;
    char prefix[SH4_LINE_COMMENT];
    SH4_DescribeCall(&g_view.program, call, prefix, sizeof(prefix));
    char* open = strchr(prefix, '(');
    if (open == NULL)
        return;
    *open = '\0';
    size_t length = (size_t)snprintf(text, size, "%s(", prefix);
    for (uint32_t i = 0; i < call->argumentCount && length < size; i++) {
        const SH4_Value* value = &call->arguments[i];
        const SH4_Value* known = value->kind == SH4_VALUE_UNKNOWN && i < other->argumentCount ? &other->arguments[i] : NULL;
        char argument[96] = "?";
        if (known == NULL)
            SH4_DescribeValue(&g_view.program, value, argument, sizeof(argument));
        else if (known->kind == SH4_VALUE_CONSTANT)
            SH4_DescribeNumber(known->value, argument, sizeof(argument));
        else if (known->kind == SH4_VALUE_DATA && BC_DataString(&g_view.pairBc, known->value) != NULL)
            SH4_QuoteString(BC_DataString(&g_view.pairBc, known->value), argument, sizeof(argument));
        length += (size_t)snprintf(text + length, size - length, i > 0 ? ", %s" : "%s", argument);
    }
    if (length < size)
        snprintf(text + length, size - length, (uint32_t)call->argumentBytes / 4 > call->argumentCount ? ", ...)" : ")");
}

// Analyzes the program read in g_view.scn
static bool Load(const char* filepath) {
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.function = -1;
    g_view.current = -1;
    g_view.selected = -1;
    const SCN_Header* header = &g_view.scn.header;
    // The calls to the game are listed with the names of the database
    SH4_SetGameNamer(GameDB_GameFunctionName);
    if (SCN_IsSH4(&g_view.scn)) {
        if (!SH4_AnalyzeProgram(&g_view.program, g_view.scn.data, header->codeStart, header->dataStart, header->end,
            header->entry))
            return false;
        g_view.sh4 = true;
        if (IsFileExtension(filepath, ".bin"))
            LoadPair(filepath);
    } else {
        if (!BC_AnalyzeProgram(&g_view.bc, &g_view.scn))
            return false;
        g_view.bytecode = true;
    }
    if (!CreateLists())
        return false;
    printf("[ProgramView]: %s, %u functions, %u calls, %u game functions, %u strings\n", g_view.sh4 ? "SH-4" : "bytecode",
        FunctionCount(), CallCount(), g_view.gameCount, g_view.stringCount);
    if (!CreateRelocationLabels() || !CreateDataLabels())
        return false;
    g_view.open = true;
    return true;
}

bool ProgramView_Open(const char* filepath) {
    ProgramView_Close();
    size_t size = 0;
    g_view.fileData = File_LoadToMemoryDecompressed(filepath, &size);
    if (g_view.fileData == NULL || !SCN_Read(g_view.fileData, size, &g_view.scn) || !Load(filepath)) {
        printf("[ProgramView_Open]: Invalid scene program %s\n", filepath);
        ProgramView_Close();
        return false;
    }
    return true;
}

bool ProgramView_OpenMap(const char* filepath) {
    ProgramView_Close();
    if (!MAPINFO_Load(filepath, &g_view.mapinfo)) {
        ProgramView_Close();
        return false;
    }
    const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&g_view.mapinfo, "SCN3");
    if (chunk == NULL || !SCN_Read(g_view.mapinfo.data + chunk->offset, chunk->size, &g_view.scn) || !Load(filepath)) {
        printf("[ProgramView_OpenMap]: No valid scene program in %s\n", filepath);
        ProgramView_Close();
        return false;
    }
    g_view.fromMap = true;
    return true;
}

// Fills the lines of a function
// The bytecode that has the pseudo-C of a function: the program itself, or the SCN file paired with an SH-4 program.
// NULL if none
static const BC_Program* PseudoSource(int32_t function, uint32_t* index) {
    if (g_view.bytecode) {
        *index = (uint32_t)function;
        return &g_view.bc;
    }
    if (g_view.paired && function >= 0 && g_view.pair.functions[function] >= 0) {
        *index = (uint32_t)g_view.pair.functions[function];
        return &g_view.pairBc;
    }
    return NULL;
}

// Decompiles the function shown when the pseudo-C is shown
static void UpdatePseudo(void) {
    if (!g_pseudoC || g_view.function < 0 || g_view.pseudoFunction == g_view.function)
        return;
    DC_FreeFunction(&g_view.pseudo);
    g_view.pseudoFunction = g_view.function;
    uint32_t index;
    const BC_Program* source = PseudoSource(g_view.function, &index);
    if (source != NULL && !DC_DecompileFunction(source, index, &g_view.pseudo))
        printf("[ProgramView]: Failed to decompile the function 0x%X\n", source->functions[index]);
}

static bool ShowFunction(int32_t function) {
    if (function == g_view.function)
        return true;
    uint32_t max = MaxFunctionLines((uint32_t)function);
    if (max > g_view.lineCapacity) {
        SH4_Line* lines = realloc(g_view.lines, max * sizeof(SH4_Line));
        if (lines == NULL)
            return false;
        g_view.lines = lines;
        g_view.lineCapacity = max;
    }
    g_view.lineCount = FunctionLines((uint32_t)function, g_view.lines);
    for (uint32_t i = 0; g_view.paired && i < g_view.lineCount; i++) {
        const SH4_Call* call = g_view.lines[i].kind == SH4_LINE_INSTRUCTION ?
            SH4_CallAt(&g_view.program, g_view.lines[i].address) : NULL;
        if (call != NULL)
            PairedCallComment(call, g_view.lines[i].comment, sizeof(g_view.lines[i].comment));
    }
    g_view.function = function;
    g_view.scroll = (Vector2) { 0.0f, 0.0f };
    UpdatePseudo();
    return true;
}

// Address of the current place: the current line, or the start of the function shown
static uint32_t CurrentAddress(void) {
    if (g_view.function < 0)
        return 0;
    if (g_view.current >= 0 && (uint32_t)g_view.current < g_view.lineCount)
        return g_view.lines[g_view.current].address;
    return FunctionStart((uint32_t)g_view.function);
}

static void PushHistory(uint32_t* stack, uint32_t* count, uint32_t address) {
    if (*count == PROGRAMVIEW_HISTORY) {
        memmove(stack, stack + 1, (PROGRAMVIEW_HISTORY - 1) * sizeof(uint32_t));
        (*count)--;
    }
    stack[(*count)++] = address;
}

// Shows the function of an address with its line highlighted. remember: the place shown before goes to the history
static void GoTo(uint32_t address, bool remember) {
    int function = FunctionAt(address);
    if (function < 0)
        return;
    if (remember && g_view.function >= 0) {
        PushHistory(g_view.back, &g_view.backCount, CurrentAddress());
        g_view.forwardCount = 0;
    }
    if (!ShowFunction(function))
        return;
    g_view.current = address == FunctionStart((uint32_t)function) ? 0 :
        SH4_LineAt(g_view.lines, g_view.lineCount, address);
    g_view.reveal = true;
    // The tree follows the function shown, unless a game function or a string is selected
    if (g_view.selected < 0 || g_view.selectedGroup == GROUP_FUNCTIONS) {
        g_view.selectedGroup = GROUP_FUNCTIONS;
        if (g_view.selected != function) {
            g_view.selected = function;
            Tree_Reveal(&g_view.tree, true);
        }
    }
}

static void GoBack(void) {
    if (g_view.backCount == 0)
        return;
    PushHistory(g_view.forward, &g_view.forwardCount, CurrentAddress());
    GoTo(g_view.back[--g_view.backCount], false);
}

static void GoForward(void) {
    if (g_view.forwardCount == 0)
        return;
    PushHistory(g_view.back, &g_view.backCount, CurrentAddress());
    GoTo(g_view.forward[--g_view.forwardCount], false);
}

// The instructions that load a string, or call a game function
static void FindReferences(void) {
    g_view.referenceCount = 0;
    g_view.reference = 0;
    free(g_view.references);
    g_view.references = NULL;
    const SH4_Program* program = &g_view.program;
    if (g_view.selectedGroup == GROUP_DATA) {
        const SD_Reference* reference = &g_view.data.references[g_view.dataItems[g_view.selected]];
        g_view.references = malloc((ReferenceCount(reference) + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < g_view.data.count && g_view.references != NULL; i++) {
            if (SameReference(&g_view.data.references[i], reference))
                g_view.references[g_view.referenceCount++] = g_view.data.references[i].address;
        }
    } else if (g_view.selectedGroup == GROUP_GAME) {
        int32_t id = g_view.games[g_view.selected].id;
        g_view.references = malloc((g_view.games[g_view.selected].calls + 1) * sizeof(uint32_t));
        for (uint32_t c = 0; c < CallCount() && g_view.references != NULL; c++) {
            if (Calls()[c].kind == SH4_CALL_GAME && Calls()[c].id == id)
                g_view.references[g_view.referenceCount++] = Calls()[c].address;
        }
    } else if (g_view.selectedGroup == GROUP_STRINGS && g_view.bytecode) {
        uint32_t offset = g_view.strings[g_view.selected].offset, found;
        g_view.references = malloc((g_view.strings[g_view.selected].loads + 1) * sizeof(uint32_t));
        for (uint32_t pc = g_view.scn.header.codeStart; pc < g_view.bc.codeEnd && g_view.references != NULL;) {
            BC_Instruction ins;
            if (!BC_Decode(g_view.scn.data, g_view.bc.codeEnd, pc, &ins)) {
                pc++;
                continue;
            }
            if (BytecodeString(&ins, &found) && found == offset &&
                g_view.referenceCount < g_view.strings[g_view.selected].loads)
                g_view.references[g_view.referenceCount++] = pc;
            pc += ins.size;
        }
    } else if (g_view.selectedGroup == GROUP_STRINGS) {
        uint32_t offset = g_view.strings[g_view.selected].offset;
        g_view.references = malloc((g_view.strings[g_view.selected].loads + 1) * sizeof(uint32_t));
        for (uint32_t pc = program->code.start; pc < program->code.end && g_view.references != NULL; pc += 2) {
            SH4_Instruction ins;
            if (!(SH4_WordKind(&program->code, pc) & SH4_WORD_CODE) ||
                !SH4_Decode(Bytes_ReadU16(program->code.data + pc), pc, 0, &ins) || !(ins.flags & SH4_LITERAL))
                continue;
            const SH4_Literal* literal = SH4_LiteralAt(program, ins.target);
            if (literal != NULL && literal->kind == SH4_LITERAL_DATA_OFFSET && literal->value == offset &&
                g_view.referenceCount < g_view.strings[g_view.selected].loads)
                g_view.references[g_view.referenceCount++] = pc;
        }
    }
}

static void ShowReference(int direction) {
    if (g_view.referenceCount == 0)
        return;
    g_view.reference = (g_view.reference + g_view.referenceCount + (uint32_t)direction) % g_view.referenceCount;
    GoTo(g_view.references[g_view.reference], true);
}

static void Select(Group group, int32_t index) {
    g_view.selectedGroup = group;
    g_view.selected = index;
    g_view.referenceCount = 0;
    if (index < 0 || !HasListing())
        return;
    if (group == GROUP_FUNCTIONS) {
        GoTo(FunctionStart((uint32_t)index), true);
    } else if (group == GROUP_GAME || group == GROUP_STRINGS || group == GROUP_DATA) {
        FindReferences();
        if (g_view.referenceCount > 0)
            GoTo(g_view.references[0], true);
    }
}

void ProgramView_Toolbar(void) {
    if (ProgramView_UsesMap()) {
        if (Toolbar_Button(ICON_ARROW_LEFT, "Back", "Close the program and go to its map", KEY_BACKSPACE)) {
            ProgramView_Close();
            return;
        }
        Toolbar_Separator();
    }
    if (!HasListing())
        return;
    if (g_view.backCount == 0)
        GuiDisable();
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Go back to the previous place", TOOLBAR_REPEAT | KEY_LEFT))
        GoBack();
    GuiEnable();
    if (g_view.forwardCount == 0)
        GuiDisable();
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Go forward", TOOLBAR_REPEAT | KEY_RIGHT))
        GoForward();
    GuiEnable();
    Toolbar_Separator();
    if (g_view.referenceCount < 2)
        GuiDisable();
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous reference of the selected item",
        TOOLBAR_REPEAT | TOOLBAR_SHIFT | KEY_TAB))
        ShowReference(-1);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next reference of the selected item", TOOLBAR_REPEAT | KEY_TAB))
        ShowReference(1);
    GuiEnable();
    Toolbar_Separator();
    if (!g_view.bytecode && !g_view.paired)
        GuiDisable();
    if (Toolbar_Toggle(ICON_FILETYPE_TEXT, "Pseudo-C", "Show the pseudo-C of the function (from the bytecode)", KEY_C,
        &g_pseudoC)) {
        g_view.scroll = (Vector2) { 0.0f, 0.0f };
        UpdatePseudo();
    }
    GuiEnable();
}

static void DrawTree(Rectangle bounds) {
    Group clickedGroup = GROUP_COUNT;
    int32_t clicked = -1;
    Tree_Begin(&g_view.tree, bounds);
    for (int g = 0; g < GROUP_COUNT; g++) {
        uint32_t count = g == GROUP_FUNCTIONS ? FunctionCount() : g == GROUP_DATA ? g_view.dataCount :
            g == GROUP_GAME ? g_view.gameCount : g == GROUP_STRINGS ? g_view.stringCount : g_view.scn.relocationCount;
        if (count == 0)
            continue;
        Tree_BeginGroup(&g_view.tree, g_groupNames[g], &g_expanded[g], NULL);
        for (uint32_t i = 0; i < count; i++) {
            const char* label = g == GROUP_FUNCTIONS ? g_view.functionLabels[i] : g == GROUP_DATA ? g_view.dataLabels[i] :
                g == GROUP_GAME ? g_view.gameLabels[i] :
                g == GROUP_STRINGS ? g_view.stringLabels[i] : g_view.relocationLabels[i];
            int flags = (int)g_view.selectedGroup == g && g_view.selected == (int32_t)i ? TREE_SELECTED : 0;
            if (Tree_Item(&g_view.tree, label, flags, NULL)) {
                clickedGroup = (Group)g;
                clicked = (int32_t)i;
            }
        }
        Tree_EndGroup(&g_view.tree);
    }
    Tree_End(&g_view.tree);
    if (clicked >= 0)
        Select(clickedGroup, clicked);
}

static void HeaderProperties(Properties* properties) {
    const SCN_Header* header = &g_view.scn.header;
    Properties_Section(properties, "Header");
    Properties_Text(properties, "Version", TextFormat("0x%X (%s)", header->version, g_view.sh4 ? "SH-4 code" : "bytecode"));
    Properties_Text(properties, "Size", TextFormat("0x%X", (unsigned)header->size));
    Properties_Text(properties, "Entry point", TextFormat("0x%X", (unsigned)header->entry));
    Properties_Text(properties, "Code", TextFormat("0x%X", (unsigned)header->codeStart));
    Properties_Text(properties, "Data", TextFormat("0x%X", (unsigned)header->dataStart));
    Properties_Text(properties, "End of the data", TextFormat("0x%X", (unsigned)header->end));
    Properties_Text(properties, "Variables", TextFormat("0x%X bytes", (unsigned)header->variableSize));
    Properties_Int(properties, "Relocations", g_view.scn.relocationCount);
    uint32_t kinds[SH4_CALL_UNKNOWN + 1] = { 0 };
    for (uint32_t c = 0; c < CallCount(); c++)
        kinds[Calls()[c].kind]++;
    Properties_Section(properties, "Code");
    Properties_Int(properties, "Functions", FunctionCount());
    if (g_view.sh4) {
        const SH4_CodeStats* stats = &g_view.program.code.stats;
        Properties_Int(properties, "Instructions", stats->instructions);
        Properties_Int(properties, "Literal words", stats->literals);
        Properties_Int(properties, "Words not reached", stats->unknown);
    } else {
        Properties_Int(properties, "Instructions", g_view.bc.instructionCount);
        Properties_Text(properties, "End of the code", TextFormat("0x%X", g_view.bc.codeEnd));
    }
    Properties_Section(properties, "Calls");
    Properties_Int(properties, "Local", kinds[SH4_CALL_LOCAL]);
    Properties_Text(properties, "Game", TextFormat("%u (%u functions)", kinds[SH4_CALL_GAME], g_view.gameCount));
    Properties_Int(properties, "ctx[0x2C], ctx[0x30]", kinds[SH4_CALL_SERVICE2C] + kinds[SH4_CALL_SERVICE30]);
    if (g_view.sh4) {
        Properties_Int(properties, "Yield", kinds[SH4_CALL_YIELD]);
        Properties_Int(properties, "Other", kinds[SH4_CALL_CONTEXT] + kinds[SH4_CALL_UNKNOWN]);
    }
}

static void FunctionProperties(Properties* properties, uint32_t index) {
    uint32_t kinds[SH4_CALL_UNKNOWN + 1] = { 0 }, callers = 0, start = FunctionStart(index), end = FunctionEnd(index);
    for (uint32_t c = 0; c < CallCount(); c++) {
        const SH4_Call* call = &Calls()[c];
        if (call->function == index)
            kinds[call->kind]++;
        callers += call->kind == SH4_CALL_LOCAL && call->target == start;
    }
    Properties_Text(properties, "Start", TextFormat("0x%X", (unsigned)start));
    Properties_Text(properties, "End", TextFormat("0x%X", (unsigned)end));
    if (g_view.sh4) {
        const SH4_Function* function = &g_view.program.functions[index];
        uint32_t instructions = 0;
        for (uint32_t pc = start; pc < end; pc += 2)
            instructions += (SH4_WordKind(&g_view.program.code, pc) & SH4_WORD_CODE) != 0;
        Properties_Int(properties, "Instructions", instructions);
        Properties_Int(properties, "Blocks", function->blockCount);
    }
    Properties_Int(properties, "Local callers", callers);
    if (g_view.paired && g_view.pair.functions[index] >= 0) {
        Properties_Section(properties, "Bytecode");
        Properties_Text(properties, "File", g_view.pairName);
        Properties_Text(properties, "Function", TextFormat("0x%X", g_view.pairBc.functions[g_view.pair.functions[index]]));
        Properties_Bool(properties, "Same calls", g_view.pair.sameCalls[index]);
    }
    Properties_Section(properties, "Calls");
    Properties_Int(properties, "Local", kinds[SH4_CALL_LOCAL]);
    Properties_Int(properties, "Game", kinds[SH4_CALL_GAME]);
    Properties_Int(properties, "ctx[0x2C], ctx[0x30]", kinds[SH4_CALL_SERVICE2C] + kinds[SH4_CALL_SERVICE30]);
    if (g_view.sh4)
        Properties_Int(properties, "Yield", kinds[SH4_CALL_YIELD]);
}

static void ReferenceProperties(Properties* properties) {
    Properties_Section(properties, "References");
    Properties_Int(properties, "Count", g_view.referenceCount);
    if (g_view.referenceCount > 0)
        Properties_Text(properties, "Shown", TextFormat("%u of %u, at 0x%X", g_view.reference + 1, g_view.referenceCount,
            g_view.references[g_view.reference]));
    if (g_view.referenceCount > 1)
        Properties_Note(properties, "Tab and Shift+Tab: next and previous reference");
}

static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_view.properties;
    int32_t index = g_view.selected;
    switch (index < 0 ? GROUP_COUNT : g_view.selectedGroup) {
    case GROUP_FUNCTIONS:
        Properties_Begin(properties, bounds, TextFormat("Function 0x%X", FunctionStart((uint32_t)index)));
        FunctionProperties(properties, (uint32_t)index);
        break;
    case GROUP_GAME: {
        const GameFunction* game = &g_view.games[index];
        const GameDB_GameFunction* info = GameDB_FindGameFunction(game->id);
        const char* name = GameDB_GameFunctionName(game->id);
        Properties_Begin(properties, bounds, name != NULL ? TextFormat("Game function %s", name) :
            TextFormat("Game function 0x%X", (unsigned)game->id));
        Properties_Text(properties, "ID", TextFormat("0x%X", (unsigned)game->id));
        if (name != NULL)
            Properties_Text(properties, "Name", name);
        if (info != NULL && info->description[0] != '\0')
            Properties_Text(properties, "Description", info->description);
        Properties_Int(properties, "Calls", game->calls);
        if (game->minArguments >= 0)
            Properties_Text(properties, "Arguments", game->minArguments == game->maxArguments ?
                TextFormat("%d", game->minArguments) : TextFormat("%d to %d", game->minArguments, game->maxArguments));
        if (info != NULL && info->arguments[0] != '\0')
            Properties_Text(properties, "Arguments in all the maps", info->arguments);
        if (info != NULL && info->handler[0] != '\0')
            Properties_Text(properties, "Code (1ST_READ.BIN)", info->handler);
        ReferenceProperties(properties);
        break;
    }
    case GROUP_DATA: {
        const SD_Reference* reference = &g_view.data.references[g_view.dataItems[index]];
        const char* name = GameDB_GameFunctionName(reference->game);
        Properties_Begin(properties, bounds, "Program data");
        Properties_Text(properties, "Kind", SD_KindName((SD_Kind)reference->kind));
        if (reference->name[0] != '\0')
            Properties_Text(properties, "Name", reference->name);
        if (reference->kind == SD_WAKE || reference->kind == SD_SLEEP || reference->kind == SD_CHARACTER)
            GameDB_CharacterProperties(properties, reference->name);
        if (reference->folder[0] != '\0')
            Properties_Text(properties, reference->kind == SD_PACKAGE || strchr(reference->folder, '/') != NULL ? "Folder" :
                "Package", reference->folder);
        if (reference->number >= 0)
            Properties_Int(properties, reference->kind == SD_AFS_PACKAGE ? "Entry" : "Slot", reference->number);
        Properties_Text(properties, "Game function", name != NULL ? TextFormat("%s (0x%X)", name, reference->game) :
            TextFormat("0x%X", reference->game));
        ReferenceProperties(properties);
        break;
    }
    case GROUP_STRINGS: {
        const DataString* string = &g_view.strings[index];
        Properties_Begin(properties, bounds, "String");
        Properties_Text(properties, "Text", StringAt(string->offset));
        Properties_Text(properties, "Offset in the data", TextFormat("0x%X", (unsigned)string->offset));
        Properties_Text(properties, "Offset in the program",
            TextFormat("0x%X", (unsigned)(g_view.scn.header.dataStart + string->offset)));
        ReferenceProperties(properties);
        break;
    }
    case GROUP_RELOCATIONS: {
        SCN_Relocation relocation = SCN_GetRelocation(&g_view.scn, (uint32_t)index);
        Properties_Begin(properties, bounds, "Relocation");
        Properties_Text(properties, "Type", TextFormat("0x%X", (unsigned)relocation.type));
        Properties_Text(properties, "Offset", TextFormat("0x%X", (unsigned)relocation.offset));
        if (relocation.offset + 4 <= g_view.scn.header.size)
            Properties_Text(properties, "Value", TextFormat("0x%X", Bytes_ReadU32(g_view.scn.data + relocation.offset)));
        break;
    }
    default:
        Properties_Begin(properties, bounds, "Scene program");
        HeaderProperties(properties);
        break;
    }
    Properties_End(properties);
}

// Colors of the listing
static const Color g_addressColor = { 130, 130, 140, 255 };
static const Color g_codeColor = { 110, 110, 120, 255 };
static const Color g_textColor = { 220, 220, 220, 255 };
static const Color g_literalColor = { 160, 160, 160, 255 };
static const Color g_unknownColor = { 100, 100, 100, 255 };
static const Color g_commentColor = { 130, 200, 130, 255 };
static const Color g_headerColor = { 240, 200, 110, 255 };
static const Color g_linkColor = { 120, 180, 250, 255 };

// The pseudo-C of the function shown, with the address of the bytecode of each line
static void DrawPseudo(Rectangle area, Font font, float fontSize, float charWidth, float lineHeight) {
    float scrollBarWidth = (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    float border = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    const DC_Function* pseudo = &g_view.pseudo;
    Rectangle content = { 0.0f, 0.0f, fmaxf(area.width - 2.0f * border - scrollBarWidth, charWidth * 200.0f),
        pseudo->lineCount * lineHeight + 2.0f * PROGRAMVIEW_MARGIN };
    GuiScrollPanel(area, NULL, content, &g_view.scroll, &g_view.panelView);
    Rectangle view = g_view.panelView;
    DrawRectangleRec(view, (Color) { 30, 30, 32, 255 });
    if (pseudo->lineCount == 0) {
        DrawTextEx(font, g_view.function < 0 ? "Select a function, a game function or a string" :
            "No pseudo-C: the program of the map has no SCN file with the same functions",
            (Vector2) { view.x + 2.0f * PROGRAMVIEW_MARGIN, view.y + 2.0f * PROGRAMVIEW_MARGIN }, fontSize, 1.0f, GRAY);
        return;
    }
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    float originX = view.x + PROGRAMVIEW_MARGIN + g_view.scroll.x, originY = view.y + PROGRAMVIEW_MARGIN + g_view.scroll.y;
    int first = (int)floorf((view.y - originY) / lineHeight), last = (int)ceilf((view.y + view.height - originY) / lineHeight);
    for (int i = first < 0 ? 0 : first; i <= last && i < (int)pseudo->lineCount; i++) {
        const DC_Line* line = &pseudo->lines[i];
        float y = originY + i * lineHeight + 3.0f;
        bool label = line->text[0] == 'L' && line->text[1] == '_';
        DrawTextEx(font, TextFormat("%08X", line->address), (Vector2) { originX, y }, fontSize, 1.0f, g_addressColor);
        DrawTextEx(font, line->text, (Vector2) { originX + (10.0f + line->indent * 4.0f) * charWidth, y }, fontSize, 1.0f,
            i == 0 ? g_headerColor : label ? YELLOW : g_textColor);
    }
    EndScissorMode();
}

// The lines of the function shown. A click on a line selects it; on a line that leads to another place, goes there
static void DrawListing(Rectangle area) {
    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float charWidth = MeasureTextEx(font, "0", fontSize, 1.0f).x + 1.0f;
    float lineHeight = fontSize + 6.0f;
    if (g_pseudoC && (g_view.bytecode || g_view.paired)) {
        DrawPseudo(area, font, fontSize, charWidth, lineHeight);
        return;
    }
    float scrollBarWidth = (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    float border = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    // Columns, in characters: address, opcode (the bytes of the instructions of the bytecode), label, instruction, comment
    const float sh4Columns[5] = { 0.0f, 10.0f, 15.0f, 17.0f, 47.0f }, bytecodeColumns[5] = { 0.0f, 10.0f, 25.0f, 27.0f, 52.0f };
    const float* columns = g_view.sh4 ? sh4Columns : bytecodeColumns;
    Rectangle content = { 0.0f, 0.0f, fmaxf(area.width - 2.0f * border - scrollBarWidth, charWidth * 160.0f),
        g_view.lineCount * lineHeight + 2.0f * PROGRAMVIEW_MARGIN };
    if (g_view.reveal && g_view.current >= 0 && g_view.panelView.height > 0.0f) {
        float y = g_view.current * lineHeight;
        if (y + g_view.scroll.y < 0.0f || y + lineHeight + g_view.scroll.y > g_view.panelView.height)
            g_view.scroll.y = -(y - g_view.panelView.height / 3.0f);
        g_view.scroll.y = fminf(0.0f, fmaxf(g_view.scroll.y, -(content.height - g_view.panelView.height)));
        g_view.scroll.x = 0.0f;
        g_view.reveal = false;
    }
    GuiScrollPanel(area, NULL, content, &g_view.scroll, &g_view.panelView);
    Rectangle view = g_view.panelView;
    DrawRectangleRec(view, (Color) { 30, 30, 32, 255 });
    if (g_view.function < 0) {
        DrawTextEx(font, "Select a function, a game function or a string", (Vector2) { view.x + 2.0f * PROGRAMVIEW_MARGIN,
            view.y + 2.0f * PROGRAMVIEW_MARGIN }, fontSize, 1.0f, GRAY);
        return;
    }

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    float originX = view.x + PROGRAMVIEW_MARGIN + g_view.scroll.x, originY = view.y + PROGRAMVIEW_MARGIN + g_view.scroll.y;
    int first = (int)floorf((view.y - originY) / lineHeight), last = (int)ceilf((view.y + view.height - originY) / lineHeight);
    Vector2 mouse = GetMousePosition();
    bool mouseIn = CheckCollisionPointRec(mouse, view);
    int clicked = -1;
    for (int i = first < 0 ? 0 : first; i <= last && i < (int)g_view.lineCount; i++) {
        const SH4_Line* line = &g_view.lines[i];
        float y = originY + i * lineHeight;
        Rectangle row = { view.x, y, view.width, lineHeight };
        bool hover = mouseIn && CheckCollisionPointRec(mouse, row);
        if (i == g_view.current)
            DrawRectangleRec(row, (Color) { 62, 62, 90, 255 });
        else if (hover)
            DrawRectangleRec(row, (Color) { 45, 45, 50, 255 });
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            clicked = i;
        float textY = y + 3.0f;
        if (line->kind == SH4_LINE_FUNCTION) {
            DrawTextEx(font, TextFormat("%s  ; %s", line->text, line->comment), (Vector2) { originX, textY }, fontSize, 1.0f,
                g_headerColor);
            continue;
        }
        Color textColor = line->kind == SH4_LINE_LITERAL ? g_literalColor : line->kind == SH4_LINE_UNKNOWN ?
            g_unknownColor : line->jump != 0 ? g_linkColor : g_textColor;
        DrawTextEx(font, TextFormat("%08X", line->address), (Vector2) { originX + columns[0] * charWidth, textY }, fontSize,
            1.0f, g_addressColor);
        if (line->kind == SH4_LINE_INSTRUCTION && g_view.sh4)
            DrawTextEx(font, TextFormat("%04X", Bytes_ReadU16(g_view.program.code.data + line->address)),
                (Vector2) { originX + columns[1] * charWidth, textY }, fontSize, 1.0f, g_codeColor);
        if (line->kind == SH4_LINE_INSTRUCTION && g_view.bytecode) {
            char bytes[16] = "";
            size_t length = 0;
            for (uint32_t b = 0; b < line->size && b < 5 && length < sizeof(bytes); b++)
                length += (size_t)snprintf(bytes + length, sizeof(bytes) - length, b > 0 ? " %02X" : "%02X",
                    g_view.scn.data[line->address + b]);
            DrawTextEx(font, bytes, (Vector2) { originX + columns[1] * charWidth, textY }, fontSize, 1.0f, g_codeColor);
        }
        if (line->label)
            DrawTextEx(font, ">", (Vector2) { originX + columns[2] * charWidth, textY }, fontSize, 1.0f, YELLOW);
        DrawTextEx(font, line->slot ? TextFormat(" %s", line->text) : line->text,
            (Vector2) { originX + columns[3] * charWidth, textY }, fontSize, 1.0f, textColor);
        if (line->comment[0] != '\0')
            DrawTextEx(font, TextFormat("; %s", line->comment), (Vector2) { originX + columns[4] * charWidth, textY },
                fontSize, 1.0f, g_commentColor);
    }
    EndScissorMode();
    if (clicked >= 0) {
        uint32_t jump = g_view.lines[clicked].jump;
        g_view.current = clicked;
        // A target in another function, or far in this one, is a new place
        if (jump != 0 && FunctionAt(jump) >= 0) {
            g_view.current = clicked;
            GoTo(jump, true);
        }
    }
}

void ProgramView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    float margin = 10.0f;
    float top = area.y + margin;
    Rectangle column = { area.x + margin, top, PROGRAMVIEW_LIST_WIDTH, area.y + area.height - margin - top };
    Rectangle listing = { column.x + column.width + margin, top, area.width - column.width - margin * 3.0f, column.height };
    Rectangle tree, properties;
    Properties_Split(column, &tree, &properties);
    if (tree.height <= 0.0f || listing.width <= 0.0f)
        return;
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    DrawTree(tree);
    DrawProperties(properties);
    DrawListing(listing);
}

void ProgramView_Status(ViewStatus* status) {
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    if (g_view.function >= 0)
        snprintf(status->info, sizeof(status->info), "Function 0x%X (%u of %u)", FunctionStart((uint32_t)g_view.function),
            (uint32_t)g_view.function + 1, FunctionCount());
    else
        snprintf(status->info, sizeof(status->info), "%s program, %u functions, %u relocations",
            g_view.sh4 ? "SH-4" : "Bytecode", FunctionCount(), g_view.scn.relocationCount);
    snprintf(status->hint, sizeof(status->hint), g_view.sh4 ?
        "Click a branch, a call or a literal: go to it, Left/Right: back and forward" :
        "Click a branch, a call or an address of a function: go to it, Left/Right: back and forward");
}

bool ProgramView_CanExport(void) {
    return g_view.open && HasListing();
}

void ProgramView_ExportDialog(void) {
    if (!ProgramView_CanExport())
        return;
    // The name of the map or of the file, as a text file next to it
    char name[256];
    if (g_view.fromMap)
        snprintf(name, sizeof(name), "%s_program", GetFileName(GetDirectoryPath(g_view.path)));
    else
        File_GetBaseName(g_view.path, name, sizeof(name));
    File_SanitizeName(name);
    strncat(name, ".txt", sizeof(name) - strlen(name) - 1);
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(g_view.path));
    const nfdu8filteritem_t filter = { "Text", "txt" };
    nfdu8char_t* outPath = NULL;
    nfdresult_t result = NFD_SaveDialogU8(&outPath, &filter, 1, folder[0] != '\0' ? folder : NULL, name);
    if (result == NFD_OKAY) {
        FILE* file = fopen(outPath, "w");
        if (file != NULL) {
            const SCN_Header* header = &g_view.scn.header;
            fprintf(file, "; %s\n; %s program: entry 0x%X, code 0x%X, data 0x%X, end 0x%X\n", g_view.path,
                g_view.sh4 ? "SH-4" : "Bytecode", header->entry, header->codeStart, header->dataStart, header->end);
            // The pseudo-C if it is shown, from the bytecode of the program or of its pair
            if (g_pseudoC && g_view.bytecode)
                DC_WriteProgram(&g_view.bc, file);
            else if (g_pseudoC && g_view.paired) {
                fprintf(file, "; Pseudo-C of %s\n", g_view.pairName);
                DC_WriteProgram(&g_view.pairBc, file);
            } else if (g_view.sh4)
                SH4_WriteListing(&g_view.program, file);
            else
                BC_WriteListing(&g_view.bc, file);
            fclose(file);
            printf("Exported the program to %s\n", outPath);
        }
        else
            printf("Failed to export the program to %s\n", outPath);
        NFD_FreePathU8(outPath);
    }
    else if (result == NFD_ERROR)
        printf("Error: %s\n", NFD_GetError());
}

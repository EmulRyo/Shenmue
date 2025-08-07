#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <cJSON.h>
#include <nfd.h>
#include <raylib.h>
#include <rlgl.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "dynamicArray.h"
#include "hashTable.h"
#include "tad.h"

static HashTable g_filenameDB;
static TAD_Context g_TADCtx;
static size_t g_tadEntriesCount;
static FilePathList g_fileList;
static unsigned int g_fileID;

static size_t Hash_Hash(const void* data) {
    size_t value = *(uint32_t*)data;
    return value;
}

static HashTable ReadJson(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    rewind(file);

    char* data = malloc(length + 1);
    if (data == NULL) {
        fputs("[ReadJson]: Memory error", stderr);
        exit(2);
    }
    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

	HashTable ht = HT_Create(128000, sizeof(uint32_t), sizeof(char) * 128, Hash_Hash);

	cJSON* root = cJSON_Parse(data);
	cJSON* item = NULL;
	cJSON_ArrayForEach(item, root) {
		cJSON* hash = cJSON_GetObjectItemCaseSensitive(item, "Hash");
		cJSON* path = cJSON_GetObjectItemCaseSensitive(item, "Path");
		uint32_t key = (uint32_t)hash->valuedouble;
		HT_SetEx(ht, &key, sizeof(uint32_t), path->valuestring, strlen(path->valuestring)+1);
	}

	return ht;
}

static void SetStyle(int fontSize) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, fontSize);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1F1F1FFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x1F1F1FFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x1F1F1FFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x3D3D3DFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0x1F1F1FFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xFFFFFFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xFFFFFFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xFFFFFFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0x5F5F5FFF);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x707070FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x707070FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x707070FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x1F1F1FFF);
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x1F1F1FFF);
}

static void Init() {
    Vector2 winDPI = GetWindowScaleDPI();
    char* utf8Chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "1234567890"
        "?¿!¡ ()[]{};,.:-_'\"#+-*=%&<>@/\\^";

    int numCodepoints = 0;
    int* codePoints = LoadCodepoints(utf8Chars, &numCodepoints);
    int fontSize = 16 * winDPI.y;
    Font font = LoadFontEx("C:\\Windows\\Fonts\\segoeui.ttf", fontSize, codePoints, numCodepoints);
    GuiSetFont(font);
	SetStyle(fontSize);

    NFD_Init();

    g_filenameDB = ReadJson("resources/FilenameDatabase.json");
}

static void ExtractTADs(const char* folder) {
    g_fileList = LoadDirectoryFilesEx(folder, ".tad", false);
    if (g_fileList.count > 0) {
        const char* filename = g_fileList.paths[0];
        printf("Loading TAD file: %s\n", filename);
        TAD_ContextInit(&g_TADCtx, filename, g_filenameDB, "content", true);
        g_tadEntriesCount = TAD_ExtractAllFilesStart(&g_TADCtx);
        g_fileID = 0;
	}
    else
        UnloadDirectoryFiles(g_fileList);
}

static void ExtractTADsDialog() {
    nfdu8char_t* outPath;
    nfdresult_t result = NFD_PickFolder(&outPath, NULL);
    if (result == NFD_OKAY) {
		ExtractTADs(outPath);
        NFD_FreePathU8(outPath);
    }
    else if (result == NFD_ERROR) {
        printf("Error: %s\n", NFD_GetError());
    }
}

static void Update() {
    if (g_TADCtx.buffer == NULL)
        return;

    size_t entryID = TAD_ExtractAllFilesNext(&g_TADCtx);
    if (entryID == SIZE_MAX) {
        TAD_ExtractAllFilesEnd(&g_TADCtx);
		g_fileID++;
        if (g_fileID < g_fileList.count) {
            const char* filename = g_fileList.paths[g_fileID];
            printf("Loading TAD file: %s\n", filename);
            TAD_ContextInit(&g_TADCtx, filename, g_filenameDB, "content", true);
            g_tadEntriesCount = TAD_ExtractAllFilesStart(&g_TADCtx);
        }
        else {
            printf("All TAD files extracted.\n");
            UnloadDirectoryFiles(g_fileList);
		}
    }
}

static void DrawStatusBar(Rectangle dst) {
    float x, y, w, h;
    const char* text;
    x = dst.x;
    w = 0;
    h = dst.height;
    y = dst.y;

    w = roundf((float)GetScreenWidth() * 1.0f/3.0f);
    text = TextFormat("%s: %d fps", "Refresh", GetFPS());
    GuiStatusBar((Rectangle){ x, y, w, h }, text);
    x += w;

    w = roundf((float)GetScreenWidth() * 1.0f/3.0f);
    text = TextFormat("TAD extraction: file %u/%u, entry: %zu/%zu", g_fileID+1, g_fileList.count, g_TADCtx.entryID+1, g_tadEntriesCount);
    GuiStatusBar((Rectangle){ x, y, w, h }, text);
    x += w;

    w = (float)GetScreenWidth() * 1.0f / 3.0f;
    GuiStatusBar((Rectangle) { x, y, w, h }, "");
	float progress = (float)(g_TADCtx.entryID + 1) / g_tadEntriesCount;
    DrawRectangleRec((Rectangle) { x + 1, y + 1, progress * (w - 2), h - 2 }, GREEN);
}

static void Draw() {
    if (GuiButton((Rectangle) { 10, 10, 200, 30 }, "Extract TAD files")) {
		ExtractTADsDialog();
    }
	DrawStatusBar((Rectangle) { 0, GetScreenHeight() - 30, GetScreenWidth(), 30 });
}

void main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Shenmue");

    Init();

    while (!WindowShouldClose()) {
        Update();

        BeginDrawing();

        ClearBackground((Color) {28, 28, 28, 255});
        Draw();

        EndDrawing();
    }

    CloseWindow();
    NFD_Quit();
}
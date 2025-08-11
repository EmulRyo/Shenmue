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
#include "pvr.h"

static HashTable g_filenameDB;
static TAD_Context g_TADCtx;
static size_t g_tadEntriesCount;
static FilePathList g_fileList;
static unsigned int g_fileID;
static Texture g_texture;

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

static void OpenFileDialog() {
    nfdu8char_t* outPath;
    nfdu8filteritem_t filters[3] = { { "Textures", "pvr,spr,dds" }, { "Models", "mt5,mt7" }, { "Sounds", "snd" } };
    nfdopendialogu8args_t args = { 0 };
    args.filterList = filters;
    args.filterCount = 3;
    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY) {
        if (strstr(outPath, ".pvr") != NULL) {
			uint32_t* data = NULL;
            uint16_t width = 0, height = 0;
            if (PVR_Load(outPath, &width, &height, &data)) {
                Image img1 = { 0 };
                img1.data = data;
                img1.width = width;
                img1.height = height;
                img1.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
				img1.mipmaps = 1;

				// Draw the image on top of the checkered pattern
				Image img2 = GenImageChecked(width, height, width/8, height/8, (Color) { 128, 128, 128, 20 }, (Color) { 255, 255, 255, 20 });
				ImageDraw(&img2, img1, (Rectangle) { 0, 0, width, height }, (Rectangle) { 0, 0, width, height }, WHITE);
                
                if (g_texture.id > 0)
                    UnloadTexture(g_texture);
                g_texture = LoadTextureFromImage(img2);
                UnloadImage(img1);
				UnloadImage(img2);
            }
            else {
                printf("Failed to load PVR file: %s\n", outPath);
            }
        }
        else {
            printf("Selected file not implemented yet.\n");
        }
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

static void DrawContent(Rectangle dst) {
    if (g_texture.id > 0) {
		float ar = (float)g_texture.width / (float)g_texture.height;
		float height = dst.height;
		float width = ar * height;
        if (ar < 1.0f) {
            width = dst.width;
            height = width / ar;
        }

        DrawTexturePro(
            g_texture,
            (Rectangle) { 0, 0, g_texture.width, g_texture.height },
            (Rectangle) { dst.x + dst.width/2.0f - width/2.0f, dst.y + dst.height/2.0f - height/2.0f, width, height },
            (Vector2)   { 0, 0 },
            0.0f,
            WHITE
        );
    }
}

static void Draw() {
    if (GuiButton((Rectangle) { 10, 10, 200, 30 }, "Extract TAD files")) {
		ExtractTADsDialog();
    }
    if (GuiButton((Rectangle) { 220, 10, 200, 30 }, "Open files")) {
        OpenFileDialog();
    }
	DrawContent((Rectangle) { 0, 50, GetScreenWidth(), GetScreenHeight() - 50 - 40 });
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
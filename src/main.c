#include <stdio.h>
#include <math.h>
#include <nfd.h>
#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "core/hashTable.h"
#include "views/imageView.h"
#include "views/views.h"
#include "app/fileOpen.h"
#include "app/filenameDB.h"
#include "app/style.h"
#include "app/tadBatch.h"
#include "ui/toolbar.h"

static HashTable g_filenameDB;

static void Init() {
    Style_Init();
    NFD_Init();
    g_filenameDB = FilenameDB_Load("resources/FilenameDatabase.json");
}

static void SaveFileDialog() {
    nfdu8char_t* outPath = NULL;
    nfdu8filteritem_t filters[1] = { { "Images", "png,jpg,bmp" } };
    nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 1, NULL, NULL);
    if (result == NFD_OKAY) {
        if (ImageView_IsOpen())
            ImageView_Export(outPath);
        else
            printf("No image to save.\n");

        NFD_FreePathU8(outPath);
    }
    else if (result == NFD_ERROR) {
        printf("Error: %s\n", NFD_GetError());
    }
}

static void ProcessDroppedFiles() {
    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        for (unsigned int i = 0; i < files.count; i++)
            FileOpen_Open(files.paths[i]);
        UnloadDroppedFiles(files);
    }
}

static void Update() {
    Style_Update();
    ProcessDroppedFiles();
    TADBatch_Update();
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

    TADBatch_Progress progress = TADBatch_GetProgress();
    w = roundf((float)GetScreenWidth() * 1.0f/3.0f);
    text = TextFormat("TAD extraction: file %u/%u, entry: %zu/%zu", progress.file+1, progress.fileCount, progress.entry+1, progress.entryCount);
    GuiStatusBar((Rectangle){ x, y, w, h }, text);
    x += w;

    w = (float)GetScreenWidth() * 1.0f / 3.0f;
    GuiStatusBar((Rectangle) { x, y, w, h }, "");
    float ratio = (float)(progress.entry + 1) / progress.entryCount;
    DrawRectangleRec((Rectangle) { x + 1, y + 1, ratio * (w - 2), h - 2 }, GREEN);
}

// The actions of the application, then those of the open view
static void DrawToolbar(Rectangle area) {
    Toolbar_Begin(area);
    if (Toolbar_Button(ICON_FOLDER_FILE_OPEN, "Open", "Open files", TOOLBAR_CTRL | KEY_O))
        FileOpen_Dialog();
    if (Toolbar_Button(ICON_SUITCASE_ZIP, "Extract TAD", "Extract the files of TAD archives", 0))
        TADBatch_Dialog(g_filenameDB);
    if (!ImageView_IsOpen())
        GuiDisable();
    if (Toolbar_Button(ICON_FILE_EXPORT, "Export", "Export the image", TOOLBAR_CTRL | KEY_S))
        SaveFileDialog();
    GuiEnable();
    Views_Toolbar();
    Toolbar_End();
}

static void Draw() {
    float toolbarHeight = 40.0f, statusHeight = 30.0f;
    DrawToolbar((Rectangle) { 0, 0, (float)GetScreenWidth(), toolbarHeight });
    Views_Draw((Rectangle) { 0, toolbarHeight, (float)GetScreenWidth(), (float)GetScreenHeight() - toolbarHeight - statusHeight });
    DrawStatusBar((Rectangle) { 0, (float)GetScreenHeight() - statusHeight, (float)GetScreenWidth(), statusHeight });
    Toolbar_DrawTooltip();
}

int main(int argc, char** argv) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "Shenmue");

    Init();
    if (argc > 1)
        FileOpen_Open(argv[1]);

    while (!WindowShouldClose()) {
        Update();

        BeginDrawing();

        ClearBackground((Color) {28, 28, 28, 255});
        Draw();

        EndDrawing();
    }

    Views_CloseAll();
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    CloseWindow();
    NFD_Quit();
    return 0;
}

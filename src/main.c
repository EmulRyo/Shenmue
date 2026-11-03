#include <stdio.h>
#include <math.h>
#include <string.h>
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
#include "ui/input.h"
#include "ui/toolbar.h"
#include "ui/tooltip.h"

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

// A field of the status bar. The whole text is shown in a tooltip if it is cut
static void DrawField(Rectangle bounds, const char* text, bool path) {
    float padding = (float)GuiGetStyle(STATUSBAR, TEXT_PADDING);
    GuiStatusBar(bounds, Tooltip_Fit(bounds, text, bounds.width - 2.0f * padding, path));
}

// The open file, what it contains, what the mouse does, the progress of the TAD extraction while it runs, and the refresh
// rate. The file and the refresh rate have a fixed width, and the contents and the mouse share the rest
static void DrawStatusBar(Rectangle area) {
    ViewStatus status;
    if (!Views_Status(&status))
        snprintf(status.info, sizeof(status.info), "Open a file, or drop it on the window");
    TADBatch_Progress progress = TADBatch_GetProgress();
    float fileWidth = 280.0f;
    float fpsWidth = 64.0f;
    float progressWidth = progress.running ? 300.0f : 0.0f;
    float infoWidth = floorf(fmaxf(area.width - fileWidth - fpsWidth - progressWidth, 0.0f) * 0.5f);
    float hintWidth = fmaxf(area.width - fileWidth - fpsWidth - progressWidth - infoWidth, 0.0f);

    float x = area.x;
    DrawField((Rectangle) { x, area.y, fileWidth, area.height }, status.file, true);
    x += fileWidth;
    DrawField((Rectangle) { x, area.y, infoWidth, area.height }, status.info, false);
    x += infoWidth;
    DrawField((Rectangle) { x, area.y, hintWidth, area.height }, status.hint, false);
    x += hintWidth;
    if (progress.running) {
        // The text over a bar of the entries extracted of the current file
        float padding = (float)GuiGetStyle(STATUSBAR, TEXT_PADDING);
        GuiStatusBar((Rectangle) { x, area.y, progressWidth, area.height }, NULL);
        float ratio = progress.entryCount > 0 ? (float)(progress.entry + 1) / progress.entryCount : 0.0f;
        DrawRectangleRec((Rectangle) { x + 1, area.y + 1, ratio * (progressWidth - 2), area.height - 2 }, (Color) { 40, 110, 60, 255 });
        GuiLabel((Rectangle) { x + padding, area.y, progressWidth - 2.0f * padding, area.height },
            TextFormat("TAD %u/%u, entry %zu/%zu", progress.file + 1, progress.fileCount, progress.entry + 1, progress.entryCount));
        x += progressWidth;
    }
    DrawField((Rectangle) { x, area.y, area.x + area.width - x, area.height }, TextFormat("%d fps", GetFPS()), false);
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
    Tooltip_Draw();
    Input_EndFrame();
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

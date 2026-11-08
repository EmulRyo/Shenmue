#include <stdio.h>
#include <math.h>
#include <string.h>
#include <nfd.h>
#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "core/hashTable.h"
#include "views/imageView.h"
#include "views/motnView.h"
#include "views/mt5View.h"
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
        snprintf(status.file, sizeof(status.file), "Open a file, or drop it on the window");
    TADBatch_Progress progress = TADBatch_GetProgress();
    float fileWidth = 280.0f;
    float fpsWidth = 64.0f;
    float progressWidth = progress.running || progress.finished ? 300.0f : 0.0f;
    float infoWidth = floorf(fmaxf(area.width - fileWidth - fpsWidth - progressWidth, 0.0f) * 0.5f);
    float hintWidth = fmaxf(area.width - fileWidth - fpsWidth - progressWidth - infoWidth, 0.0f);

    float x = area.x;
    DrawField((Rectangle) { x, area.y, fileWidth, area.height }, status.file, true);
    x += fileWidth;
    DrawField((Rectangle) { x, area.y, infoWidth, area.height }, status.info, false);
    x += infoWidth;
    DrawField((Rectangle) { x, area.y, hintWidth, area.height }, status.hint, false);
    x += hintWidth;
    Rectangle progressBounds = { x, area.y, progressWidth, area.height };
    bool overProgress = CheckCollisionPointRec(GetMousePosition(), progressBounds);
    if (progress.running) {
        // The text over a bar of the entries extracted of all the files
        float padding = (float)GuiGetStyle(STATUSBAR, TEXT_PADDING);
        GuiStatusBar(progressBounds, NULL);
        float ratio = progress.total > 0 ? (float)progress.extracted / progress.total : 0.0f;
        DrawRectangleRec((Rectangle) { x + 1, area.y + 1, ratio * (progressWidth - 2), area.height - 2 }, (Color) { 40, 110, 60, 255 });
        GuiLabel((Rectangle) { x + padding, area.y, progressWidth - 2.0f * padding, area.height },
            TextFormat("TAD %u/%u, file %zu/%zu", progress.file + 1, progress.fileCount, progress.extracted, progress.total));
        if (overProgress)
            Tooltip_Set(progressBounds, TextFormat("Extracting to %s (%.0f s)", progress.outputPath, progress.seconds));
        x += progressWidth;
    }
    else if (progress.finished) {
        // The result stays until it is clicked: left to open the folder, right to close it
        DrawField(progressBounds, TextFormat("Extracted %zu files to %s", progress.extracted, progress.outputPath), false);
        if (overProgress) {
            Tooltip_Set(progressBounds, TextFormat("Extracted %zu files in %.1f s to %s\nClick to open the folder, right click to close",
                progress.extracted, progress.seconds, progress.outputPath));
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                TADBatch_OpenOutput();
                TADBatch_Dismiss();
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                TADBatch_Dismiss();
        }
        x += progressWidth;
    }
    DrawField((Rectangle) { x, area.y, area.x + area.width - x, area.height }, TextFormat("%d fps", GetFPS()), false);
}

// The actions of the application, then those of the open view
static void DrawToolbar(Rectangle area) {
    Toolbar_Begin(area);
    if (Toolbar_Button(ICON_SUITCASE_ZIP, "Extract TAD", "Extract the files of TAD archives", 0))
        TADBatch_Dialog(g_filenameDB);
    if (Toolbar_Button(ICON_FOLDER_FILE_OPEN, "Open", "Open files", TOOLBAR_CTRL | KEY_O))
        FileOpen_Dialog();
    // The image, the model or the model with its motions of the open view
    bool model = MT5View_IsOpen(), motions = MOTNView_CanExport();
    if (!model && !motions && !ImageView_IsOpen())
        GuiDisable();
    const char* tooltip = model ? "Export the model (glTF or OBJ)" : motions ? "Export the model with the motions (glTF)" : "Export the image";
    if (Toolbar_Button(ICON_FILE_EXPORT, "Export", tooltip, TOOLBAR_CTRL | KEY_S)) {
        if (model)
            MT5View_ExportDialog();
        else if (motions)
            MOTNView_ExportDialog();
        else
            ImageView_ExportDialog();
    }
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

    TADBatch_Stop();
    Views_CloseAll();
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    CloseWindow();
    NFD_Quit();
    return 0;
}

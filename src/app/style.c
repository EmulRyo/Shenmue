#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include "raygui.h"
#include "app/style.h"

// Fonts of the user interface on Linux and macOS, after Segoe UI in the folder of Windows
static const char* g_fonts[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
};

// Path of the first font found. Returns false if there is none
static bool FindFont(char* path, size_t size) {
    const char* windowsFolder = getenv("WINDIR");
    if (windowsFolder != NULL) {
        snprintf(path, size, "%s\\Fonts\\segoeui.ttf", windowsFolder);
        if (FileExists(path))
            return true;
    }
    for (size_t i = 0; i < sizeof(g_fonts) / sizeof(g_fonts[0]); i++) {
        if (FileExists(g_fonts[i])) {
            snprintf(path, size, "%s", g_fonts[i]);
            return true;
        }
    }
    return false;
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

void Style_Init(void) {
    Vector2 winDPI = GetWindowScaleDPI();
    char* utf8Chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "1234567890"
        "?¿!¡ ()[]{};,.:-_'\"#+-*=%&<>@/\\^";

    int fontSize = (int)(16.0f * winDPI.y);
    char path[512];
    if (FindFont(path, sizeof(path))) {
        int numCodepoints = 0;
        int* codePoints = LoadCodepoints(utf8Chars, &numCodepoints);
        GuiSetFont(LoadFontEx(path, fontSize, codePoints, numCodepoints));
        UnloadCodepoints(codePoints);
    }
    else
        fputs("[Style_Init]: No font found, using the default one\n", stderr);
    SetStyle(fontSize);
}

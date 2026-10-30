#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include "raygui.h"
#include "app/style.h"

// Size of the text in logical pixels, scaled by the DPI of the window (Segoe UI 9 pt at 96 DPI)
#define STYLE_FONT_SIZE 16

// Fonts of the user interface on Linux and macOS, after Segoe UI in the folder of Windows
static const char* g_fonts[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
};

static char g_fontPath[512];
static float g_fontScale;

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

// Loads the font with the pixels of the screen at the given scale, so it isn't scaled when drawn
static void LoadScaledFont(float scale) {
    const char* utf8Chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "1234567890"
        "?¿!¡ ()[]{};,.:-_'\"#+-*=%&<>@/\\^";

    int numCodepoints = 0;
    int* codePoints = LoadCodepoints(utf8Chars, &numCodepoints);
    Font font = LoadFontEx(g_fontPath, (int)(STYLE_FONT_SIZE * scale), codePoints, numCodepoints);
    UnloadCodepoints(codePoints);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Font previous = GuiGetFont();
    GuiSetFont(font);
    if (previous.texture.id > 0 && previous.texture.id != GetFontDefault().texture.id)
        UnloadFont(previous);
    g_fontScale = scale;
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
    if (FindFont(g_fontPath, sizeof(g_fontPath)))
        LoadScaledFont(GetWindowScaleDPI().y);
    else
        fputs("[Style_Init]: No font found, using the default one\n", stderr);
    SetStyle(STYLE_FONT_SIZE);
}

void Style_Update(void) {
    float scale = GetWindowScaleDPI().y;
    if (g_fontPath[0] != '\0' && scale != g_fontScale)
        LoadScaledFont(scale);
}

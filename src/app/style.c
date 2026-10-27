#include <raylib.h>
#include "raygui.h"
#include "app/style.h"

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

	int numCodepoints = 0;
	int* codePoints = LoadCodepoints(utf8Chars, &numCodepoints);
	int fontSize = (int)(16.0f * winDPI.y);
	Font font = LoadFontEx("C:\\Windows\\Fonts\\segoeui.ttf", fontSize, codePoints, numCodepoints);
	GuiSetFont(font);
	SetStyle(fontSize);
}

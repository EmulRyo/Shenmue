#include <stdio.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "ui/tooltip.h"

#define TOOLTIP_TIME 0.4f // Seconds over a control before its tooltip is shown

typedef struct {
    char        text[1024];
    Rectangle   bounds;     // Of the control under the mouse in this frame, empty if none
    Rectangle   previous;   // In the previous frame
    float       time;       // Since the mouse is over the control
} Tooltip_State;

static Tooltip_State g_tooltip;

void Tooltip_Set(Rectangle bounds, const char* text) {
    g_tooltip.bounds = bounds;
    snprintf(g_tooltip.text, sizeof(g_tooltip.text), "%s", text);
}

void Tooltip_Draw(void) {
    Rectangle bounds = g_tooltip.bounds;
    bool same = bounds.width > 0.0f && bounds.x == g_tooltip.previous.x && bounds.y == g_tooltip.previous.y;
    g_tooltip.time = same ? g_tooltip.time + GetFrameTime() : 0.0f;
    g_tooltip.previous = bounds;
    g_tooltip.bounds = (Rectangle) { 0 };
    if (bounds.width <= 0.0f || g_tooltip.time < TOOLTIP_TIME || IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    Vector2 size = MeasureTextEx(font, g_tooltip.text, fontSize, 1.0f);
    Rectangle box = { bounds.x, bounds.y + bounds.height + 4.0f, size.x + 12.0f, size.y + 8.0f };
    if (box.y + box.height > (float)GetScreenHeight())
        box.y = bounds.y - box.height - 4.0f;
    box.x = fmaxf(fminf(box.x, (float)GetScreenWidth() - box.width), 0.0f);
    DrawRectangleRec(box, (Color) { 50, 50, 52, 255 });
    DrawRectangleLinesEx(box, 1.0f, (Color) { 112, 112, 112, 255 });
    DrawTextEx(font, g_tooltip.text, (Vector2) { box.x + 6.0f, box.y + 4.0f }, fontSize, 1.0f, WHITE);
}

#include <stdbool.h>
#include <raylib.h>
#include "raygui.h"
#include "views/listView.h"

static const Color g_failedColor = { 230, 70, 60, 255 };

void ListView_Draw(Rectangle bounds, char** labels, const bool* failed, int count, int* scroll, int* active, int* focus) {
    GuiListViewEx(bounds, labels, count, scroll, active, focus);
    if (failed == NULL)
        return;

    // The rows as GuiListViewEx places them, from the first visible item (the scroll it has just updated)
    int spacing = GuiGetStyle(LISTVIEW, LIST_ITEMS_SPACING);
    int height = GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT);
    int border = GuiGetStyle(DEFAULT, BORDER_WIDTH);
    int visible = (int)bounds.height / (height + spacing);
    if (visible > count)
        visible = count;
    Rectangle row = {
        bounds.x + spacing,
        bounds.y + spacing + border,
        bounds.width - 2 * spacing - border,
        (float)height,
    };
    if ((height + spacing) * count > bounds.height)
        row.width -= GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    int first = *scroll;
    if (first < 0 || first > count - visible)
        first = 0;
    for (int i = 0; i < visible; i++) {
        if (failed[first + i]) {
            DrawRectangleRec(row, Fade(g_failedColor, 0.25f));
            DrawRectangleRec((Rectangle) { row.x, row.y, 3.0f, row.height }, g_failedColor);
        }
        row.y += height + spacing;
    }
}

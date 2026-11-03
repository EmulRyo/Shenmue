#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "ui/tooltip.h"
#include "ui/properties.h"

#define PROPERTIES_ROW      22.0f   // Height of a row
#define PROPERTIES_TITLE    26.0f
#define PROPERTIES_NAME     0.4f    // Width of the names, of the width of the panel
#define PROPERTIES_MIN      160.0f  // Height of the panel, at least
#define PROPERTIES_SHARE    0.4f    // Of the height of the column

static const Color g_textColor = { 220, 220, 220, 255 };
static const Color g_nameColor = { 150, 150, 150, 255 };
static const Color g_titleColor = { 52, 52, 56, 255 };
static const Color g_sectionColor = { 120, 170, 230, 255 };
static const Color g_lineColor = { 62, 62, 66, 255 };

void Properties_Split(Rectangle column, Rectangle* tree, Rectangle* panel) {
    float margin = 10.0f;
    float height = floorf(fmaxf(column.height * PROPERTIES_SHARE, fminf(PROPERTIES_MIN, column.height * 0.5f)));
    *tree = (Rectangle) { column.x, column.y, column.width, column.height - height - margin };
    *panel = (Rectangle) { column.x, column.y + column.height - height, column.width, height };
}

static void DrawRowText(Rectangle bounds, float x, float width, const char* text, Color color) {
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const char* fitted = Tooltip_Fit(bounds, text, width, false);
    DrawTextEx(GuiGetFont(), fitted, (Vector2) { x, bounds.y + (bounds.height - fontSize) * 0.5f }, fontSize,
        (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

// Places the next row. Returns false if it is out of the view
static bool NextRow(Properties* properties, Rectangle* row) {
    *row = (Rectangle) { properties->view.x, properties->view.y + properties->scroll.y + properties->y, properties->view.width,
        PROPERTIES_ROW };
    properties->y += PROPERTIES_ROW;
    return row->y + row->height > properties->view.y && row->y < properties->view.y + properties->view.height;
}

void Properties_Begin(Properties* properties, Rectangle bounds, const char* title) {
    properties->y = 0.0f;
    if (title == NULL)
        title = "Properties";
    if (!TextIsEqual(properties->title, title)) {
        snprintf(properties->title, sizeof(properties->title), "%s", title);
        properties->scroll = (Vector2) { 0.0f, 0.0f };
    }
    Rectangle header = { bounds.x, bounds.y, bounds.width, PROPERTIES_TITLE };
    DrawRectangleRec(header, g_titleColor);
    DrawRectangleLinesEx(header, 1.0f, g_lineColor);
    DrawRowText(header, header.x + 8.0f, header.width - 16.0f, title, g_textColor);

    // The rows, with a vertical scroll bar if they don't fit
    Rectangle panel = { bounds.x, bounds.y + PROPERTIES_TITLE - 1.0f, bounds.width, bounds.height - PROPERTIES_TITLE + 1.0f };
    float border = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    bool scrollBar = properties->contentHeight > panel.height - 2.0f * border;
    Rectangle content = { 0.0f, 0.0f, panel.width - 2.0f * border - (scrollBar ? (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH) : 0.0f),
        properties->contentHeight };
    GuiScrollPanel(panel, NULL, content, &properties->scroll, &properties->view);
    BeginScissorMode((int)properties->view.x, (int)properties->view.y, (int)properties->view.width, (int)properties->view.height);
}

void Properties_Section(Properties* properties, const char* name) {
    // A space before the sections after the first one
    if (properties->y > 0.0f)
        properties->y += 6.0f;
    Rectangle row;
    if (NextRow(properties, &row)) {
        DrawRowText(row, row.x + 8.0f, row.width - 16.0f, name, g_sectionColor);
        DrawLineEx((Vector2) { row.x + 8.0f, row.y + row.height - 1.0f }, (Vector2) { row.x + row.width - 8.0f, row.y + row.height - 1.0f },
            1.0f, g_lineColor);
    }
}

void Properties_Text(Properties* properties, const char* name, const char* value) {
    Rectangle row;
    if (!NextRow(properties, &row))
        return;
    float nameWidth = floorf(row.width * PROPERTIES_NAME);
    DrawRowText((Rectangle) { row.x, row.y, nameWidth, row.height }, row.x + 8.0f, nameWidth - 12.0f, name, g_nameColor);
    DrawRowText((Rectangle) { row.x + nameWidth, row.y, row.width - nameWidth, row.height }, row.x + nameWidth,
        row.width - nameWidth - 8.0f, value, g_textColor);
}

void Properties_Int(Properties* properties, const char* name, long long value) {
    Properties_Text(properties, name, TextFormat("%lld", value));
}

void Properties_Hex(Properties* properties, const char* name, unsigned long long value) {
    Properties_Text(properties, name, TextFormat("0x%llX", value));
}

void Properties_Float(Properties* properties, const char* name, float value) {
    Properties_Text(properties, name, TextFormat("%g", value));
}

void Properties_Vector(Properties* properties, const char* name, const float* values, int count) {
    char text[256];
    int length = snprintf(text, sizeof(text), "(");
    for (int i = 0; i < count && length < (int)sizeof(text); i++)
        length += snprintf(text + length, sizeof(text) - length, "%s%.3f", i > 0 ? ", " : "", values[i]);
    if (length < (int)sizeof(text))
        snprintf(text + length, sizeof(text) - length, ")");
    Properties_Text(properties, name, text);
}

void Properties_Bool(Properties* properties, const char* name, bool value) {
    Properties_Text(properties, name, value ? "yes" : "no");
}

void Properties_Note(Properties* properties, const char* text) {
    Rectangle row;
    if (NextRow(properties, &row))
        DrawRowText(row, row.x + 8.0f, row.width - 16.0f, text, g_nameColor);
}

void Properties_End(Properties* properties) {
    EndScissorMode();
    properties->contentHeight = properties->y + 4.0f;
}

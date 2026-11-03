#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "ui/input.h"
#include "ui/tooltip.h"
#include "ui/properties.h"

#define PROPERTIES_ROW      22.0f   // Height of a row
#define PROPERTIES_TITLE    26.0f
#define PROPERTIES_NAME     0.4f    // Width of the names, of the width of the panel
#define PROPERTIES_MIN      160.0f  // Height of the panel, at least
#define PROPERTIES_SHARE    0.4f    // Of the height of the column
#define PROPERTIES_RESTORE  22.0f   // Width of the button that restores an edited value
#define PROPERTIES_GAP      3.0f    // Between the boxes of the values of a row
#define PROPERTIES_NUMBER   3       // A single number is as wide as a component of a vector of 3

static const Color g_textColor = { 220, 220, 220, 255 };
static const Color g_nameColor = { 150, 150, 150, 255 };
static const Color g_titleColor = { 52, 52, 56, 255 };
static const Color g_sectionColor = { 120, 170, 230, 255 };
static const Color g_lineColor = { 62, 62, 66, 255 };
static const Color g_fieldColor = { 36, 36, 38, 255 };
static const Color g_readOnlyColor = { 54, 54, 58, 255 };
static const Color g_readOnlyTextColor = { 185, 185, 185, 255 };
static const Color g_switchOnColor = { 47, 95, 143, 255 };
static const Color g_switchOffColor = { 70, 70, 75, 255 };
static const Color g_activeColor = { 47, 95, 143, 255 };
static const Color g_editedColor = { 235, 170, 80, 255 };

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
    properties->field = 0;
    properties->hover = false;
    if (title == NULL)
        title = "Properties";
    if (!TextIsEqual(properties->title, title)) {
        snprintf(properties->title, sizeof(properties->title), "%s", title);
        properties->scroll = (Vector2) { 0.0f, 0.0f };
        properties->active = -1;
        properties->typing = false;
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

// The name of a row at its left, highlighted if its value is edited
static void DrawName(Rectangle row, const char* name, bool edited) {
    float width = floorf(row.width * PROPERTIES_NAME);
    DrawRowText((Rectangle) { row.x, row.y, width, row.height }, row.x + 8.0f, width - 12.0f, name, edited ? g_editedColor : g_nameColor);
}

// The values of a row: between the name and the button that restores an edited value, in every row, so that the values
// are aligned
static Rectangle ValueArea(Rectangle row) {
    float nameWidth = floorf(row.width * PROPERTIES_NAME);
    return (Rectangle) { row.x + nameWidth, row.y + 2.0f, row.width - nameWidth - PROPERTIES_RESTORE - 4.0f, row.height - 4.0f };
}

// Box i of count boxes that share the area of the values. A single number is as wide as a component of a vector, at the
// left
static Rectangle ValueBox(Rectangle row, int i, int count, bool number) {
    Rectangle area = ValueArea(row);
    if (number && count == 1)
        count = PROPERTIES_NUMBER;
    float width = (area.width - PROPERTIES_GAP * (count - 1)) / count;
    return (Rectangle) { area.x + i * (width + PROPERTIES_GAP), area.y, width, area.height };
}

static void DrawBoxText(Rectangle box, const char* text, bool right, Color color) {
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    const char* fitted = Tooltip_Fit(box, text, box.width - 8.0f, false);
    float width = MeasureTextEx(GuiGetFont(), fitted, fontSize, spacing).x;
    DrawTextEx(GuiGetFont(), fitted, (Vector2) { right ? box.x + box.width - 4.0f - width : box.x + 4.0f, box.y + (box.height - fontSize) * 0.5f },
        fontSize, spacing, color);
}

// Values that can't be edited, in boxes without a border: the texts at the left, the numbers at the right
static void ReadOnlyRow(Properties* properties, const char* name, const char* const* values, int count, bool numbers) {
    Rectangle row;
    if (!NextRow(properties, &row))
        return;
    DrawName(row, name, false);
    for (int i = 0; i < count; i++) {
        Rectangle box = ValueBox(row, i, count, numbers);
        DrawRectangleRec(box, g_readOnlyColor);
        DrawBoxText(box, values[i], numbers, g_readOnlyTextColor);
    }
}

void Properties_Text(Properties* properties, const char* name, const char* value) {
    ReadOnlyRow(properties, name, &value, 1, false);
}

void Properties_Int(Properties* properties, const char* name, long long value) {
    const char* text = TextFormat("%lld", value);
    ReadOnlyRow(properties, name, &text, 1, true);
}

void Properties_Hex(Properties* properties, const char* name, unsigned long long value) {
    const char* text = TextFormat("0x%llX", value);
    ReadOnlyRow(properties, name, &text, 1, true);
}

void Properties_Float(Properties* properties, const char* name, float value) {
    const char* text = TextFormat("%.3f", value);
    ReadOnlyRow(properties, name, &text, 1, true);
}

void Properties_Vector(Properties* properties, const char* name, const float* values, int count) {
    // TextFormat uses a rotating buffer of 4 texts
    char texts[4][32];
    const char* pointers[4];
    if (count > 4)
        count = 4;
    for (int i = 0; i < count; i++) {
        snprintf(texts[i], sizeof(texts[i]), "%.3f", values[i]);
        pointers[i] = texts[i];
    }
    ReadOnlyRow(properties, name, pointers, count, true);
}

// A switch, dimmed: it can't be changed
void Properties_Bool(Properties* properties, const char* name, bool value) {
    Rectangle row;
    if (!NextRow(properties, &row))
        return;
    DrawName(row, name, false);
    Rectangle area = ValueArea(row);
    Rectangle track = { area.x, area.y + (area.height - 14.0f) * 0.5f, 28.0f, 14.0f };
    DrawRectangleRounded(track, 1.0f, 8, Fade(value ? g_switchOnColor : g_switchOffColor, 0.8f));
    float knobX = value ? track.x + track.width - 7.0f : track.x + 7.0f;
    DrawCircleV((Vector2) { knobX, track.y + 7.0f }, 5.0f, g_readOnlyTextColor);
    DrawBoxText((Rectangle) { track.x + track.width + 4.0f, area.y, area.width - track.width - 4.0f, area.height }, value ? "yes" : "no",
        false, g_nameColor);
}

// A number field: dragged, or typed after a click. Returns true when the value changes
static bool NumberField(Properties* properties, Rectangle box, bool visible, float* value, float step) {
    int id = properties->field++;
    bool active = properties->active == id;
    if (active && properties->typing) {
        Input_SetTyping();
        if (visible && GuiTextBox(box, properties->text, sizeof(properties->text), true)) {
            // Enter or a click outside: a valid number is applied
            properties->active = -1;
            properties->typing = false;
            char* end;
            float typed = strtof(properties->text, &end);
            if (end != properties->text && isfinite(typed) && typed != *value) {
                *value = typed;
                return true;
            }
        }
        return false;
    }

    bool changed = false;
    Vector2 mouse = GetMousePosition();
    bool hovered = visible && CheckCollisionPointRec(mouse, box) && CheckCollisionPointRec(mouse, properties->view);
    if (hovered && properties->active < 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        properties->active = id;
        properties->dragged = false;
        properties->dragStart = mouse.x;
        properties->startValue = *value;
        active = true;
    }
    if (active) {
        float distance = mouse.x - properties->dragStart;
        if (fabsf(distance) > 3.0f)
            properties->dragged = true;
        if (properties->dragged) {
            bool fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            float dragged = properties->startValue + distance * step * (fine ? 0.1f : 1.0f);
            changed = dragged != *value;
            *value = dragged;
        }
        // Released: a click types the value, a drag ends
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            properties->typing = !properties->dragged;
            if (properties->typing)
                snprintf(properties->text, sizeof(properties->text), "%g", *value);
            else
                properties->active = -1;
        }
    }
    properties->hover |= hovered || (active && properties->dragged);
    if (!visible)
        return changed;

    DrawRectangleRec(box, active ? g_activeColor : g_fieldColor);
    DrawRectangleLinesEx(box, 1.0f, hovered ? g_nameColor : g_lineColor);
    DrawBoxText(box, TextFormat("%.3f", *value), true, g_textColor);
    return changed;
}

bool Properties_EditVector(Properties* properties, const char* name, float* values, const float* original, int count, float step) {
    Rectangle row;
    bool visible = NextRow(properties, &row);
    bool edited = original != NULL && memcmp(values, original, count * sizeof(float)) != 0;
    if (visible)
        DrawName(row, name, edited);
    bool changed = false;
    for (int i = 0; i < count; i++)
        changed |= NumberField(properties, ValueBox(row, i, count, true), visible, &values[i], step);

    Rectangle restore = { row.x + row.width - PROPERTIES_RESTORE - 2.0f, row.y + 2.0f, PROPERTIES_RESTORE, row.height - 4.0f };
    if (visible && edited) {
        if (CheckCollisionPointRec(GetMousePosition(), restore))
            Tooltip_Set(restore, "Restore the value of the file");
        if (GuiButton(restore, GuiIconText(ICON_UNDO, NULL))) {
            memcpy(values, original, count * sizeof(float));
            properties->active = -1;
            properties->typing = false;
            changed = true;
        }
    }
    return changed;
}

bool Properties_EditFloat(Properties* properties, const char* name, float* value, const float* original, float step) {
    return Properties_EditVector(properties, name, value, original, 1, step);
}

void Properties_Note(Properties* properties, const char* text) {
    Rectangle row;
    if (NextRow(properties, &row))
        DrawRowText(row, row.x + 8.0f, row.width - 16.0f, text, g_nameColor);
}

void Properties_End(Properties* properties) {
    EndScissorMode();
    // The fields of the previous frame that aren't added anymore
    if (properties->active >= properties->field) {
        properties->active = -1;
        properties->typing = false;
    }
    if (properties->hover != properties->cursor) {
        SetMouseCursor(properties->hover ? MOUSE_CURSOR_RESIZE_EW : MOUSE_CURSOR_DEFAULT);
        properties->cursor = properties->hover;
    }
    properties->contentHeight = properties->y + 4.0f;
}

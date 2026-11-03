#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <raylib.h>
#include "raygui.h"
#include "ui/toolbar.h"
#include "ui/tooltip.h"

#define TOOLBAR_PADDING     5.0f
#define TOOLBAR_SPACING     4.0f
#define TOOLBAR_SEPARATOR   16.0f
#define TOOLBAR_MAX_KEYS    64

#define KEY_CODE(key)       ((key) & 0xFFFF)
#define KEY_MODIFIERS(key)  ((key) & (TOOLBAR_SHIFT | TOOLBAR_CTRL))

typedef struct {
    Rectangle   area;
    float       x;              // Of the next control
    int         keys[TOOLBAR_MAX_KEYS]; // Of the actions added in the frame, without TOOLBAR_REPEAT
    int         keyCount;
    int         warned[TOOLBAR_MAX_KEYS]; // Keys already reported as reserved or repeated
    int         warnedCount;
    int         pressedColor;   // Of the style, restored at the end
} Toolbar_State;

static Toolbar_State g_toolbar;

static const Color g_backgroundColor = { 37, 37, 38, 255 };
static const Color g_lineColor = { 62, 62, 66, 255 };

static const char* KeyName(int key) {
    static char name[32];
    int code = KEY_CODE(key);
    char base[16];
    switch (code) {
    case KEY_SPACE: snprintf(base, sizeof(base), "Space"); break;
    case KEY_TAB: snprintf(base, sizeof(base), "Tab"); break;
    case KEY_ENTER: snprintf(base, sizeof(base), "Enter"); break;
    case KEY_DELETE: snprintf(base, sizeof(base), "Delete"); break;
    case KEY_HOME: snprintf(base, sizeof(base), "Home"); break;
    case KEY_END: snprintf(base, sizeof(base), "End"); break;
    case KEY_LEFT: snprintf(base, sizeof(base), "Left"); break;
    case KEY_RIGHT: snprintf(base, sizeof(base), "Right"); break;
    case KEY_UP: snprintf(base, sizeof(base), "Up"); break;
    case KEY_DOWN: snprintf(base, sizeof(base), "Down"); break;
    default:
        if (code >= KEY_F1 && code <= KEY_F12)
            snprintf(base, sizeof(base), "F%d", code - KEY_F1 + 1);
        else if ((code >= KEY_A && code <= KEY_Z) || (code >= KEY_ZERO && code <= KEY_NINE))
            snprintf(base, sizeof(base), "%c", code);
        else
            snprintf(base, sizeof(base), "key %d", code);
        break;
    }
    snprintf(name, sizeof(name), "%s%s%s", key & TOOLBAR_CTRL ? "Ctrl+" : "", key & TOOLBAR_SHIFT ? "Shift+" : "", base);
    return name;
}

// Prints once that a key is reserved for the free camera or used by two actions
static void Warn(int key, const char* reason) {
    for (int i = 0; i < g_toolbar.warnedCount; i++) {
        if (g_toolbar.warned[i] == key)
            return;
    }
    if (g_toolbar.warnedCount < TOOLBAR_MAX_KEYS)
        g_toolbar.warned[g_toolbar.warnedCount++] = key;
    printf("[Toolbar]: The key %s %s\n", KeyName(key), reason);
}

static void AddKey(int key) {
    key &= ~TOOLBAR_REPEAT;
    if (KEY_CODE(key) == 0)
        return;
    const int reserved[] = { KEY_W, KEY_A, KEY_S, KEY_D, KEY_Q, KEY_E };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (KEY_MODIFIERS(key) == 0 && KEY_CODE(key) == reserved[i])
            Warn(key, "is reserved for the flight of the camera");
    }
    for (int i = 0; i < g_toolbar.keyCount; i++) {
        if (g_toolbar.keys[i] == key)
            Warn(key, "is used by two actions");
    }
    if (g_toolbar.keyCount < TOOLBAR_MAX_KEYS)
        g_toolbar.keys[g_toolbar.keyCount++] = key;
}

static bool KeyTriggered(int key) {
    int code = KEY_CODE(key);
    if (code == 0 || GuiGetState() == STATE_DISABLED)
        return false;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (shift != ((key & TOOLBAR_SHIFT) != 0) || ctrl != ((key & TOOLBAR_CTRL) != 0))
        return false;
    return IsKeyPressed(code) || ((key & TOOLBAR_REPEAT) && IsKeyPressedRepeat(code));
}

static float ControlWidth(int icon, const char* label) {
    float height = g_toolbar.area.height - 2.0f * TOOLBAR_PADDING;
    if (label == NULL || label[0] == '\0')
        return height;
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float width = MeasureTextEx(GuiGetFont(), label, fontSize, (float)GuiGetStyle(DEFAULT, TEXT_SPACING)).x;
    return width + (icon != ICON_NONE ? 20.0f : 0.0f) + 16.0f;
}

// Places the next control, and registers its key and its tooltip. Returns false if it doesn't fit in the toolbar: its key
// still works
static bool Place(float width, const char* name, int key, Rectangle* bounds) {
    AddKey(key);
    *bounds = (Rectangle) { g_toolbar.x, g_toolbar.area.y + TOOLBAR_PADDING, width, g_toolbar.area.height - 2.0f * TOOLBAR_PADDING };
    g_toolbar.x += width + TOOLBAR_SPACING;
    if (bounds->x + bounds->width > g_toolbar.area.x + g_toolbar.area.width)
        return false;
    if (CheckCollisionPointRec(GetMousePosition(), *bounds))
        Tooltip_Set(*bounds, KEY_CODE(key) != 0 ? TextFormat("%s (%s)", name, KeyName(key)) : name);
    return true;
}

void Toolbar_Begin(Rectangle area) {
    g_toolbar.area = area;
    g_toolbar.x = area.x + TOOLBAR_PADDING;
    g_toolbar.keyCount = 0;
    DrawRectangleRec(area, g_backgroundColor);
    DrawLineEx((Vector2) { area.x, area.y + area.height - 0.5f }, (Vector2) { area.x + area.width, area.y + area.height - 0.5f }, 1.0f, g_lineColor);
    // The style of the application draws the pressed controls like the normal ones: highlight the pressed toggles
    g_toolbar.pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
}

void Toolbar_Separator(void) {
    float x = g_toolbar.x - TOOLBAR_SPACING + TOOLBAR_SEPARATOR * 0.5f;
    if (x < g_toolbar.area.x + g_toolbar.area.width)
        DrawLineEx((Vector2) { x, g_toolbar.area.y + TOOLBAR_PADDING + 4.0f },
            (Vector2) { x, g_toolbar.area.y + g_toolbar.area.height - TOOLBAR_PADDING - 4.0f }, 1.0f, g_lineColor);
    g_toolbar.x += TOOLBAR_SEPARATOR - TOOLBAR_SPACING;
}

bool Toolbar_Button(int icon, const char* label, const char* name, int key) {
    Rectangle bounds;
    bool clicked = false;
    if (Place(ControlWidth(icon, label), name, key, &bounds))
        clicked = GuiButton(bounds, GuiIconText(icon, label));
    return clicked || KeyTriggered(key);
}

bool Toolbar_Toggle(int icon, const char* label, const char* name, int key, bool* value) {
    Rectangle bounds;
    bool previous = *value;
    if (Place(ControlWidth(icon, label), name, key, &bounds))
        GuiToggle(bounds, GuiIconText(icon, label), value);
    if (KeyTriggered(key))
        *value = !previous;
    return *value != previous;
}

bool Toolbar_Choice(const char* options, const char* name, int key, int* active) {
    int previous = *active;
    int count = 0;
    const char** texts = TextSplit(options, ';', &count);
    // TextSplit uses a static buffer, which GuiToggle may use too
    char labels[8][64];
    if (count > 8)
        count = 8;
    for (int i = 0; i < count; i++)
        snprintf(labels[i], sizeof(labels[i]), "%s", texts[i]);
    // A single key for the whole group
    AddKey(key);
    for (int i = 0; i < count; i++) {
        Rectangle bounds;
        bool pressed = i == previous;
        if (Place(ControlWidth(ICON_NONE, labels[i]), name, 0, &bounds)) {
            if (KEY_CODE(key) != 0 && CheckCollisionPointRec(GetMousePosition(), bounds))
                Tooltip_Set(bounds, TextFormat("%s (%s)", name, KeyName(key)));
            GuiToggle(bounds, labels[i], &pressed);
        }
        // A click on the pressed option keeps it
        if (pressed && i != previous)
            *active = i;
        if (i < count - 1)
            g_toolbar.x -= TOOLBAR_SPACING - 1.0f;
    }
    if (count > 0 && KeyTriggered(key))
        *active = (previous + 1) % count;
    return *active != previous;
}

void Toolbar_End(void) {
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, g_toolbar.pressedColor);
}

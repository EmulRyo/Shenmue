#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include "raygui.h"
#include "ui/input.h"
#include "ui/tooltip.h"
#include "ui/tree.h"

#define TREE_ROW        24.0f   // Height of a row
#define TREE_INDENT     14.0f   // Of each level of groups
#define TREE_ICON       16.0f
#define TREE_FILTER     28.0f   // Height of the filter

static const Color g_textColor = { 220, 220, 220, 255 };
static const Color g_dimmedColor = { 130, 130, 130, 255 };
static const Color g_groupColor = { 52, 52, 56, 255 };
static const Color g_hoverColor = { 62, 62, 68, 255 };
static const Color g_selectedColor = { 47, 95, 143, 255 };
static const Color g_failedColor = { 230, 70, 60, 255 };
static const Color g_editedColor = { 235, 170, 80, 255 };

// Case-insensitive. An empty filter is in every text
static bool Contains(const char* text, const char* filter) {
    if (filter[0] == '\0')
        return true;
    for (; *text != '\0'; text++) {
        size_t i = 0;
        while (filter[i] != '\0' && tolower((unsigned char)text[i]) == tolower((unsigned char)filter[i]))
            i++;
        if (filter[i] == '\0')
            return true;
    }
    return false;
}

static TreeGroup* Parent(Tree* tree) {
    return tree->depth > 0 ? &tree->groups[tree->depth - 1] : NULL;
}

// The rows of the current group are shown
static bool IsOpen(Tree* tree) {
    return tree->depth == 0 || Parent(tree)->open;
}

// Row of the content on the screen
static Rectangle RowBounds(const Tree* tree, float y) {
    return (Rectangle) { tree->view.x, tree->view.y + tree->scroll.y + y, tree->view.width, TREE_ROW };
}

static bool IsVisibleRow(const Tree* tree, Rectangle row) {
    return row.y + row.height > tree->view.y && row.y < tree->view.y + tree->view.height;
}

static bool IsHovered(const Tree* tree, Rectangle bounds) {
    Vector2 mouse = GetMousePosition();
    return CheckCollisionPointRec(mouse, tree->view) && CheckCollisionPointRec(mouse, bounds);
}

static bool IsClicked(const Tree* tree, Rectangle bounds) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsHovered(tree, bounds);
}

static void DrawRowText(Rectangle row, float x, float right, const char* text, Color color) {
    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const char* fitted = Tooltip_Fit(row, text, right - x, false);
    DrawTextEx(font, fitted, (Vector2) { x, row.y + (row.height - fontSize) * 0.5f }, fontSize,
        (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

static void DrawIcon(int icon, float x, Rectangle row, Color color) {
    GuiDrawIcon(icon, (int)x, (int)(row.y + (row.height - TREE_ICON) * 0.5f), 1, color);
}

// The eye at the right of a row. Returns true when it is clicked
static bool Toggle(const Tree* tree, Rectangle row, bool* visible, bool hiddenByParent) {
    Rectangle eye = { row.x + row.width - TREE_ICON - 8.0f, row.y, TREE_ICON + 8.0f, row.height };
    bool clicked = IsClicked(tree, eye);
    if (clicked)
        *visible = !*visible;
    if (IsHovered(tree, eye))
        Tooltip_Set(eye, *visible ? "Hide" : "Show");
    DrawIcon(*visible ? ICON_EYE_ON : ICON_EYE_OFF, eye.x + 4.0f, row, *visible && !hiddenByParent ? g_textColor : g_dimmedColor);
    return clicked;
}

void Tree_Begin(Tree* tree, Rectangle bounds) {
    tree->depth = 0;
    tree->y = 0.0f;

    // The filter, edited after a click on it or Ctrl+F, until Enter or a click outside
    Rectangle box = { bounds.x, bounds.y, bounds.width - TREE_FILTER - 4.0f, TREE_FILTER };
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_F))
        tree->editing = true;
    if (GuiTextBox(box, tree->filter, TREE_FILTER_SIZE, tree->editing))
        tree->editing = !tree->editing;
    if (tree->editing)
        Input_SetTyping();
    else if (tree->filter[0] == '\0') {
        float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
        DrawTextEx(GuiGetFont(), "Filter (Ctrl+F)", (Vector2) { box.x + 8.0f, box.y + (box.height - fontSize) * 0.5f }, fontSize,
            (float)GuiGetStyle(DEFAULT, TEXT_SPACING), g_dimmedColor);
    }
    Rectangle clear = { box.x + box.width + 4.0f, box.y, TREE_FILTER, TREE_FILTER };
    if (tree->filter[0] == '\0')
        GuiDisable();
    if (CheckCollisionPointRec(GetMousePosition(), clear))
        Tooltip_Set(clear, "Clear the filter");
    if (GuiButton(clear, GuiIconText(ICON_CROSS_SMALL, NULL))) {
        tree->filter[0] = '\0';
        tree->editing = false;
    }
    GuiEnable();

    // The rows, with a vertical scroll bar if they don't fit
    Rectangle panel = { bounds.x, bounds.y + TREE_FILTER + 4.0f, bounds.width, bounds.height - TREE_FILTER - 4.0f };
    float border = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    bool scrollBar = tree->contentHeight > panel.height - 2.0f * border;
    Rectangle content = { 0.0f, 0.0f, panel.width - 2.0f * border - (scrollBar ? (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH) : 0.0f),
        tree->contentHeight };
    GuiScrollPanel(panel, NULL, content, &tree->scroll, &tree->view);
    BeginScissorMode((int)tree->view.x, (int)tree->view.y, (int)tree->view.width, (int)tree->view.height);
}

void Tree_BeginGroup(Tree* tree, const char* name, bool* expanded, bool* visible) {
    if (tree->depth == TREE_MAX_DEPTH)
        return;
    const TreeGroup* parent = Parent(tree);
    bool parentOpen = IsOpen(tree);
    TreeGroup* group = &tree->groups[tree->depth++];
    group->y = tree->y;
    snprintf(group->name, sizeof(group->name), "%s", name);
    group->expanded = expanded;
    group->visible = visible;
    group->matched = (parent != NULL && parent->matched) || (tree->filter[0] != '\0' && Contains(name, tree->filter));
    group->hidden = (parent != NULL && parent->hidden) || (visible != NULL && !*visible);
    group->open = parentOpen && (tree->filter[0] != '\0' || *expanded);
    group->changed = false;
    group->count = 0;
    group->matches = 0;
    if (parentOpen)
        tree->y += TREE_ROW;
}

bool Tree_EndGroup(Tree* tree) {
    if (tree->depth == 0)
        return false;
    TreeGroup* group = &tree->groups[--tree->depth];
    TreeGroup* parent = Parent(tree);
    if (parent != NULL) {
        parent->count += group->count;
        parent->matches += group->matches;
    }
    if (!IsOpen(tree))
        return false;
    // Without the groups that have nothing that contains the filter
    if (tree->filter[0] != '\0' && group->matches == 0) {
        tree->y = group->y;
        return false;
    }

    Rectangle row = RowBounds(tree, group->y);
    if (!IsVisibleRow(tree, row))
        return false;
    bool hiddenByParent = parent != NULL && parent->hidden;
    DrawRectangleRec(row, IsHovered(tree, row) ? g_hoverColor : g_groupColor);
    float x = row.x + 4.0f + tree->depth * TREE_INDENT;
    float right = row.x + row.width - (group->visible != NULL ? TREE_ICON + 12.0f : 4.0f);
    bool changed = group->visible != NULL && Toggle(tree, row, group->visible, hiddenByParent);
    if (!changed && IsClicked(tree, (Rectangle) { row.x, row.y, right - row.x, row.height }))
        *group->expanded = !*group->expanded;

    Color color = group->hidden ? g_dimmedColor : g_textColor;
    DrawIcon(group->open ? ICON_ARROW_DOWN_FILL : ICON_ARROW_RIGHT_FILL, x, row, color);
    // The count at the right, of the items that contain the filter if there is one
    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    const char* count = tree->filter[0] != '\0' ? TextFormat("%d/%d", group->matches, group->count) : TextFormat("%d", group->count);
    float countWidth = MeasureTextEx(font, count, fontSize, spacing).x;
    DrawTextEx(font, count, (Vector2) { right - countWidth, row.y + (row.height - fontSize) * 0.5f }, fontSize, spacing, g_dimmedColor);
    DrawRowText(row, x + TREE_ICON + 4.0f, right - countWidth - 8.0f, group->name, color);
    return changed;
}

bool Tree_Item(Tree* tree, const char* text, int flags, bool* visible) {
    TreeGroup* parent = Parent(tree);
    bool matched = (parent != NULL && parent->matched) || Contains(text, tree->filter);
    if (parent != NULL) {
        parent->count++;
        parent->matches += matched;
    }
    bool selected = (flags & TREE_SELECTED) != 0;
    if (!matched)
        return false;
    if (!IsOpen(tree)) {
        // Revealed in the next frame, when its groups are expanded
        if (selected && tree->reveal && tree->revealExpand) {
            for (int i = 0; i < tree->depth; i++)
                *tree->groups[i].expanded = true;
            tree->revealing = true;
        }
        return false;
    }

    float y = tree->y;
    tree->y += TREE_ROW;
    if (selected && tree->reveal) {
        if (y < -tree->scroll.y)
            tree->scroll.y = -y;
        else if (y + TREE_ROW > -tree->scroll.y + tree->view.height)
            tree->scroll.y = -(y + TREE_ROW - tree->view.height);
        tree->reveal = false;
    }
    Rectangle row = RowBounds(tree, y);
    if (!IsVisibleRow(tree, row))
        return false;

    bool hidden = parent != NULL && parent->hidden;
    if (selected)
        DrawRectangleRec(row, g_selectedColor);
    else if (IsHovered(tree, row))
        DrawRectangleRec(row, g_hoverColor);
    if (flags & TREE_FAILED) {
        DrawRectangleRec(row, Fade(g_failedColor, 0.25f));
        DrawRectangleRec((Rectangle) { row.x, row.y, 3.0f, row.height }, g_failedColor);
    }
    float right = row.x + row.width - (visible != NULL ? TREE_ICON + 12.0f : 4.0f);
    bool toggled = visible != NULL && Toggle(tree, row, visible, hidden);
    bool clicked = !toggled && IsClicked(tree, (Rectangle) { row.x, row.y, right - row.x, row.height });

    float x = row.x + 8.0f + tree->depth * TREE_INDENT;
    Color color = hidden || (visible != NULL && !*visible) || (flags & TREE_DIMMED) ? g_dimmedColor : g_textColor;
    if (flags & TREE_EDITED)
        color = hidden || (visible != NULL && !*visible) || (flags & TREE_DIMMED) ? Fade(g_editedColor, 0.6f) : g_editedColor;
    if (flags & TREE_FAILED) {
        DrawIcon(ICON_WARNING, x, row, g_failedColor);
        x += TREE_ICON + 4.0f;
    }
    DrawRowText(row, x, right, text, color);
    return clicked;
}

void Tree_End(Tree* tree) {
    while (tree->depth > 0)
        Tree_EndGroup(tree);
    EndScissorMode();
    tree->contentHeight = tree->y;
    // The selected item wasn't found, or it is filtered out
    if (tree->reveal && !tree->revealing)
        tree->reveal = false;
    tree->revealing = false;
}

void Tree_Reveal(Tree* tree, bool expand) {
    tree->reveal = true;
    tree->revealExpand = expand;
}

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "raygui.h"
#include "formats/font/fon.h"
#include "ui/toolbar.h"
#include "views/fonView.h"

#define FONVIEW_ATLAS_COLUMNS 64
#define FONVIEW_ATLAS_BORDER 1    // Transparent pixels around each glyph, so that the scaled glyphs don't take pixels of their neighbours
#define FONVIEW_GAP 4.0f        // Between the cells, so that the glyphs that touch their edges aren't taken for marks of the next one
#define FONVIEW_MARGIN 10.0f

static const int g_zooms[] = { 1, 2, 3, 4, 6, 8 };
#define FONVIEW_ZOOM_COUNT ((int)(sizeof(g_zooms) / sizeof(g_zooms[0])))
// With the aspect corrected, the pixels of the 24-pixel glyphs are 2 wide and 3 high, times the zoom: the glyphs are
// drawn 2/3 as wide, without columns of different widths. Named by the height of a pixel
#define FONVIEW_ASPECT_ZOOM_COUNT 4
#define FONVIEW_ASPECT_ZOOMS "3x;6x;9x;12x"

typedef struct {
    bool        open;
    char        path[1024];     // Of the open file
    FON_Font    font;
    uint32_t    nonEmpty;       // Glyphs with ink
    Texture     atlas;          // The glyphs in rows of FONVIEW_ATLAS_COLUMNS, white over transparent, with a border
    uint32_t*   shown;          // Indices of the glyphs in the grid
    uint32_t    shownCount;
    bool        shownEmpty;     // The value of the option when shown was filled
    Vector2     scroll;
    Rectangle   panelView;      // Of the cells, without the scroll bar
    int32_t     hovered;        // Glyph under the mouse, -1 if none
} FONView_State;

// Kept when another font is opened
typedef struct {
    int         zoom;           // Index in g_zooms
    int         aspectZoom;     // With the aspect corrected: the pixels are 2 × (aspectZoom + 1) by 3 × (aspectZoom + 1)
    bool        aspect;         // Draw the 24-pixel glyphs 2/3 as wide (see FONView_Toolbar)
    bool        showEmpty;
} FONView_Options;

static FONView_State g_view;
static FONView_Options g_options = { .zoom = 2, .aspectZoom = 0, .aspect = true, .showEmpty = false };

static void FillShown(void) {
    g_view.shownCount = 0;
    for (uint32_t i = 0; i < g_view.font.count; i++) {
        if (g_options.showEmpty || !FON_IsEmpty(&g_view.font, i))
            g_view.shown[g_view.shownCount++] = i;
    }
    g_view.shownEmpty = g_options.showEmpty;
}

static Texture LoadAtlas(const FON_Font* font) {
    int columns = (int)(font->count < FONVIEW_ATLAS_COLUMNS ? font->count : FONVIEW_ATLAS_COLUMNS);
    int rows = (int)((font->count + columns - 1) / columns);
    int cellWidth = (int)font->width + 2 * FONVIEW_ATLAS_BORDER, cellHeight = (int)font->height + 2 * FONVIEW_ATLAS_BORDER;
    int width = columns * cellWidth, height = rows * cellHeight;
    Color* pixels = calloc((size_t)width * height, sizeof(Color));
    if (pixels == NULL)
        return (Texture) { 0 };
    for (uint32_t glyph = 0; glyph < font->count; glyph++) {
        int left = (int)(glyph % columns) * cellWidth + FONVIEW_ATLAS_BORDER;
        int top = (int)(glyph / columns) * cellHeight + FONVIEW_ATLAS_BORDER;
        for (uint32_t y = 0; y < font->height; y++) {
            for (uint32_t x = 0; x < font->width; x++) {
                if (FON_Pixel(font, glyph, x, y))
                    pixels[(size_t)(top + y) * width + left + x] = WHITE;
            }
        }
    }
    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

void FONView_Close(void) {
    if (g_view.atlas.id > 0)
        UnloadTexture(g_view.atlas);
    FON_Free(&g_view.font);
    free(g_view.shown);
    memset(&g_view, 0, sizeof(g_view));
}

bool FONView_Open(const char* filepath) {
    FON_Font font;
    if (!FON_Load(filepath, &font))
        return false;
    FONView_Close();
    g_view.font = font;
    g_view.shown = malloc(sizeof(uint32_t) * font.count);
    g_view.atlas = LoadAtlas(&font);
    if (g_view.shown == NULL || g_view.atlas.id == 0) {
        fputs("[FONView_Open]: Failed to create the texture of the glyphs\n", stderr);
        FONView_Close();
        return false;
    }
    for (uint32_t i = 0; i < font.count; i++)
        g_view.nonEmpty += !FON_IsEmpty(&font, i);
    FillShown();
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.hovered = -1;
    g_view.open = true;
    printf("%s: %u glyphs of %ux%u (%u with ink), %s\n", GetFileName(filepath), font.count, font.width, font.height, g_view.nonEmpty,
        font.encoding == FON_ENCODING_BYTE ? "single-byte codes" : "JIS X 0208");
    return true;
}

bool FONView_IsOpen(void) {
    return g_view.open;
}

// The 24-pixel glyphs look like a 16-pixel design stretched to 24 columns (see the FON page of the wiki): drawn with square
// pixels they are too wide, e.g. the square of 21x14 pixels. Drawn 2/3 as wide, they have the proportions of the 16x15
// glyphs of the single-byte fonts
static bool AspectCorrected(void) {
    return g_options.aspect && g_view.font.width == 24;
}

void FONView_Toolbar(void) {
    if (AspectCorrected())
        Toolbar_Choice(FONVIEW_ASPECT_ZOOMS, "Zoom of the glyphs: pixels of 2x3 screen pixels at 3x", KEY_Z, &g_options.aspectZoom);
    else
        Toolbar_Choice("1x;2x;3x;4x;6x;8x", "Zoom of the glyphs", KEY_Z, &g_options.zoom);
    if (g_view.font.width != 24)
        GuiDisable();
    Toolbar_Toggle(ICON_ZOOM_CENTER, "2:3", "Correct the aspect: draw the 24-pixel glyphs 2/3 as wide, as the 16-pixel design they seem "
        "to be stretched from", KEY_X, &g_options.aspect);
    GuiEnable();
    Toolbar_Toggle(ICON_GRID, "Empty", "Show the empty glyphs", KEY_H, &g_options.showEmpty);
}

void FONView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    if (g_view.shownEmpty != g_options.showEmpty) {
        FillShown();
        g_view.scroll = (Vector2) { 0.0f, 0.0f };
    }

    // Ctrl+wheel zooms, the wheel alone scrolls the panel
    bool aspect = AspectCorrected();
    int* zoom = aspect ? &g_options.aspectZoom : &g_options.zoom;
    int zoomCount = aspect ? FONVIEW_ASPECT_ZOOM_COUNT : FONVIEW_ZOOM_COUNT;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && CheckCollisionPointRec(GetMousePosition(), area)) {
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f && *zoom < zoomCount - 1)
            (*zoom)++;
        else if (wheel < 0.0f && *zoom > 0)
            (*zoom)--;
    }
    if (*zoom < 0 || *zoom >= zoomCount)
        *zoom = 0;

    // The zoom is in pixels of the screen: with a scale of the display (e.g. 125%), the coordinates of the window are
    // scaled, so that a pixel of the glyph would be drawn 3.75 pixels wide at 3x, some columns 3 and others 4. The cells
    // are placed in pixels of the screen, then converted to the coordinates of the window
    const FON_Font* font = &g_view.font;
    float dpi = GetWindowScaleDPI().x;
    if (dpi <= 0.0f)
        dpi = 1.0f;
    float scaleX, scaleY; // Size of a pixel of the glyph
    if (aspect) {
        scaleX = 2.0f * (g_options.aspectZoom + 1) / dpi;
        scaleY = 3.0f * (g_options.aspectZoom + 1) / dpi;
    }
    else
        scaleX = scaleY = (float)g_zooms[g_options.zoom] / dpi;
    float gap = roundf(FONVIEW_GAP * dpi) / dpi;
    float glyphWidth = font->width * scaleX, glyphHeight = font->height * scaleY;
    float cellWidth = glyphWidth + gap, cellHeight = glyphHeight + gap;
    // The columns that fit with the scroll bar, so that they don't change when it appears
    float scrollBarWidth = (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    float border = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    float usable = area.width - 2.0f * border - scrollBarWidth - 2.0f * FONVIEW_MARGIN + gap;
    uint32_t columns = usable > cellWidth ? (uint32_t)(usable / cellWidth) : 1;
    // The single-byte codes in rows of 16, as the code tables
    if (font->encoding == FON_ENCODING_BYTE && g_options.showEmpty && columns >= 16)
        columns = 16;
    uint32_t rows = (g_view.shownCount + columns - 1) / columns;
    Rectangle content = { 0.0f, 0.0f, area.width - 2.0f * border - scrollBarWidth,
        rows * cellHeight - gap + 2.0f * FONVIEW_MARGIN };

    Vector2 scroll = g_view.scroll;
    GuiScrollPanel(area, NULL, content, &g_view.scroll, &g_view.panelView);
    if (ctrl)
        g_view.scroll = scroll; // The wheel zoomed
    Rectangle view = g_view.panelView;
    DrawRectangleRec(view, (Color) { 45, 45, 48, 255 });

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    // On a pixel of the screen
    float originX = roundf((view.x + FONVIEW_MARGIN + g_view.scroll.x) * dpi) / dpi;
    float originY = roundf((view.y + FONVIEW_MARGIN + g_view.scroll.y) * dpi) / dpi;
    int firstRow = (int)floorf((view.y - originY) / cellHeight);
    int lastRow = (int)floorf((view.y + view.height - originY) / cellHeight);
    if (firstRow < 0)
        firstRow = 0;

    Vector2 mouse = GetMousePosition();
    g_view.hovered = -1;
    bool mouseInView = CheckCollisionPointRec(mouse, view);
    uint32_t atlasColumns = font->count < FONVIEW_ATLAS_COLUMNS ? font->count : FONVIEW_ATLAS_COLUMNS;
    for (int row = firstRow; row <= lastRow && (uint32_t)row < rows; row++) {
        for (uint32_t column = 0; column < columns; column++) {
            uint32_t index = (uint32_t)row * columns + column;
            if (index >= g_view.shownCount)
                break;
            uint32_t glyph = g_view.shown[index];
            Rectangle cell = { originX + column * cellWidth, originY + row * cellHeight, glyphWidth, glyphHeight };
            bool hovered = mouseInView && CheckCollisionPointRec(mouse, (Rectangle) { cell.x, cell.y, cellWidth, cellHeight });
            if (hovered)
                g_view.hovered = (int32_t)glyph;
            DrawRectangleRec(cell, hovered ? (Color) { 60, 80, 120, 255 } : (Color) { 20, 20, 22, 255 });
            Rectangle source = { (float)(glyph % atlasColumns * (font->width + 2 * FONVIEW_ATLAS_BORDER) + FONVIEW_ATLAS_BORDER),
                (float)(glyph / atlasColumns * (font->height + 2 * FONVIEW_ATLAS_BORDER) + FONVIEW_ATLAS_BORDER),
                (float)font->width, (float)font->height };
            DrawTexturePro(g_view.atlas, source, cell, (Vector2) { 0.0f, 0.0f }, 0.0f, WHITE);
        }
    }
    EndScissorMode();
}

void FONView_Status(ViewStatus* status) {
    const FON_Font* font = &g_view.font;
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    if (g_view.hovered < 0) {
        snprintf(status->info, sizeof(status->info), "%u glyphs of %ux%u (%u empty), %s", font->count, font->width, font->height,
            font->count - g_view.nonEmpty, font->encoding == FON_ENCODING_BYTE ? "single-byte codes" : "JIS X 0208 order");
    }
    else {
        uint32_t glyph = (uint32_t)g_view.hovered;
        uint16_t code = FON_Code(font, glyph);
        if (font->encoding == FON_ENCODING_BYTE)
            snprintf(status->info, sizeof(status->info), "Glyph %u, code 0x%02X", glyph, code);
        else
            snprintf(status->info, sizeof(status->info), "Glyph %u, JIS 0x%04X (row %u, cell %u), Shift-JIS 0x%04X", glyph, code,
                glyph / 94 + 1, glyph % 94 + 1, FON_JISToShiftJIS(code));
    }
    snprintf(status->hint, sizeof(status->hint), "Wheel: scroll, Ctrl+wheel: zoom");
}

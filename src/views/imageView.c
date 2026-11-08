#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <nfd.h>
#include <raylib.h>
#include "raygui.h"
#include "core/file.h"
#include "formats/texture/pvr.h"
#include "formats/texture/spr.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/imageView.h"

#define IMAGEVIEW_LIST_WIDTH    300.0f
#define IMAGEVIEW_MARGIN        10.0f

static Image g_image;
static Texture g_texture;
static char g_path[1024];

// Zooms of the wheel, in pixels of the screen per pixel of the image: whole numbers above 1, so that all the pixels of the
// image are drawn with the same size
static const float g_zooms[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f };
#define IMAGEVIEW_ZOOM_COUNT ((int)(sizeof(g_zooms) / sizeof(g_zooms[0])))

typedef enum {
    ZOOM_CUSTOM = -1,   // Chosen with the wheel
    ZOOM_FIT,           // As large as fits in the view
    ZOOM_ACTUAL,        // A pixel of the screen per pixel of the image
} ZoomMode;

// How the image is shown. It is kept when another texture of the same SPR file is selected
static struct {
    int         mode;       // ZoomMode
    float       zoom;       // Pixels of the screen per pixel of the image
    Vector2     center;     // Point of the image at the center of the view, in pixels of the image
    bool        dragging;
    Vector2     dragStart;  // Mouse position when the drag started, and the center then
    Vector2     dragCenter;
    int         filter;     // Filter of the texture, -1 if not set yet
    int         hoveredX;   // Pixel of the image under the mouse, -1 if none
    int         hoveredY;
} g_zoom = { .mode = ZOOM_FIT, .zoom = 1.0f, .filter = -1, .hoveredX = -1, .hoveredY = -1 };

static void ResetZoom(void) {
    g_zoom.mode = ZOOM_FIT;
    g_zoom.dragging = false;
}

// The textures of the open SPR file, shown in a list when it has several
static struct {
    SPR_File    file;
    int         selected;   // -1 if none
    bool        failed;     // The selected one can't be decoded
    bool        expanded;
    Tree        tree;
    Properties  properties;
} g_spr = { .selected = -1, .expanded = true };

static void Unload(void) {
    if (g_image.data != NULL)
        UnloadImage(g_image);
    g_image = (Image) { 0 };
    if (g_texture.id > 0)
        UnloadTexture(g_texture);
    g_texture = (Texture) { 0 };
}

// Takes the ownership of pixels
static void Show(uint32_t* pixels, uint16_t width, uint16_t height) {
    Unload();
    g_image.data = pixels;
    g_image.width = width;
    g_image.height = height;
    g_image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    g_image.mipmaps = 1;

    float ar = (float)width / (float)height;
    // Draw the image on top of the checkered pattern
    Image img2 = GenImageChecked(width, height, width / 8, (int)(ar * height / 8), (Color) { 100, 100, 100, 255 }, (Color) { 150, 150, 150, 255 });
    ImageDrawImage(&img2, g_image, 0, 0, WHITE);
    g_texture = LoadTextureFromImage(img2);
    UnloadImage(img2);
    g_zoom.filter = -1;
    g_zoom.center = (Vector2) { width / 2.0f, height / 2.0f };
    g_zoom.hoveredX = g_zoom.hoveredY = -1;
}

static bool HasList(void) {
    return g_spr.file.count > 1;
}

// Shows the texture index of the SPR file; on error, the view stays open with nothing shown. reveal scrolls the list to it
static void SelectTexture(int index, bool reveal) {
    if (index < 0 || index >= (int)g_spr.file.count)
        return;
    g_spr.selected = index;
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    g_spr.failed = !SPR_Decode(&g_spr.file, (uint32_t)index, &width, &height, &data);
    if (g_spr.failed)
        Unload();
    else
        Show(data, width, height);
    if (reveal)
        Tree_Reveal(&g_spr.tree, true);
}

static void CloseSPR(void) {
    SPR_Close(&g_spr.file);
    g_spr.selected = -1;
    g_spr.failed = false;
    g_spr.tree = (Tree) { 0 };
    g_spr.properties = (Properties) { 0 };
}

bool ImageView_OpenPVR(const char* filepath) {
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    if (!PVR_Load(filepath, &width, &height, &data))
        return false;
    CloseSPR();
    ResetZoom();
    Show(data, width, height);
    snprintf(g_path, sizeof(g_path), "%s", filepath);
    return true;
}

bool ImageView_OpenSPR(const char* filepath) {
    SPR_File file;
    if (!SPR_Open(filepath, &file))
        return false;
    // Without any texture that can be decoded, the file isn't opened
    uint32_t first = 0;
    while (first < file.count && !file.textures[first].pvr)
        first++;
    if (first == file.count) {
        SPR_Close(&file);
        return false;
    }
    CloseSPR();
    ResetZoom();
    g_spr.file = file;
    SelectTexture((int)first, false);
    if (g_spr.failed && !HasList()) {
        CloseSPR();
        return false;
    }
    snprintf(g_path, sizeof(g_path), "%s", filepath);
    return true;
}

void ImageView_Close(void) {
    Unload();
    CloseSPR();
}

bool ImageView_IsOpen(void) {
    return g_texture.id > 0 || g_spr.file.count > 0;
}

bool ImageView_HasToolbar(void) {
    return true;
}

void ImageView_Toolbar(void) {
    if (Toolbar_Choice("Fit;1:1", "Fit the image to the view or show it at its real size", KEY_Z, &g_zoom.mode))
        g_zoom.dragging = false;
    if (!HasList())
        return;
    Toolbar_Separator();
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous texture", TOOLBAR_REPEAT | KEY_UP))
        SelectTexture(g_spr.selected - 1, true);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next texture", TOOLBAR_REPEAT | KEY_DOWN))
        SelectTexture(g_spr.selected + 1, true);
}

static void DrawList(Rectangle bounds) {
    Tree* tree = &g_spr.tree;
    int clicked = -1;
    Tree_Begin(tree, bounds);
    Tree_BeginGroup(tree, "Textures", &g_spr.expanded, NULL);
    for (uint32_t i = 0; i < g_spr.file.count; i++) {
        const SPR_Texture* texture = &g_spr.file.textures[i];
        bool selected = (int)i == g_spr.selected;
        int flags = (selected ? TREE_SELECTED : 0) | (!texture->pvr || (selected && g_spr.failed) ? TREE_FAILED : 0);
        const char* label = texture->pvr ? TextFormat("%u  %s  (%ux%u)", i, texture->name, texture->width, texture->height)
            : TextFormat("%u  %s", i, texture->name);
        if (Tree_Item(tree, label, flags, NULL))
            clicked = (int)i;
    }
    Tree_EndGroup(tree);
    Tree_End(tree);
    if (clicked >= 0 && clicked != g_spr.selected)
        SelectTexture(clicked, false);
}

static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_spr.properties;
    if (g_spr.selected < 0) {
        Properties_Begin(properties, bounds, NULL);
        Properties_Note(properties, "Select a texture");
        Properties_End(properties);
        return;
    }
    const SPR_Texture* texture = &g_spr.file.textures[g_spr.selected];
    Properties_Begin(properties, bounds, TextFormat("Texture %d", g_spr.selected));
    Properties_Text(properties, "Name", texture->name);
    Properties_Text(properties, "Offset", TextFormat("0x%X", texture->offset));
    Properties_Text(properties, "Size", TextFormat("0x%X", texture->size));
    if (!texture->pvr)
        Properties_Note(properties, "It has no PVR texture");
    else {
        Properties_Section(properties, "PVR");
        Properties_Int(properties, "Width", texture->width);
        Properties_Int(properties, "Height", texture->height);
        Properties_Text(properties, "Pixel format", TextFormat("%u (%s)", texture->pixelFormat, PVR_PixelFormatToString(texture->pixelFormat)));
        Properties_Text(properties, "Data format", TextFormat("%u (%s)", texture->dataFormat, PVR_DataFormatToString(texture->dataFormat)));
        if (g_spr.failed)
            Properties_Note(properties, "It can't be decoded");
    }
    Properties_End(properties);
}

// Keeps the view inside the image: centered on the axes where the image is smaller than the view. scale is in window
// coordinates per pixel of the image
static void ClampCenter(Rectangle area, float scale) {
    float halfWidth = area.width / scale / 2.0f, halfHeight = area.height / scale / 2.0f;
    float width = (float)g_texture.width, height = (float)g_texture.height;
    if (width <= 2.0f * halfWidth)
        g_zoom.center.x = width / 2.0f;
    else
        g_zoom.center.x = fminf(fmaxf(g_zoom.center.x, halfWidth), width - halfWidth);
    if (height <= 2.0f * halfHeight)
        g_zoom.center.y = height / 2.0f;
    else
        g_zoom.center.y = fminf(fmaxf(g_zoom.center.y, halfHeight), height - halfHeight);
}

// Goes to the next zoom of g_zooms in the direction of the wheel, keeping the pixel of the image under the mouse
static void WheelZoom(Rectangle area, float dpi, float wheel) {
    float zoom = g_zoom.zoom;
    if (wheel > 0.0f) {
        for (int i = 0; i < IMAGEVIEW_ZOOM_COUNT; i++) {
            if (g_zooms[i] > g_zoom.zoom * 1.001f) {
                zoom = g_zooms[i];
                break;
            }
        }
    }
    else {
        for (int i = IMAGEVIEW_ZOOM_COUNT - 1; i >= 0; i--) {
            if (g_zooms[i] < g_zoom.zoom * 0.999f) {
                zoom = g_zooms[i];
                break;
            }
        }
    }
    if (zoom == g_zoom.zoom)
        return;
    Vector2 mouse = GetMousePosition();
    float dx = mouse.x - (area.x + area.width / 2.0f), dy = mouse.y - (area.y + area.height / 2.0f);
    float oldScale = g_zoom.zoom / dpi, newScale = zoom / dpi;
    g_zoom.center.x += dx / oldScale - dx / newScale;
    g_zoom.center.y += dy / oldScale - dy / newScale;
    g_zoom.zoom = zoom;
    g_zoom.mode = zoom == 1.0f ? ZOOM_ACTUAL : ZOOM_CUSTOM;
}

static void DrawImage(Rectangle area) {
    g_zoom.hoveredX = g_zoom.hoveredY = -1;
    if (g_texture.id == 0 || area.width <= 0.0f || area.height <= 0.0f)
        return;

    // The zoom is in pixels of the screen: with a scale of the display (e.g. 125%), the coordinates of the window are scaled
    float dpi = GetWindowScaleDPI().x;
    if (dpi <= 0.0f)
        dpi = 1.0f;
    float width = (float)g_texture.width, height = (float)g_texture.height;
    if (g_zoom.mode == ZOOM_FIT)
        g_zoom.zoom = fminf(area.width / width, area.height / height) * dpi;
    else if (g_zoom.mode == ZOOM_ACTUAL)
        g_zoom.zoom = 1.0f;

    // The wheel zooms and a drag with the left or middle button moves the image
    Vector2 mouse = GetMousePosition();
    bool mouseInArea = CheckCollisionPointRec(mouse, area);
    float wheel = mouseInArea ? GetMouseWheelMove() : 0.0f;
    if (wheel != 0.0f)
        WheelZoom(area, dpi, wheel);
    float scale = g_zoom.zoom / dpi;
    if (mouseInArea && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))) {
        g_zoom.dragging = true;
        g_zoom.dragStart = mouse;
        g_zoom.dragCenter = g_zoom.center;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        g_zoom.dragging = false;
    if (g_zoom.dragging) {
        g_zoom.center.x = g_zoom.dragCenter.x - (mouse.x - g_zoom.dragStart.x) / scale;
        g_zoom.center.y = g_zoom.dragCenter.y - (mouse.y - g_zoom.dragStart.y) / scale;
    }
    ClampCenter(area, scale);

    // Sharp pixels when enlarged, smoothed when reduced
    int filter = g_zoom.zoom < 1.0f ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT;
    if (filter != g_zoom.filter) {
        SetTextureFilter(g_texture, filter);
        g_zoom.filter = filter;
    }

    // The corner of the image on a pixel of the screen
    float x = roundf((area.x + area.width / 2.0f - g_zoom.center.x * scale) * dpi) / dpi;
    float y = roundf((area.y + area.height / 2.0f - g_zoom.center.y * scale) * dpi) / dpi;
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawTexturePro(
        g_texture,
        (Rectangle) { 0, 0, width, height },
        (Rectangle) { x, y, width * scale, height * scale },
        (Vector2)   { 0, 0 },
        0,
        WHITE
    );
    EndScissorMode();

    if (mouseInArea) {
        int px = (int)floorf((mouse.x - x) / scale), py = (int)floorf((mouse.y - y) / scale);
        if (px >= 0 && py >= 0 && px < g_texture.width && py < g_texture.height) {
            g_zoom.hoveredX = px;
            g_zoom.hoveredY = py;
        }
    }
}

void ImageView_Draw(Rectangle area) {
    if (!HasList()) {
        DrawImage(area);
        return;
    }
    // The list of the textures at the left, the selected one at the right
    float margin = IMAGEVIEW_MARGIN;
    Rectangle column = { area.x + margin, area.y + margin, IMAGEVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle image = { column.x + column.width + margin, area.y + margin, area.width - column.width - margin * 3.0f, area.height - margin * 2.0f };
    if (column.height <= 0.0f || image.width <= 0.0f)
        return;
    Rectangle list, properties;
    Properties_Split(column, &list, &properties);
    DrawList(list);
    DrawProperties(properties);
    DrawImage(image);
}

bool ImageView_Export(const char* filepath) {
    if (g_image.data == NULL)
        return false;
    return ExportImage(g_image, filepath);
}

void ImageView_ExportDialog(void) {
    static char lastFolder[1024];
    if (g_image.data == NULL) {
        printf("No image to export.\n");
        return;
    }

    // The name of the file without its extensions (also .gz), with the texture of the SPR file if it has several
    char name[256];
    File_GetBaseName(g_path, name, sizeof(name));
    if (HasList() && g_spr.selected >= 0) {
        size_t length = strlen(name);
        snprintf(name + length, sizeof(name) - length, "_%d_%s", g_spr.selected, g_spr.file.textures[g_spr.selected].name);
    }
    if (name[0] == '\0')
        snprintf(name, sizeof(name), "texture");
    File_SanitizeName(name);
    strncat(name, ".png", sizeof(name) - strlen(name) - 1);

    // It opens in the folder of the last export, or in that of the texture
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s", lastFolder[0] != '\0' ? lastFolder : GetDirectoryPath(g_path));

    nfdu8char_t* outPath = NULL;
    nfdu8filteritem_t filters[3] = { { "PNG image", "png" }, { "BMP image", "bmp" }, { "JPG image", "jpg" } };
    nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 3, folder[0] != '\0' ? folder : NULL, name);
    if (result == NFD_OKAY) {
        if (ImageView_Export(outPath))
            snprintf(lastFolder, sizeof(lastFolder), "%s", GetDirectoryPath(outPath));
        else
            printf("Failed to export the image to %s\n", outPath);
        NFD_FreePathU8(outPath);
    }
    else if (result == NFD_ERROR)
        printf("Error: %s\n", NFD_GetError());
}

void ImageView_Status(ViewStatus* status) {
    snprintf(status->file, sizeof(status->file), "%s", g_path);
    char image[64];
    snprintf(image, sizeof(image), "%dx%d pixels, %g%%", g_image.width, g_image.height, roundf(g_zoom.zoom * 1000.0f) / 10.0f);
    if (HasList() && g_spr.selected >= 0) {
        const SPR_Texture* texture = &g_spr.file.textures[g_spr.selected];
        snprintf(status->info, sizeof(status->info), "%u textures, %d: %s, %s", g_spr.file.count, g_spr.selected, texture->name, image);
    }
    else
        snprintf(status->info, sizeof(status->info), "%s", image);
    // The pixel under the mouse, from the image without the checkered pattern
    if (g_image.data != NULL && g_zoom.hoveredX >= 0 && g_zoom.hoveredX < g_image.width && g_zoom.hoveredY < g_image.height) {
        const uint8_t* pixel = (const uint8_t*)g_image.data + ((size_t)g_zoom.hoveredY * g_image.width + g_zoom.hoveredX) * 4;
        size_t length = strlen(status->info);
        snprintf(status->info + length, sizeof(status->info) - length, ", pixel %d, %d: RGBA %u, %u, %u, %u", g_zoom.hoveredX,
            g_zoom.hoveredY, pixel[0], pixel[1], pixel[2], pixel[3]);
    }
    snprintf(status->hint, sizeof(status->hint), "Wheel: zoom, drag: move");
}
